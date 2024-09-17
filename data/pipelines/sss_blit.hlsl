//@include "pipelines/common.hlsli"

cbuffer Data : register(b4) {
	float2 u_size;
	float u_current_frame_weight;
	uint u_sss;
	uint u_history;
	uint u_depthbuf;
	uint u_gbuffer2;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float depth = bindless_textures[u_depthbuf][thread_id.xy].x;
	
	float2 uv = (float2(thread_id.xy) + 0.5) / u_size.xy;
	float2 uv_prev = cameraReproject(uv, depth).xy;

	float current = bindless_textures[u_sss][thread_id.xy].x;
	if (all(uv_prev < 1) && all(uv_prev > 0)) {
		float prev = sampleBindlessLod(LinearSamplerClamp, u_history, uv_prev, 0).x;
		current = lerp(prev, current, u_current_frame_weight);
		bindless_rw_textures[u_sss][thread_id.xy] = current;
	}
	
	float4 gb2v = bindless_rw_textures[u_gbuffer2][thread_id.xy];
	gb2v.w = min(current, gb2v.w);
	bindless_rw_textures[u_gbuffer2][thread_id.xy] = gb2v;
}

