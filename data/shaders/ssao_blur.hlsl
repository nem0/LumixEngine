// depth-aware blur using poisson disk sampling

#include "shaders/common.hlsli"

cbuffer UB : register(b4) {
	float2 u_rcp_size;
	float u_weight_scale;
	TextureHandle u_input;
	TextureHandle u_depth_buffer;
	RWTextureHandle u_output;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 uv_base = thread_id.xy * u_rcp_size;
	float4 sum = bindless_textures[u_input][thread_id.xy];

	float weight_sum = 1;
	float ndc_depth = sampleBindlessLod(LinearSamplerClamp, u_depth_buffer, uv_base, 0).r;
	float depth = 0.1 / ndc_depth;

	float sum_weight = 1;
	for (int i = -2; i <= 2; ++i) {
		for (int j = -2; j <= 2; ++j) {
			float sample_depth = 0.1 / bindless_textures[u_depth_buffer][(thread_id.xy + int2(i, j)) / 2].r;
			float w = saturate(u_weight_scale * depth / abs(sample_depth - depth));
			sum += bindless_textures[u_input][thread_id.xy + int2(i, j)] * w;
			sum_weight += w;
		}
	}

	bindless_rw_textures[u_output][thread_id.xy] = sum / sum_weight;
}
