#include "shaders/common.hlsli"

cbuffer Drawcall : register(b4) {
	float4 u_r_mask;
	float4 u_g_mask;
	float4 u_b_mask;
	float4 u_a_mask;
	float4 u_offsets;
	uint2 u_position;
	int2 u_scale;
	TextureHandle u_input;
	RWTextureHandle u_output;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float4 value = bindless_textures[u_input][thread_id.xy];
	
	bindless_rw_textures[u_output][thread_id.xy * u_scale + u_position] = float4(
		dot(value, u_r_mask) + u_offsets.r,
		dot(value, u_g_mask) + u_offsets.g,
		dot(value, u_b_mask) + u_offsets.b,
		dot(value, u_a_mask) + u_offsets.a
	);
}
