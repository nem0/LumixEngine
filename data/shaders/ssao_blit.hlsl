#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	uint u_downscale;
	float u_depth_diff_weight;
	TextureHandle u_ssao_buf;
	TextureHandle u_depthbuffer; // full-size
	TextureHandle u_depthbuffer_small; // downscaled, if available
	RWTextureHandle u_gbufferB;
};


[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float ssao;
	if (u_downscale > 0) {
		// depth-aware upscale
		uint downscale_mask = (1 << u_downscale) - 1;
		uint step = 1 << u_downscale;
		uint2 ij = thread_id.xy & ~downscale_mask;
		uint2 ssao_xy = thread_id.xy >> u_downscale;

		// depths
		// sky/background has depth == 0, we use small value instead to avoid division by 0
		float d = 1 / max(10e-30, bindless_textures[u_depthbuffer][thread_id.xy].r);
		// u_depthbuffer_small does not contain 0, see ssao_downscale_depth.hlsl
		float d00 = 1 / bindless_textures[u_depthbuffer_small][ssao_xy + int2(0, 0)].r;
		float d10 = 1 / bindless_textures[u_depthbuffer_small][ssao_xy + uint2(1, 0)].r;
		float d01 = 1 / bindless_textures[u_depthbuffer_small][ssao_xy + uint2(0, 1)].r;
		float d11 = 1 / bindless_textures[u_depthbuffer_small][ssao_xy + uint2(1, 1)].r;

		// ssao values
		float ssao00 = bindless_textures[u_ssao_buf][ssao_xy + int2(0, 0)].r;
		float ssao10 = bindless_textures[u_ssao_buf][ssao_xy + int2(1, 0)].r;
		float ssao01 = bindless_textures[u_ssao_buf][ssao_xy + int2(0, 1)].r;
		float ssao11 = bindless_textures[u_ssao_buf][ssao_xy + int2(1, 1)].r;

		// coefs for bilinear filtering
		float2 uv = float2(thread_id.xy - ij) / (step - 1);
		float r00 = (1 - uv.x) * (1 - uv.y);
		float r10 = uv.x * (1 - uv.y);
		float r01 = (1 - uv.x) * uv.y;
		float r11 = uv.x * uv.y;

		// compute weights as combination of depth differences and bilinear coefs
		// depth difference added to bilinear coefs to fixed artifacts at depth discontinuities
		float w00 = saturate(r00 - saturate(r00 * abs(d - d00) * u_depth_diff_weight));
		float w10 = saturate(r10 - saturate(r10 * abs(d - d10) * u_depth_diff_weight));
		float w01 = saturate(r01 - saturate(r01 * abs(d - d01) * u_depth_diff_weight));
		float w11 = saturate(r11 - saturate(r11 * abs(d - d11) * u_depth_diff_weight));
	
		ssao = ssao00 * w00
			+ ssao10 * w10
			+ ssao01 * w01
			+ ssao11 * w11;
			
		float sum_w = w00 + w01 + w10 + w11;
		if (sum_w < 10e-6) {
			// our depth is not close to any from the four sampled depths, just use the first one
			ssao = ssao00;
		} else {
			// normalize weight (because of depth differences)
			ssao = saturate(ssao / sum_w); 
		}
	} else {
		// ssao is at full resolution, just sample it
		float2 uv = (thread_id.xy + 0.5) * Global_rcp_framebuffer_size;
		float depth = bindless_textures[u_depthbuffer][thread_id.xy].r;
	
		ssao = sampleBindlessLod(LinearSamplerClamp, u_ssao_buf, uv, 0).x;
	}

	// apply ssao to gbuffer
	float4 v = bindless_rw_textures[u_gbufferB][thread_id.xy];
	v.w *= ssao;
	bindless_rw_textures[u_gbufferB][thread_id.xy] = v;
}
