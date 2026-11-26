//@surface
#include "engine/shaders/common.hlsli"
#include "engine/shaders/atmo_common.hlsli"

// Sky pixel shader drawn in light_pass with hardware stencil test (stencil == 0),
// avoiding depth reads, HDR read-modify-write, and UAV transitions.

struct VSOutput {
	float2 uv : TEXCOORD0;
	float4 position : SV_POSITION;
};

cbuffer Data : register(b4) {
	AtmoParams u_atmo;
};

#ifdef FULL
float2 hash2(float2 p) {
	p = float2(dot(p, float2(127.1, 311.7)),
			   dot(p, float2(269.5, 183.3)));
	return frac(sin(p) * 43758.5453);
}

float3 hash3(float3 p) {
	p = float3(dot(p, float3(127.1, 311.7, 74.7)),
			  dot(p, float3(269.5, 183.3, 246.1)),
			  dot(p, float3(113.5, 271.9, 124.6)));
	return frac(sin(p) * 43758.5453);
}

float perlinNoise(float3 p) {
	float3 pi = floor(p);
	float3 pf = frac(p);
	float3 w = pf * pf * (3.0 - 2.0 * pf);

	float3 g000 = hash3(pi + float3(0.0, 0.0, 0.0)) * 2.0 - 1.0;
	float3 g100 = hash3(pi + float3(1.0, 0.0, 0.0)) * 2.0 - 1.0;
	float3 g010 = hash3(pi + float3(0.0, 1.0, 0.0)) * 2.0 - 1.0;
	float3 g110 = hash3(pi + float3(1.0, 1.0, 0.0)) * 2.0 - 1.0;
	float3 g001 = hash3(pi + float3(0.0, 0.0, 1.0)) * 2.0 - 1.0;
	float3 g101 = hash3(pi + float3(1.0, 0.0, 1.0)) * 2.0 - 1.0;
	float3 g011 = hash3(pi + float3(0.0, 1.0, 1.0)) * 2.0 - 1.0;
	float3 g111 = hash3(pi + float3(1.0, 1.0, 1.0)) * 2.0 - 1.0;

	float n000 = dot(g000, pf - float3(0.0, 0.0, 0.0));
	float n100 = dot(g100, pf - float3(1.0, 0.0, 0.0));
	float n010 = dot(g010, pf - float3(0.0, 1.0, 0.0));
	float n110 = dot(g110, pf - float3(1.0, 1.0, 0.0));
	float n001 = dot(g001, pf - float3(0.0, 0.0, 1.0));
	float n101 = dot(g101, pf - float3(1.0, 0.0, 1.0));
	float n011 = dot(g011, pf - float3(0.0, 1.0, 1.0));
	float n111 = dot(g111, pf - float3(1.0, 1.0, 1.0));

	float nx00 = lerp(n000, n100, w.x);
	float nx10 = lerp(n010, n110, w.x);
	float nx01 = lerp(n001, n101, w.x);
	float nx11 = lerp(n011, n111, w.x);

	float nxy0 = lerp(nx00, nx10, w.y);
	float nxy1 = lerp(nx01, nx11, w.y);

	return lerp(nxy0, nxy1, w.z);
}

float worleyNoise(float3 p) {
	float3 cell = floor(p);
	float min_dist = 1.0;
	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++) {
			for (int k = -1; k <= 1; k++) {
				float3 neighbor_cell = cell + float3(i, j, k);
				float3 feature_point = neighbor_cell + hash3(neighbor_cell);
				min_dist = min(min_dist, length(p - feature_point));
			}
		}
	}
	return min_dist;
}

float cloudPhase(float g, float cos_theta) {
	float w = 0.7;
	return w * miePhase(g, cos_theta) + (1.0 - w) * miePhase(-g, cos_theta);
}

float remap(float t, float min_from, float max_from, float min_to, float max_to) {
	return min_to + (t - min_from) / (max_from - min_from) * (max_to - min_to);
}

float heightFalloff(float h) {
	static const float UPPER_W = 2;
	static const float LOWER_W = 25;
	static const float BIAS = 0.02;
	float r = 1 - exp(-UPPER_W * (1 - h));
	r *= 1 - exp(-LOWER_W * h);
	r = max(0.01, r);
	r = 1 / r;
	r *= BIAS;
	return saturate(r);
}

float cloudDensity(float3 pos, bool shadow) {
	static const float coverage = 0.4;
	float height = (pos.y - u_atmo.clouds_bottom) / (u_atmo.clouds_top - u_atmo.clouds_bottom);
	float3 cloud_pos = pos * 0.00015;
	float height_factor = heightFalloff(height);

	float base_noise = perlinNoise(cloud_pos) * 0.5;
	base_noise += perlinNoise(cloud_pos * 1.7) * 0.5;
	base_noise = base_noise * 0.5 + 0.5;
	base_noise = saturate(base_noise - height_factor);
	base_noise -= 1 - remap(coverage, 0, 1, 0.4, 0.8);

	float worley0 = worleyNoise(cloud_pos * 5.0);
	float worley1 = 1 - worleyNoise(cloud_pos * 15.0);
	float worley2 = worleyNoise(cloud_pos * 40.0);
	float worleyFBM = worley0 * 0.625 + worley1 * 0.25;
	if (!shadow) {
		worleyFBM += worley2 * 0.125;
	}

	return saturate(base_noise - worleyFBM * 0.15);
}

struct Cloud {
	float3 color;
	float transmittance;
	float t_bottom;
};

#pragma warning(disable : 4000)
Cloud cloud(float2 screen_uv, float3 eyedir) {
	Cloud result;
	result.color = float3(0, 0, 0);
	result.transmittance = 1;
	result.t_bottom = 0;

	float3 cam_pos = Global_camera_world_pos.xyz;
	if ((eyedir.y < 0 && cam_pos.y < u_atmo.clouds_bottom) || (eyedir.y > 0 && cam_pos.y > u_atmo.clouds_top)) {
		return result;
	}

	float t_start = 0;
	float t_end = 100000.0;
	if (abs(eyedir.y) > 0.0001) {
		float t_bottom = (u_atmo.clouds_bottom - cam_pos.y) / eyedir.y;
		float t_top = (u_atmo.clouds_top - cam_pos.y) / eyedir.y;
		result.t_bottom = t_bottom;
		if (eyedir.y > 0) {
			t_start = max(0.0, t_bottom);
			t_end = t_top;
		} else {
			t_start = max(0.0, t_top);
			t_end = t_bottom;
		}
	} else {
		if (cam_pos.y < u_atmo.clouds_bottom || cam_pos.y > u_atmo.clouds_top) {
			return result;
		}
		t_start = 0.0;
		result.t_bottom = 0;
	}

	if (t_start >= t_end || t_end <= 0.0) {
		return result;
	}

	const int STEP_COUNT = 32;
	float step_size = (t_end - t_start) / STEP_COUNT;
	float offset = hash2(screen_uv).x;
	t_start += step_size * offset;

	float3 cloud_color = float3(0, 0, 0);
	float transmittance = 1.0;
	float cos_theta = -dot(eyedir, Global_light_dir.xyz);
	float hg_phase = cloudPhase(0.3, cos_theta);

	for (int i = 0; i < STEP_COUNT && transmittance > 0.01; i++) {
		float t = t_start + i * step_size;
		float3 pos = cam_pos + eyedir * t;
		if (pos.y < u_atmo.clouds_bottom || pos.y > u_atmo.clouds_top) break;

		float density = cloudDensity(pos, false);
		if (density > 0.01) {
			float shadow = 0.0;
			float3 light_pos = pos;
			const int LIGHT_SAMPLES = 4;
			const float light_step = 300;
			for (int j = 0; j < LIGHT_SAMPLES; j++) {
				light_pos += Global_light_dir.xyz * light_step;
				if (light_pos.y >= u_atmo.clouds_bottom && light_pos.y <= u_atmo.clouds_top) {
					shadow += cloudDensity(light_pos, true) * light_step * 0.10;
				}
			}

			float light_transmittance = exp(-shadow);
			float3 sunlight = u_atmo.sunlight.rgb * u_atmo.sunlight.a;
			float height = (pos.y - u_atmo.clouds_bottom) / (u_atmo.clouds_top - u_atmo.clouds_bottom);
			float3 ambient = sunlight * lerp(0.01, 0.04, height);
			float3 luminance = sunlight * light_transmittance * hg_phase + ambient;
			float3 scatter_light = luminance * density;
			float tr = exp(-density * step_size * 0.05);
			cloud_color += transmittance * (scatter_light - scatter_light * tr) / density;
			transmittance *= tr;
		}
	}

	result.color = cloud_color;
	result.transmittance = saturate(transmittance - 0.01);
	return result;
}
#endif

VSOutput mainVS(uint vertexID : SV_VertexID) {
	VSOutput output;
	output.position = fullscreenQuad(vertexID, output.uv);
	return output;
}

float4 mainPS(VSOutput input) : SV_Target {
	float3 sunlight = u_atmo.sunlight.rgb * u_atmo.sunlight.a;
	float3 eyedir = getViewDirection(input.uv);
	const float cos_theta = dot(eyedir, Global_light_dir.xyz);

	float2 v = float2(1, max(0, eyedir.y));
	float4 insc = sampleBindlessLod(LinearSamplerClamp, u_atmo.inscatter, v, 0);
	float3 atmo_rgb =
		insc.aaa * miePhase(0.75, -cos_theta) * sunlight * u_atmo.scatter_mie.rgb
		+ insc.rgb * rayleighPhase(-cos_theta) * sunlight * u_atmo.scatter_rayleigh.rgb;

	float3 sun_spot = 0;
	if (cos_theta > 0.995) {
		const float3 extinction_rayleigh = u_atmo.scatter_rayleigh.rgb;
		const float3 extinction_mie = u_atmo.scatter_mie.rgb + u_atmo.absorb_mie.rgb;
		const float3 cam_origin = float3(0, u_atmo.bot, 0);
		float3 p = cam_origin + Global_camera_world_pos.xyz;
		float p_height = saturate((length(p) - u_atmo.bot) / (u_atmo.top - u_atmo.bot));
		float2 opt_depth = sampleBindlessLod(LinearSamplerClamp, u_atmo.optical_depth, float2(abs(eyedir.y), p_height), 0).xy;
		sun_spot = smoothstep(0.0, 1000.0, phase(cos_theta, 0.9995)) * 200;
		sun_spot *= exp(-opt_depth.x * extinction_rayleigh - opt_depth.y * extinction_mie);
	}

	#ifdef FULL
	if (u_atmo.clouds_enabled > 0) {
		Cloud cl = cloud(input.uv, eyedir);
		float t = saturate(cl.t_bottom / 80000);
		atmo_rgb = (1 - t) * atmo_rgb * cl.transmittance
			+ cl.color
			+ t * atmo_rgb;
		sun_spot *= (1 - t) * cl.transmittance;
	}
	#endif
	atmo_rgb += sun_spot;

	#if defined(FOG) || defined(FULL)
	if (u_atmo.fog_enabled > 0) {
		float dist = 1e5 / dot(eyedir, Global_view_dir.xyz);
		float3 fog_trans;
		#ifdef FULL
		const bool allow_godrays = true;
		#else
		const bool allow_godrays = false;
		#endif
		applyAtmoFog(u_atmo, eyedir, dist, cos_theta, input.position.xy, sunlight, allow_godrays, atmo_rgb, fog_trans);
	}
	#endif

	return float4(atmo_rgb, 1);
}
