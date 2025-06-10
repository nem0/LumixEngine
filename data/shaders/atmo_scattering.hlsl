#include "shaders/common.hlsli"

// https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016-pbs-frostbite-sky-clouds-new.pdf

cbuffer Data : register(b4) {
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
	float u_godarys_enabled;
	float u_clouds_enabled;
	float u_clouds_top;
	float u_clouds_bottom;
	RWTextureHandle u_inscatter;
	TextureHandle u_optical_depth;
};

float getOptDepthY(float3 position) {
	return saturate((length(position) - u_bot) / (u_top - u_bot));
}

float4 sampleOpticalDepth(float3 dir, float3 up, float height) {
	return sampleBindlessLod(LinearSamplerClamp, u_optical_depth, float2(saturate(dot(dir, up)), height), 0);	
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	const float3 extinction_rayleigh = u_scatter_rayleigh.rgb;
	const float3 extinction_mie = u_scatter_mie.rgb + 4.4e-6;

	float zenith_angle = thread_id.y / u_resolution.y * M_PI * 0.5;
	float3 eyedir = float3(cos(zenith_angle), sin(zenith_angle), 0);

	float3 campos = float3(0, u_bot, 0) + Global_camera_world_pos.xyz;
	float2 atmo_isect = raySphereIntersect(campos, eyedir, 0, u_top);
	
	if (atmo_isect.y < 0) {
		bindless_rw_textures[u_inscatter][thread_id.xy] = 0;
		return;
	}
	atmo_isect.x = max(0, atmo_isect.x);

	float3 rayleigh = 0;
	float3 mie = 0;
	float3 p = campos;//+ atmo_isect.x * eyedir;
	const int STEP_COUNT = 50;
	float dist = atmo_isect.y - atmo_isect.x;
	dist = thread_id.x == uint(u_resolution.x) - 1 
		? dist 
		: min(thread_id.x / u_resolution.x * 50e3, dist);
	const float step_len = dist / STEP_COUNT;
	const float3 step = step_len * eyedir;

	float height_b = getOptDepthY(campos);
	float4 depth_b = sampleOpticalDepth(eyedir, float3(0, 1, 0), height_b);
	const float cos_light_up = saturate(Global_light_dir.y);
	const float cos_eye_up = saturate(eyedir.y);
	
	for (int i = 0; i < STEP_COUNT; ++i) {
		float height_a = getOptDepthY(p);
		
		float3 up = normalize(p);
		float4 depth_a = sampleOpticalDepth(Global_light_dir.xyz, up, height_a);
		float4 depth_c = sampleOpticalDepth(eyedir, up, height_a);
		
		float3 x_rayleigh = (-depth_a.x - depth_b.x + depth_c.x) * extinction_rayleigh;
		float3 x_mie = (-depth_a.y - depth_b.y + depth_c.y) * extinction_mie;
		float3 total_transmittance = exp(x_rayleigh) * exp(x_mie);

		float h = min(0, u_bot - length(p));
		rayleigh += step_len * exp(h / u_distribution_rayleigh) * total_transmittance;
		mie += step_len * exp(h / u_distribution_mie) * total_transmittance;

		p += step; 
	}
	
	bindless_rw_textures[u_inscatter][thread_id.xy] = float4(rayleigh, mie.b);
}
