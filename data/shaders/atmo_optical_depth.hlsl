#include "shaders/common.hlsli"

cbuffer Data :register(b4) {
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
	RWTextureHandle u_output;
};

float3 getTopAtmo(float3 p, float3 dir) {
	float2 t = raySphereIntersect(p, dir, 0, u_top);
	return p + t.y * dir.xyz;
}

float opticalDepth(float3 a, float3 b, float distribution) {
	float l = length(a - b);
	int step_count = 50;
	float dens = 0;
	float3 step = (b - a) / step_count;
	float step_len = l / step_count;
	float3 p = a;
	for (int i = 0; i < step_count; ++i) {
		float h = length(p + step * 0.5);
		float tmp = exp(min(0, (u_bot - h) / distribution));
		dens += step_len * tmp;
		p += step;
	}
	return dens;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	const float2 xy = thread_id.xy / u_resolution.xy;
	float angle = xy.x * M_PI * 0.5;
	const float3 p = float3(0, u_bot + (u_top - u_bot) * xy.y, 0);
	const float3 dir = float3(sqrt(saturate(1 - xy.x * xy.x)), xy.x, 0);

	float rayleigh = opticalDepth(p, getTopAtmo(p, dir), u_distribution_rayleigh);
	float mie = opticalDepth(p, getTopAtmo(p, dir), u_distribution_mie);

	bindless_rw_textures[u_output][thread_id.xy] = float4(rayleigh, mie, 0, 0);
}
