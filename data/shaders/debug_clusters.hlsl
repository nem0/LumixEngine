#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	TextureHandle u_deptbuffer;
	RWTextureHandle u_output;
};

float3 countToDebugColor(int c, uint2 thread_id) {
	float3 res;
	if (c == 0)
		res.rgb = bindless_rw_textures[u_output][thread_id].rgb;
	else if ((thread_id.x + thread_id.y) % 2)
		res.rgb = bindless_rw_textures[u_output][thread_id].rgb;
	else {
		res.rgb = lerp(0, float3(0, 0, 1), saturate(c / 5.0));
		res.rgb = lerp(res.rgb, float3(0, 1, 0), saturate((c - 5) / 10.0));
		res.rgb = lerp(res.rgb, float3(1, 0, 0), saturate((c - 15) / 20.0));
	}
	print(res.rgb, thread_id.xy % 64 - int2(28, 16), c);
	return res;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 screen_uv = thread_id.xy * Global_rcp_framebuffer_size;
	float depth_ndc;
	float3 pos_ws = getPositionWS(u_deptbuffer, screen_uv, depth_ndc);

	uint3 cluster_coord;
	Cluster cluster = getCluster(depth_ndc, thread_id.xy, cluster_coord);

	float4 o_color = 1;
	#ifdef LIGHTS
		o_color.rgb = countToDebugColor(cluster.lights_count, thread_id.xy);
	#else
		o_color.rgb = countToDebugColor(cluster.env_probes_count, thread_id.xy);
		// debug z binning
		// int mask = 1 << (cluster_coord.z % 3);
		// o_color.rgb = float3(mask >> 2, (mask >> 1) & 1, mask & 1);
	#endif
	bindless_rw_textures[u_output][thread_id.xy] = o_color;
}
