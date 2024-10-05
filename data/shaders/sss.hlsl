#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	float u_max_steps;
	float u_stride;
	TextureHandle u_depth;
	RWTextureHandle u_sss_buffer;
};

// based on http://casual-effects.blogspot.com/2014/08/screen-space-ray-tracing.html
void raycast(float3 ray_origin, float3 ray_dir, float stride, float jitter, uint2 frag_coord) {
	float3 csEndPoint = ray_origin + abs(ray_origin.z * 0.1) * ray_dir;

	float4 H0 = transformPosition(ray_origin, Global_vs_to_ndc);
	float4 H1 = transformPosition(csEndPoint, Global_vs_to_ndc);

	float k0 = 1 / H0.w, k1 = 1 / H1.w;

	float2 P0 = toScreenUV(H0.xy * k0 * 0.5 + 0.5) * Global_framebuffer_size;
	float2 P1 = toScreenUV(H1.xy * k1 * 0.5 + 0.5) * Global_framebuffer_size;

	float2 delta = P1 - P0;
	bool permute = abs(delta.x) < abs(delta.y);
	if (permute) {
		P0 = P0.yx;
		P1 = P1.yx;
		delta = delta.yx;
	}

	float step_dir = sign(delta.x);
	float invdx = step_dir / delta.x;

	float dk = ((k1 - k0) * invdx) * stride;
	float2  dP = (float2(step_dir, delta.y * invdx)) * stride;

	float2 P = P0;
	float k = k0;

	float depth_tolerance = 0.02;
	uint max_steps = uint(min(abs(P1.x - P0.x), u_max_steps)) >> 2;

	for (uint j = 0; j < 4; ++j) {
		P += dP * jitter;
		k += dk * jitter;
		for (uint i = 0; i < max_steps; ++i) {
			float ray_z_far = 1 / k;

			float2 p = permute ? P.yx : P;
			if (any(p < 0)) break;
			if (any(p > float2(Global_framebuffer_size))) break;

			float ndc_depth = sampleBindlessLod(LinearSamplerClamp, u_depth, p * Global_rcp_framebuffer_size, 0).x;
			float linear_depth = toLinearDepth(ndc_depth);
			
			float dif = ray_z_far - linear_depth;
			if (dif < linear_depth * depth_tolerance && dif > 1e-3) {
				bindless_rw_textures[u_sss_buffer][frag_coord] = 0;
				return;
			}

			P += dP;
			k += dk;
		}
		P -= dP;
		k -= dk;
		dP *= 2;
		dk *= 2;
		depth_tolerance *= 2;
	}
	bindless_rw_textures[u_sss_buffer][frag_coord] = 1;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 screen_uv = float2(thread_id.xy) * Global_rcp_framebuffer_size;
	float3 pos_ws = getPositionWS(u_depth, screen_uv);
	float4 pos_vs = transformPosition(pos_ws, Global_ws_to_vs);
	float3 light_dir_vs = mul(Global_light_dir.xyz, (float3x3)Global_ws_to_vs);
	float jitter = hash(float2(thread_id.xy) + Global_random_float2_normalized);
	raycast(pos_vs.xyz, light_dir_vs, u_stride, jitter, thread_id.xy);
}
