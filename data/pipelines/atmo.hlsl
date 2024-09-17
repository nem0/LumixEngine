//@include "pipelines/common.hlsli"

cbuffer Data : register (b4) {
	float u_bot;
	float u_top;
	float u_distribution_rayleigh;
	float u_distribution_mie;
	float4 u_scatter_rayleigh;
	float4 u_scatter_mie;
	float4 u_absorb_mie;
	float4 u_sunlight;
	float4 u_resolution;
	float4 u_fog_scattering;
	float u_fog_top;
	float u_fog_enabled;
	float u_godrays_enabled;
	uint u_output;
	uint u_optical_depth;
	uint u_depth_buffer;
	uint u_inscatter;
};

// mie - Schlick appoximation phase function of Henyey-Greenstein
float miePhase(float g, float cos_theta) {
	float k = 1.55*g - 0.55*g*g*g; 
	float tmp = 1 + k * cos_theta;
	return (1 - k * k) / (4 * M_PI * tmp * tmp);
}

float rayleighPhase(float cos_theta) {
	return 3 / (16.0 * M_PI) * (1 + cos_theta * cos_theta);
}

float phase(float alpha, float g) {
	float a = 3.0*(1.0-g*g);
	float b = 2.0*(2.0+g*g);
	float c = 1.0+alpha*alpha;
	float d = pow(abs(1.0+g*g-2.0*g*alpha), 1.5);
	return (a/b)*(c/d);
}

float getFogDensity(float3 pos) {
	return pos.y > u_fog_top ? 0 : 1;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float4 atmo;
	float4 scene = 1;
	atmo.a = 1;
	
	float3 sunlight = u_sunlight.rgb * u_sunlight.a;
	float ndc_depth = bindless_textures[u_depth_buffer][thread_id.xy].r;
	float2 uv = thread_id.xy / (float2)Global_framebuffer_size.xy;
	float3 eyedir = getViewDirection(uv);
	const float cos_theta = dot(eyedir, Global_light_dir.xyz);

	if (ndc_depth > 0) {
		// sky is hidden some object
		float linear_depth = toLinearDepth(ndc_depth);
		float2 v = float2(
			saturate(linear_depth / 50e3),
			max(0, eyedir.y)
		);
		float4 insc = sampleBindlessLod(LinearSamplerClamp, u_inscatter, v, 0);

		float mie_phase = miePhase(0.75, -cos_theta);
		float rayleigh_phase = rayleighPhase(-cos_theta);
		atmo.rgb = 
			insc.aaa * mie_phase * sunlight * u_scatter_mie.rgb
			+ insc.rgb * rayleigh_phase * sunlight * u_scatter_rayleigh.rgb
			;
	}
	else {
		// sky is visible
		float sun_spot = smoothstep(0.0, 1000.0, phase(cos_theta, 0.9995)) * 200;

		float2 v = 1;
		v.y = max(0, eyedir.y);
	
		const float3 extinction_rayleigh = u_scatter_rayleigh.rgb;
		const float3 extinction_mie = u_scatter_mie.rgb + u_absorb_mie.rgb;
	
		const float3 cam_origin = float3(0, u_bot, 0);
		float3 p = cam_origin + Global_camera_world_pos.xyz;
		float4 insc = sampleBindlessLod(LinearSamplerClamp, u_inscatter, v, 0);
		float p_height = saturate((length(p) - u_bot) / (u_top - u_bot));
		float2 opt_depth = sampleBindlessLod(LinearSamplerClamp, u_optical_depth, float2(abs(eyedir.y), p_height), 0).xy;
		atmo.rgb = 
			insc.aaa * miePhase(0.75, -cos_theta) * sunlight * u_scatter_mie.rgb
			+ insc.rgb * rayleighPhase(-cos_theta) * sunlight * u_scatter_rayleigh.rgb
			+ sun_spot * exp(-opt_depth.x * extinction_rayleigh - opt_depth.y * extinction_mie) 
			;
	}

	if (u_fog_enabled > 0) {
		float linear_depth = ndc_depth > 0 ? toLinearDepth(ndc_depth) : 1e5;
		float dist = (linear_depth / dot(eyedir, Global_view_dir.xyz));
		float3 p0 = Global_camera_world_pos.xyz;
		float3 p1 = Global_camera_world_pos.xyz + eyedir * dist;
		makeAscending(p0, p1);

		const bool is_in_fog = p0.y < u_fog_top;
		if (is_in_fog) {
			const bool is_partially_in_fog = p1.y > u_fog_top;
			if (is_partially_in_fog) {
				// clip to top of fog
				float3 dir = p1 - p0;
				float3 diry1 =  dir / (abs(dir.y) < 1e-5 ? 1e-5 : dir.y);
				p1 -= diry1 * (p1.y - u_fog_top);
			}

			float3 fog_transmittance = exp(-distanceInFog(p0, p1) * u_fog_scattering.rgb);

			float3 inscatter = 0;
			{
				const int STEP_COUNT = u_godrays_enabled > 0 ? 32 : 8;
				float step_len = length(p1 - p0) / (STEP_COUNT + 1);
				float offset = hash((float2)thread_id.xy * 0.05);
				for (float f = (offset ) / STEP_COUNT; f < 1; f += 1.0 / (STEP_COUNT + 1)) {
					float3 p = lerp(p0, p1, f);
					float od = distanceInFog(p, p + Global_light_dir.xyz * 1e5); // TODO 1e5
					od += distanceInFog(p, Global_camera_world_pos.xyz);
					float shadow = u_godrays_enabled > 0 ? getShadowSimple(Global_shadowmap, p - Global_camera_world_pos.xyz) : 1;
					inscatter += getFogDensity(p) * step_len * exp(-od * u_fog_scattering.rgb) * shadow;
				}
			}

			scene.rgb = fog_transmittance;
			atmo.rgb += inscatter * u_fog_scattering.rgb * sunlight * miePhase(0.25, -cos_theta);
		}
	}

	bindless_rw_textures[u_output][thread_id.xy] = atmo + bindless_rw_textures[u_output][thread_id.xy] * scene;
}
