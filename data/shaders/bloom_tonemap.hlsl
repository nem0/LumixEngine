#include "shaders/common.hlsli"

cbuffer Drawcall : register (b4) {
	float u_exposure;
	TextureHandle u_input;
	uint u_accum;
	RWTextureHandle u_output;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float accum = asfloat(bindless_buffers[u_accum].Load(256 * 4));
	accum = max(accum, 1e-5);
	float exposure = u_exposure / (10 * accum);
	float3 input_rgb = bindless_textures[u_input][thread_id.xy].rgb;
	float3 tonemapped = ACESFilm(input_rgb * exposure);
	bindless_rw_textures[u_output][thread_id.xy] = float4(tonemapped, 1);
}
