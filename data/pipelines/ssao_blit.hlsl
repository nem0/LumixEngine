//@include "pipelines/common.hlsli"

cbuffer Data : register(b4) {
	float2 u_size;
	uint u_ssao_buf;
	uint u_gbufferB;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	int2 ij = int2(thread_id.xy);
	float2 uv = float2(ij) / u_size.xy;
	float ssao = sampleBindlessLod(LinearSamplerClamp, u_ssao_buf, uv, 0).x;
	float4 v = bindless_rw_textures[u_gbufferB][ij];
	v.w *= ssao;
	bindless_rw_textures[u_gbufferB][ij] = v;
}
