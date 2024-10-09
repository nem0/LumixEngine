#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	TextureHandle u_ssao_buf;
	RWTextureHandle u_gbufferB;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 uv = thread_id.xy * Global_rcp_framebuffer_size;
	float ssao = sampleBindlessLod(LinearSamplerClamp, u_ssao_buf, uv, 0).x;
	float4 v = bindless_rw_textures[u_gbufferB][thread_id.xy];
	v.w *= ssao;
	bindless_rw_textures[u_gbufferB][thread_id.xy] = v;
}
