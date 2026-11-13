#include "engine/shaders/common.hlsli"

// top-down ambient occlusion

cbuffer Drawcall : register(b4) {
	float4 u_offset;
	float2 u_rcp_size;
	float u_intensity;
	float u_rcp_range;
	float u_half_depth_range;
	float u_scale;
	float u_depth_offset;
	TextureHandle u_depth_buffer;
	RWTextureHandle u_gbufferB;
	TextureHandle u_topdown_depthmap;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	// compute td-space position
	float2 screen_uv = thread_id.xy * u_rcp_size;
	float3 pos_td = getPositionWS(u_depth_buffer, screen_uv);
	pos_td += u_offset.xyz;
	pos_td.y += u_depth_offset;

	// compute uv in tdao texture space
	float2 uv = pos_td.xz * u_rcp_range;
	#ifdef _ORIGIN_BOTTOM_LEFT
		uv = uv * float2(1, -1);
	#endif
	if (any(abs(uv) > 1)) return;
	uv = saturate(uv * 0.5 + 0.5);

	// A sample of the map is a point above the pixel, `height` over it and `offset_world` away. It occludes the pixel only if it is in the hemisphere of the surface:
	// the top of a wall that the pixel is on is behind the wall, so it does not, and the awning above a table leg is in front of it, so it does.
	float4 gbufferB_value = bindless_rw_textures[u_gbufferB][thread_id.xy];
	const float3 normal = normalize(gbufferB_value.xyz * 2 - 1);
	const float world_per_uv = 2.0 / u_rcp_range;	// the map covers 2 * range

	// create random rotation matrix
	float random_angle = 2 * M_PI * hash(float2(thread_id.xy) * 0.01);
	float c = cos(random_angle);
	float s = sin(random_angle);
	float2x2 rot = u_scale * float2x2(c, s, -s, c); 

	// compute tdao
	float ao = 0;
	for (int i = 0; i < 16; ++i) {
		const float2 offset = mul(POISSON_DISK_16[i], rot);
		float td_depth_ndc = sampleBindlessLod(LinearSamplerClamp, u_topdown_depthmap, uv + offset, 0).r;
		float td_depth = (td_depth_ndc * 2 - 1) * u_half_depth_range;
		const float height = td_depth - pos_td.y;
		float2 offset_world = offset * world_per_uv;
		#ifdef _ORIGIN_BOTTOM_LEFT
			offset_world.y = -offset_world.y;
		#endif
		const float3 to_occluder = float3(offset_world.x, height, offset_world.y);
		const float facing = dot(to_occluder, normal) / max(length(to_occluder), 1e-3);
		ao += saturate(height) * saturate(facing * 4);	// 4: samples that are well above a flat surface count fully, as without the test
	}
	ao *= u_intensity / 16;

	// add tdao to ao
	gbufferB_value.w = gbufferB_value.w * (1 - ao);
	bindless_rw_textures[u_gbufferB][thread_id.xy] = gbufferB_value;
}

