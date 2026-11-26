#ifndef HZB_COMMON_HLSLI
#define HZB_COMMON_HLSLI

// Shared HZB sphere occlusion culling helpers used by nanite.hlsl and voxel_bricks.hlsl (expects common.hlsli included first).
// The pyramid has no level 0 (hzb.hlsl): its first level is half of the size of the depth buffer, so clusters/bricks whose rectangle
// is 4 pixels or smaller are tested at level 1 (a texel is 2 x 2 pixels).
#define HZB_DETAIL_LEVELS 2
#define HZB_MIN_MIP 1

// True if the projected sphere (with precomputed clip position and row_scale = float2(length(vp[0].xyz), length(vp[1].xyz)) * radius)
// is certainly behind what `hzb_tex` has: every pixel it can cover has a nearer depth than the nearest point of the sphere.
bool isOccludedClip(float4 clip, float radius, float2 row_scale, uint hzb_tex, float2 hzb_size, uint hzb_mips) {
	const float w_near = clip.w - radius;
	if (w_near <= 1e-4) return false; // may touch the near plane
	const float w_far = clip.w + radius;
	const float2 n_max = clip.xy + row_scale;
	const float2 n_min = clip.xy - row_scale;
	const float2 ndc_max = n_max / float2(n_max.x > 0 ? w_near : w_far, n_max.y > 0 ? w_near : w_far);
	const float2 ndc_min = n_min / float2(n_min.x < 0 ? w_near : w_far, n_min.y < 0 ? w_near : w_far);
	// reversed depth: larger is nearer
	const float nearest = clip.z / w_near;
	const float2 uv_a = toScreenUV(ndc_min * 0.5 + 0.5);
	const float2 uv_b = toScreenUV(ndc_max * 0.5 + 0.5);
	const float2 mn = saturate(min(uv_a, uv_b));
	const float2 mx = saturate(max(uv_a, uv_b));

	// Choose the level where the rectangle covers 2 to 4 texels in a row (at most 5), avoiding coarse-mip over-conservatism.
	const float2 size_px = (mx - mn) * hzb_size;
	const uint mip = min((uint)max(ceil(log2(max(max(size_px.x, size_px.y), 1.0))) - HZB_DETAIL_LEVELS, HZB_MIN_MIP), hzb_mips - 1);
	const int2 mip_size = max(int2(hzb_size) >> mip, 1);
	// A texel covers pixels [i << mip, (i + 1) << mip), so derive texel coords from pixel coords directly.
	const int2 t0 = min(int2(mn * hzb_size) >> mip, mip_size - 1);
	const int2 t1 = min(int2(mx * hzb_size) >> mip, mip_size - 1);
	Texture2D<float4> hzb = bindless_textures[hzb_tex];
	float farthest = 1e30;
	for (int y = t0.y; y <= t1.y; ++y) {
		for (int x = t0.x; x <= t1.x; ++x) {
			farthest = min(farthest, hzb.Load(int3(x, y, mip - HZB_MIN_MIP)).r);
		}
	}
	// Margin for rounding and 24-bit depth buffer precision
	return farthest > nearest * 1.0005 + 1e-6;
}

struct HZBUniforms {
	float4 prev_vp[4];
	float4 cur_vp[4];
	float4 prev_delta;
	uint mode;
	uint prev;
	uint cur;
	uint mips;
	float2 size;
	uint2 pad;
};

bool isOccludedSphere(float3 center, float radius, float4 vp[4], uint hzb_tex, float2 hzb_size, uint hzb_mips) {
	const float4 p = float4(center, 1);
	const float4 clip = float4(dot(vp[0], p), dot(vp[1], p), dot(vp[2], p), dot(vp[3], p));
	const float2 row_scale = float2(length(vp[0].xyz), length(vp[1].xyz)) * radius;
	return isOccludedClip(clip, radius, row_scale, hzb_tex, hzb_size, hzb_mips);
}

bool isOccludedHZB(HZBUniforms hzb, float3 center, float radius, bool previous) {
	if (previous) return isOccludedSphere(center + hzb.prev_delta.xyz, radius, hzb.prev_vp, hzb.prev, hzb.size, hzb.mips);
	return isOccludedSphere(center, radius, hzb.cur_vp, hzb.cur, hzb.size, hzb.mips);
}

#endif
