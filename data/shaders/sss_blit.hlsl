#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	float u_current_frame_weight;
	RWTextureHandle u_sss;
	TextureHandle u_history;
	TextureHandle u_depthbuf;
	RWTextureHandle u_gbuffer2;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float depth = bindless_textures[u_depthbuf][thread_id.xy].x;
	
	float2 screen_uv = (float2(thread_id.xy) + 0.5) * Global_rcp_framebuffer_size;
	float2 screen_uv_prev = cameraReproject(screen_uv, depth).xy;

	float current = bindless_textures[u_sss][thread_id.xy].x;
	if (all(screen_uv_prev < 1) && all(screen_uv_prev > 0)) {
		float prev = bindless_textures[u_history].SampleLevel(LinearSamplerClamp, screen_uv_prev, 0).x;
		current = lerp(prev, current, 0.1);
		bindless_rw_textures[u_sss][thread_id.xy] = current;
	}
	
	float4 gb2v = bindless_rw_textures[u_gbuffer2][thread_id.xy];
	gb2v.w = min(current, gb2v.w);
	bindless_rw_textures[u_gbuffer2][thread_id.xy] = gb2v;
}

