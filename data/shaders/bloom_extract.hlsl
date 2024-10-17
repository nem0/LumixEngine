#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	float u_avg_lum_multiplier;
	uint u_histogram;
	TextureHandle u_input;
	RWTextureHandle u_output;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float avg_lum = asfloat(bindless_buffers[u_histogram].Load(256 * 4));
	float3 c = bindless_textures[u_input][thread_id.xy * 2].rgb
		+ bindless_textures[u_input][thread_id.xy * 2 + uint2(1, 0)].rgb
		+ bindless_textures[u_input][thread_id.xy * 2 + uint2(1, 1)].rgb
		+ bindless_textures[u_input][thread_id.xy * 2 + uint2(0, 1)].rgb;
	float _luminance = luminance(c * 0.25);

	float multiplier = saturate(1 + _luminance - avg_lum * u_avg_lum_multiplier);
	bindless_rw_textures[u_output][thread_id.xy] = float4(c * multiplier, 1);
}
