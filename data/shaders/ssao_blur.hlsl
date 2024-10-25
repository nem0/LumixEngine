// depth-aware blur using poisson disk sampling

#include "shaders/common.hlsli"

cbuffer UB : register(b4) {
	float2 u_rcp_size;
	float u_weight_scale;
	uint u_stride;
	uint u_downscale;
	TextureHandle u_input;
	TextureHandle u_depth_buffer;
	RWTextureHandle u_output;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float4 sum = bindless_textures[u_input][thread_id.xy];

	float ndc_depth = bindless_textures[u_depth_buffer][thread_id.xy].r;
	float depth = 0.1 / ndc_depth;

	float sum_weight = 1;
	#define BLUR_RADIUS 1
	for (int i = -BLUR_RADIUS; i <= BLUR_RADIUS; ++i) {
		for (int j = -BLUR_RADIUS; j <= BLUR_RADIUS; ++j) {
			float sample_depth = 0.1 / bindless_textures[u_depth_buffer][(thread_id.xy + int2(i, j) * u_stride)].r;
			float w = saturate(u_weight_scale * depth / abs(sample_depth - depth)) * 0.125;
			sum += bindless_textures[u_input][thread_id.xy + int2(i, j) * u_stride] * w;
			sum_weight += w;
		}
	}

	bindless_rw_textures[u_output][thread_id.xy] = sum / sum_weight;
}
