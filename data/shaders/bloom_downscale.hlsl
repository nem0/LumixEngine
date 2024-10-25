#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	TextureHandle u_input;
	RWTextureHandle u_output;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float4 o_color = bindless_textures[u_input][thread_id.xy * 2]
		+ bindless_textures[u_input][thread_id.xy * 2 + uint2(1, 0)]
		+ bindless_textures[u_input][thread_id.xy * 2 + uint2(1, 1)]
		+ bindless_textures[u_input][thread_id.xy * 2 + uint2(0, 1)];
	o_color *= 0.25;
	bindless_rw_textures[u_output][thread_id.xy] = o_color;
}
