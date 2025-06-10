#include "shaders/common.hlsli"

// clouds:
// https://www.youtube.com/watch?v=Qj_tK_mdRcA
// https://x.com/FewesW/status/1364629939568451587/photo/1
// https://www.shadertoy.com/view/3sffzj
// https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf

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
	RWTextureHandle u_output;
	TextureHandle u_optical_depth;
	TextureHandle u_depth_buffer;
	TextureHandle u_inscatter;
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

// Hash function for pseudorandom values
float2 hash2(float2 p) {
	p = float2(dot(p, float2(127.1, 311.7)),
			   dot(p, float2(269.5, 183.3)));
	return frac(sin(p) * 43758.5453);
}

// 3D hash function
float3 hash3(float3 p) {
	p = float3(dot(p, float3(127.1, 311.7, 74.7)),
			  dot(p, float3(269.5, 183.3, 246.1)),
			  dot(p, float3(113.5, 271.9, 124.6)));
	return frac(sin(p) * 43758.5453);
}

// 3D Perlin noise implementation
float perlinNoise(float3 p) {
	float3 pi = floor(p);
	float3 pf = frac(p);
	
	// Cubic Hermite curve for smooth interpolation
	float3 w = pf * pf * (3.0 - 2.0 * pf);
	
	// Eight corners of the cube
	float3 g000 = hash3(pi + float3(0.0, 0.0, 0.0)) * 2.0 - 1.0;
	float3 g100 = hash3(pi + float3(1.0, 0.0, 0.0)) * 2.0 - 1.0;
	float3 g010 = hash3(pi + float3(0.0, 1.0, 0.0)) * 2.0 - 1.0;
	float3 g110 = hash3(pi + float3(1.0, 1.0, 0.0)) * 2.0 - 1.0;
	float3 g001 = hash3(pi + float3(0.0, 0.0, 1.0)) * 2.0 - 1.0;
	float3 g101 = hash3(pi + float3(1.0, 0.0, 1.0)) * 2.0 - 1.0;
	float3 g011 = hash3(pi + float3(0.0, 1.0, 1.0)) * 2.0 - 1.0;
	float3 g111 = hash3(pi + float3(1.0, 1.0, 1.0)) * 2.0 - 1.0;
	
	// Dot products
	float n000 = dot(g000, pf - float3(0.0, 0.0, 0.0));
	float n100 = dot(g100, pf - float3(1.0, 0.0, 0.0));
	float n010 = dot(g010, pf - float3(0.0, 1.0, 0.0));
	float n110 = dot(g110, pf - float3(1.0, 1.0, 0.0));
	float n001 = dot(g001, pf - float3(0.0, 0.0, 1.0));
	float n101 = dot(g101, pf - float3(1.0, 0.0, 1.0));
	float n011 = dot(g011, pf - float3(0.0, 1.0, 1.0));
	float n111 = dot(g111, pf - float3(1.0, 1.0, 1.0));
	
	// Trilinear interpolation
	float nx00 = lerp(n000, n100, w.x);
	float nx10 = lerp(n010, n110, w.x);
	float nx01 = lerp(n001, n101, w.x);
	float nx11 = lerp(n011, n111, w.x);
	
	float nxy0 = lerp(nx00, nx10, w.y);
	float nxy1 = lerp(nx01, nx11, w.y);
	
	return lerp(nxy0, nxy1, w.z);
}

// Worley noise (cellular noise) implementation
float worleyNoise(float3 p) {
	// Find the base cell coordinates
	float3 cell = floor(p);
	
	float min_dist = 1.0; // Initialize with a large value
	
	// Check the current cell and its neighbors
	for(int i = -1; i <= 1; i++) {
		for(int j = -1; j <= 1; j++) {
			for(int k = -1; k <= 1; k++) {
				// Neighbor cell coordinates
				float3 neighbor_cell = cell + float3(i, j, k);
				
				// Get random point within this cell
				float3 random_point = hash3(neighbor_cell);
				
				// Feature point in world coordinates
				float3 feature_point = neighbor_cell + random_point;
				
				// Calculate distance to this feature point
				float dist = length(p - feature_point);
				
				// Keep the minimum distance
				min_dist = min(min_dist, dist);
			}
		}
	}
	
	return min_dist;
}

static const float CLOUD_TOP = 4000;
static const float CLOUD_BOT = 2000;

float cloudPhase(float g, float cos_theta) {
	// Double Henyey-Greenstein phase function with Schlick approximation
	float w = 0.7;          // weight (favoring forward scattering)
	
	// Forward scattering component
	float hg1 = miePhase(g, cos_theta);
	
	// Backscattering component
	float hg2 = miePhase(-g, cos_theta);

	// Weighted combination
	return w * hg1 + (1.0 - w) * hg2;
}

float3 multipleOctaves(float extinction, float mu, float stepL){
    float3 luminance = float3(0, 0, 0);
    const float octaves = 4.0;
    
    // Attenuation
    float a = 1.0;
    // Contribution
    float b = 1.0;
    // Phase attenuation
    float c = 1.0;
    
    float phase;
    
    for(float i = 0.0; i < octaves; i++){
        phase = lerp(miePhase(-0.1 * c, mu), miePhase(0.3 * c, mu), 0.7);
        luminance += b * phase * exp(-stepL * extinction * a);
        // Lower is brighter
        a *= 0.2;
        // Higher is brighter
        b *= 0.5;
        c *= 0.5;
    }
    return luminance;
}

float cloudDensity(float3 pos, float coverage) {
	// use perlin worley noise
	float height = (pos.y - CLOUD_BOT) / (CLOUD_TOP - CLOUD_BOT);
	float height_factor = 1.0 - abs(height - 0.2) * 1.25;
	height_factor = saturate(height_factor);

	float3 cloud_pos = pos * 0.0002;

	// Base noise for cloud shape using 3D Perlin noise
	float base_noise = perlinNoise(float3(cloud_pos.x, 0, cloud_pos.z)) * 0.5 + 0.5;

	//cloud_pos += Global_time * float3(0.1, 0, 0.1);

	float detail = worleyNoise(cloud_pos * 6.0) * 0.625 + 
					worleyNoise(cloud_pos * 12.0) * 0.25 +
					worleyNoise(cloud_pos * 24.0) * 0.125;

	float den =  saturate((base_noise - 0.4) - detail * 0.35) ;
	den *= height_factor * height_factor;
	return den;
}

struct Cloud {
	float3 color;
	float transmittance;
	float t_bottom;
};

// raymarched cloud with shadowing for flat earth model
// returns final cloud color
Cloud cloud(float2 screen_uv) {
	// Cloud coverage parameter [0-1]
	Cloud result;
	result.color = float3(0, 0, 0);
	result.transmittance = 1;
	float coverage = 0.6; // Default coverage - modify as needed
	
	// Get view direction based on screen UV
	float3 eyedir = getViewDirection(screen_uv);
	
	// Camera position
	float3 cam_pos = Global_camera_world_pos.xyz/* + Global_time * float3(1000, 0, 1)*/;
	
	// Early exit if looking down and below clouds or looking up and above clouds
	if ((eyedir.y < 0 && cam_pos.y < CLOUD_BOT) || (eyedir.y > 0 && cam_pos.y > CLOUD_TOP)) {
		return result;
	}
	
	// Ray-plane intersection for flat cloud layer
	float t_start = 0;
	float t_end = 100000.0;
	
	if (abs(eyedir.y) > 0.0001) {
		// Calculate intersections with cloud bottom and top planes
		float t_bottom = (CLOUD_BOT - cam_pos.y) / eyedir.y;
		float t_top = (CLOUD_TOP - cam_pos.y) / eyedir.y;
		result.t_bottom = t_bottom;
		
		// Ensure correct order based on view direction
		if (eyedir.y > 0) {
			t_start = max(0.0, t_bottom);
			t_end = t_top;
		} else {
			t_start = max(0.0, t_top);
			t_end = t_bottom;
		}
	} else {
		// Looking parallel to horizon (no intersection with cloud layer)
		if (cam_pos.y < CLOUD_BOT || cam_pos.y > CLOUD_TOP) {
			return result;
		}
		// Inside cloud layer looking horizontally
		t_start = 0.0;
		result.t_bottom = 0;
	}
	
	// Validate ray segment
	if (t_start >= t_end || t_end <= 0.0) {
		return result;
	}
	
	// Number of steps for ray marching
	const int STEP_COUNT = 16;
	float step_size = (t_end - t_start) / STEP_COUNT;
	
	// Add jitter to reduce banding
	float offset = hash2(screen_uv).x;
	t_start += step_size * offset;
	
	// Initialize ray marching accumulation variables
	float3 cloud_color = float3(0, 0, 0);
	float transmittance = 1.0;
	
	float cos_theta = -dot(eyedir, Global_light_dir.xyz);
	float hg_phase = cloudPhase(0.3, cos_theta);

	// March along the view ray
	for (int i = 0; i < STEP_COUNT && transmittance > 0.01; i++) {
		float t = t_start + i * step_size;
		float3 pos = cam_pos + eyedir * t;
		
		// we are outside of clouds, abort
		if (pos.y < CLOUD_BOT || pos.y > CLOUD_TOP) break;
		
		float density = cloudDensity(pos, coverage);
		
		if (density > 0.01) {
			// Calculate light attenuation (shadowing)
			float shadow = 0.0;
			float3 light_pos = pos;
			
			const int LIGHT_SAMPLES = 2;
			const float light_step = 400;
			
			for (int j = 0; j < LIGHT_SAMPLES; j++) {
				light_pos += Global_light_dir.xyz * light_step;
				// Skip shadow samples outside cloud layer
				if (light_pos.y >= CLOUD_BOT && light_pos.y <= CLOUD_TOP) {
					shadow += cloudDensity(light_pos, coverage) * light_step * 0.05;
				}
			}
			
			float3 beersLaw = multipleOctaves(shadow, cos_theta, light_step);
			// Light transmission through cloud
			float light_transmittance = exp(-shadow);
			
			// Calculate lighting with sunlight
			float3 sunlight = u_sunlight.rgb * u_sunlight.a;
			
			// Ambient sky color
			float height = (pos.y - CLOUD_BOT) / (CLOUD_TOP - CLOUD_BOT);
			float3 ambient = sunlight * lerp(0.01, 0.04, height);

			float luminance = sunlight * light_transmittance * hg_phase + ambient;

			// Calculate lighting with enhanced powder effect
			float3 scatter_light = luminance * density;
			
			float t = exp(-density * step_size * 0.05);

			// Add to final color, considering current transmittance
			cloud_color += transmittance * (scatter_light - scatter_light * t) / density;
			
			// Update transmittance along view ray
			transmittance *= t;
		}
	}
	
	result.color = cloud_color;
	result.transmittance = saturate(transmittance - 0.01);
	return result;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float4 atmo;
	float4 scene = 1;
	atmo.a = 1;
	
	float3 sunlight = u_sunlight.rgb * u_sunlight.a;
	float ndc_depth = bindless_textures[u_depth_buffer][thread_id.xy].r;
	float2 uv = thread_id.xy * Global_rcp_framebuffer_size;
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
			;

		/*
		Cloud cl = cloud(uv);
		float t = saturate(cl.t_bottom / 80000);
		atmo.rgb = (1 - t) * atmo.rgb * cl.transmittance // atmo behind the cloud
			+ cl.color // cloud
			+ (t) * atmo.rgb; // atmo in front of the cloud
		
		atmo.rgb += sun_spot * exp(-opt_depth.x * extinction_rayleigh - opt_depth.y * extinction_mie) * (1 - t) * cl.transmittance;
		*/
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
