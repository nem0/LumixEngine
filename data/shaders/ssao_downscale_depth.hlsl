#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	uint u_scale;
	TextureHandle u_input;
	RWTextureHandle u_output;
};


[numthreads(8, 8, 1)]
void main(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID) {
	// this just picks the first texel from the input
	// because ssao.hlsl picks the first texel from the input normal map
	// and we need matching normal and depth, otherwise we get false self occlusion
	float value = bindless_textures[u_input][thread_id.xy * u_scale].r;
	// sky/background has depth == 0, we use small value instead to avoid issues in later stages (see ssao_blit.hlsl)
	bindless_rw_textures[u_output][thread_id.xy] = max(10e-30, value); 
}
