#include "engine/shaders/common.hlsli"

// Builds the pyramid of depths for the occlusion culling of clusters (nanite.hlsl, see PipelineImpl::buildHZB). The depth is reversed (the farthest
// is the smallest), so a texel of a coarser level is the minimum of the texels under it: the farthest depth of its area, it is what a hidden object has to be
// behind. The pyramid has no level 0 (the size of the depth buffer): it starts at level 1, which is made from the depth buffer, so the full resolution copy is not made
// or read. The levels are R32F, a few levels are made in one dispatch (up to 5 from the depth buffer, then up to 4 from the last level that was made), the source of
// the later dispatches is read through a UAV view of a level, so the texture stays in one state.
//   u_mode 0: the source is the depth buffer (u_src is a bindless texture)
//   u_mode 1: the source is the level `n` (u_src is the rw view of it)
// Sizes of the levels are rounded down, the last texel of a row or column covers the extra source texel of an odd size.

cbuffer Data : register(b4) {
	uint u_mode;
	uint u_src;
	uint u_dst;			// the rw view of the first level that is made (n + 1)
	uint u_levels;		// how many levels are made (1-5)
	uint2 u_src_size;
	uint2 u_dst_size;	// the size of the last level that is made
	uint u_dst1;		// the rw views of the levels n + 2 ... n + 5
	uint u_dst2;
	uint u_dst3;
	uint u_dst4;
};

float loadSrc(uint2 p) {
	return u_mode == 0 ? bindless_textures[u_src][p].r : bindless_rw_textures[u_src][p].r;
}

uint dstView(uint i) {
	return i == 1 ? u_dst : (i == 2 ? u_dst1 : (i == 3 ? u_dst2 : (i == 4 ? u_dst3 : u_dst4)));
}

// A group of 64 threads makes one texel of the last level (n + u_levels) and the texels of the levels between that are under it. The texel `p` of level `n + i` covers
// the texels [p << i, ((p + 1) << i) - 1] of level `n`, the last one of a row or column all the rest: this is the same as chaining the reduction of 2 x 2 texels
// (3 for the last texel of an odd size) level by level.
// A group that is not the last one of a row or column has a block of (1 << u_levels) ^ 2 texels of level `n` and reduces it in the shared memory, reading each texel once.
// The last groups also own the rest of the row or column (an odd size, or the remainder of the division), they are rare and make their texels level by level from
// the global memory (multiLevel).
groupshared float g_a[16][16];
groupshared float g_b[8][8];

// all the texels of a group (its texel `k` of the last level and the texels of the other levels under it), shared by the 64 threads of the group (`thread` is the index
// of the thread in the group), each texel is made by one thread
void multiLevel(uint2 k, uint thread) {
	for (uint i = 1; i <= u_levels; ++i) {
		const uint shift = u_levels - i;
		const uint2 size_i = max(u_src_size >> i, 1);
		const uint2 size_prev = max(u_src_size >> (i - 1), 1);
		const uint2 lo = k << shift;
		uint2 hi = min(((k + 1) << shift) - 1, size_i - 1);
		if (k.x == u_dst_size.x - 1) hi.x = size_i.x - 1;
		if (k.y == u_dst_size.y - 1) hi.y = size_i.y - 1;
		const uint dst = dstView(i);
		const uint width = hi.x - lo.x + 1;
		const uint count = width * (hi.y - lo.y + 1);
		// the level `i - 1` was made by this group (the texels under the ones of this level are all in its range)
		if (i > 1) AllMemoryBarrierWithGroupSync();
		for (uint idx = thread; idx < count; idx += 64) {
			const uint2 p = lo + uint2(idx % width, idx / width);
			const uint2 from = p * 2;
			// 2 texels, 3 for the last one of an odd size
			uint2 to = from + 1;
			if (p.x == size_i.x - 1 && (size_prev.x & 1) != 0) to.x = from.x + 2;
			if (p.y == size_i.y - 1 && (size_prev.y & 1) != 0) to.y = from.y + 2;
			to = min(to, size_prev - 1);
			float d = 1e30;
			for (uint sy = from.y; sy <= to.y; ++sy) {
				for (uint sx = from.x; sx <= to.x; ++sx) {
					d = min(d, i == 1 ? loadSrc(uint2(sx, sy)) : bindless_rw_textures[dstView(i - 1)][uint2(sx, sy)].r);
				}
			}
			bindless_rw_textures[dst][p] = d;
		}
	}
}

[numthreads(8, 8, 1)]
void main(uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID) {
	const uint2 t = local_id.xy;
	// a group per texel of the last level
	[branch]
	if (group_id.x + 1 >= u_dst_size.x || group_id.y + 1 >= u_dst_size.y) {
		multiLevel(group_id.xy, t.y * 8 + t.x);
		return;
	}

	// level n + 1: 2 x 2 texels of the block for each of its texels, shared by the threads
	const uint half_block = (1u << u_levels) >> 1;
	const float2 inv_src_size = 1.0 / float2(u_src_size);
	for (uint yy = t.y; yy < half_block; yy += 8) {
		for (uint xx = t.x; xx < half_block; xx += 8) {
			const uint2 from = (group_id.xy << u_levels) + uint2(xx, yy) * 2;
			float d;
			if (u_mode == 0) {
				const float4 g = bindless_textures[u_src].GatherRed(LinearSamplerClamp, (float2(from) + 1.0) * inv_src_size);
				d = min(min(g.x, g.y), min(g.z, g.w));
			}
			else {
				d = min(loadSrc(from), loadSrc(from + uint2(1, 0)));
				d = min(d, min(loadSrc(from + uint2(0, 1)), loadSrc(from + uint2(1, 1))));
			}
			bindless_rw_textures[u_dst][(group_id.xy << (u_levels - 1)) + uint2(xx, yy)] = d;
			g_a[yy][xx] = d;
		}
	}
	// the levels n + 2 ... n + u_levels, from the shared memory (the arrays alternate, so that a level is not overwritten while the threads read it)
	for (uint i = 2; i <= u_levels; ++i) {
		GroupMemoryBarrierWithGroupSync();
		if (all(t < ((1u << u_levels) >> i))) {
			const uint2 f = t * 2;
			float d;
			if ((i & 1) == 0) d = min(min(g_a[f.y][f.x], g_a[f.y][f.x + 1]), min(g_a[f.y + 1][f.x], g_a[f.y + 1][f.x + 1]));
			else d = min(min(g_b[f.y][f.x], g_b[f.y][f.x + 1]), min(g_b[f.y + 1][f.x], g_b[f.y + 1][f.x + 1]));
			bindless_rw_textures[dstView(i)][(group_id.xy << (u_levels - i)) + t] = d;
			if ((i & 1) == 0) g_b[t.y][t.x] = d;
			else g_a[t.y][t.x] = d;
		}
	}
}
