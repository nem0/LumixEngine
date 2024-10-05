#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	TextureHandle u_input;
	RWTextureHandle u_output;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 uv = thread_id.xy * Global_rcp_framebuffer_size;
	float4 a = sampleBindlessLod(LinearSamplerClamp, u_input, uv, 0);
	float4 b = bindless_rw_textures[u_output][thread_id.xy];
	bindless_rw_textures[u_output][thread_id.xy] = a + b;
}
