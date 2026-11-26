#include "engine/shaders/common.hlsli"

// Builds the reversed-Z depth pyramid for occlusion culling (see PipelineImpl::buildHZB).
// Each texel is the min (farthest) depth of the 2x2 (or 3x3 on odd borders) texels below it.
// Up to 5 levels are reduced per dispatch in shared memory: each 8x8 threadgroup owns one
// texel of the coarsest level of the dispatch and all finer texels under it.
//   u_mode 0: source is the depth buffer (SRV)
//   u_mode 1: source is a previous pyramid mip (UAV)

cbuffer Data : register(b4) {
	uint u_mode;
	uint u_src;
	uint u_dst;			// mip n + 1
	uint u_levels;		// 1..5
	uint2 u_src_size;
	uint2 u_dst_size;	// size of mip n + u_levels
	uint u_dst1;		// mip n + 2 .. n + 5
	uint u_dst2;
	uint u_dst3;
	uint u_dst4;
};

// Worst-case border group (u_levels == 5) has <= 31x31 texels at level 1 and <= 15x15 at level 2.
groupshared float g_a[32][32];
groupshared float g_b[16][16];

float loadSrc(uint2 p) {
	return u_mode == 0 ? bindless_textures[u_src][p].r : bindless_rw_textures[u_src][p].r;
}

uint dstView(uint level) {
	if (level == 1) return u_dst;
	if (level == 2) return u_dst1;
	if (level == 3) return u_dst2;
	if (level == 4) return u_dst3;
	return u_dst4;
}

float reduceSrc(uint2 from, uint2 to, float2 inv_size) {
	if (u_mode == 0 && all(to == from + 1)) {
		float4 g = bindless_textures[u_src].GatherRed(LinearSamplerClamp, (float2(from) + 1.0) * inv_size);
		return min(min(g.x, g.y), min(g.z, g.w));
	}
	float d = 1e30;
	for (uint y = from.y; y <= to.y; ++y)
		for (uint x = from.x; x <= to.x; ++x)
			d = min(d, loadSrc(uint2(x, y)));
	return d;
}

float reduceLDS(uint level, uint2 from, uint2 to) {
	float d = 1e30;
	for (uint y = from.y; y <= to.y; ++y)
		for (uint x = from.x; x <= to.x; ++x)
			d = min(d, (level & 1) == 0 ? g_a[y][x] : g_b[y][x]);
	return d;
}

[numthreads(8, 8, 1)]
void main(uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID) {
	const uint2 group = group_id.xy;
	const uint2 tid = local_id.xy;
	const float2 inv_src_size = 1.0 / float2(u_src_size);

	for (uint level = 1; level <= u_levels; ++level) {
		const uint shift = u_levels - level;
		const uint2 size = max(u_src_size >> level, 1);
		const uint2 prev_size = max(u_src_size >> (level - 1), 1);

		const uint2 lo = group << shift;
		uint2 hi = min(((group + 1) << shift) - 1, size - 1);
		if (group.x == u_dst_size.x - 1) hi.x = size.x - 1;
		if (group.y == u_dst_size.y - 1) hi.y = size.y - 1;

		const uint2 count = hi - lo + 1;
		const uint dst = dstView(level);

		if (level > 1) GroupMemoryBarrierWithGroupSync();

		for (uint y = tid.y; y < count.y; y += 8) {
			for (uint x = tid.x; x < count.x; x += 8) {
				const uint2 local = uint2(x, y);
				const uint2 p = lo + local;
				const uint2 from = p * 2;
				const uint2 extra = uint2(
					p.x == size.x - 1 && (prev_size.x & 1) != 0,
					p.y == size.y - 1 && (prev_size.y & 1) != 0
				);
				const uint2 to = min(from + 1 + extra, prev_size - 1);

				float d;
				if (level == 1) {
					d = reduceSrc(from, to, inv_src_size);
				}
				else {
					const uint2 prev_lo = group << (shift + 1);
					d = reduceLDS(level, from - prev_lo, to - prev_lo);
				}

				bindless_rw_textures[dst][p] = d;
				if ((level & 1) == 0) g_b[y][x] = d;
				else g_a[y][x] = d;
			}
		}
	}
}
