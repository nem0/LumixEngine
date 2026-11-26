#include "cluster_builder_internal.h"

#include "core/crt.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/string.h"

namespace Lumix {

struct Voxel {
	u64 key;
	i32 c[3];
	Vec3 normal;
	Vec3 color;
	float ao;
	float weight;
};

struct VoxelCluster {
	u32 first;		// in the voxels of the level
	u32 count;
	DagBounds bounds;
	int refined;
};

struct VoxelLevel {
	explicit VoxelLevel(IAllocator& allocator) : voxels(allocator), clusters(allocator) {}
	Array<Voxel> voxels;			// the voxels of a cluster are consecutive, sorted by key
	Array<VoxelCluster> clusters;	// ordered by the Morton code of their centers, so the neighbours are close
};

namespace {

constexpr u32 VOXEL_BRICKS_PER_CLUSTER = 64;
static_assert(VOXEL_BRICKS_PER_CLUSTER <= 64, "voxel_bricks.hlsl uses 64-bit masks and 6-bit brick indices");
constexpr u32 VOXEL_GROUP_CLUSTERS = 8;			// clusters of a group that is downsampled to the next level
constexpr float VOXEL_MAX_RESOLUTION = 4000.f;	// the voxels of the finest level along the longest side of the mesh, so that bricks are less than 1024 apart
constexpr float VOXEL_HALF_DIAGONAL = 0.8660254f;	// the distance from the center of a voxel to its corner, in voxels: the error of a voxel level
constexpr float VOXEL_MIN_COVERAGE = 0.25f;		// minimum accumulated surface area / (size * size) for a voxel to survive
constexpr float VOXEL_STALL_THRESHOLD = 0.65f;	// switch a group to voxels if simplification leaves more than this fraction of its triangles

struct KeyIndex {
	u64 key;
	u32 index;
};

u64 spreadBits(u64 x) {
	x &= 0xffff;
	x = (x | (x << 32)) & 0x1f00000000ffffull;
	x = (x | (x << 16)) & 0x1f0000ff0000ffull;
	x = (x | (x << 8)) & 0x100f00f00f00f00full;
	x = (x | (x << 4)) & 0x10c30c30c30c30c3ull;
	x = (x | (x << 2)) & 0x1249249249249249ull;
	return x;
}

u64 mortonKey(u32 x, u32 y, u32 z) { return spreadBits(x) | (spreadBits(y) << 1) | (spreadBits(z) << 2); }

u64 voxelKey(i32 x, i32 y, i32 z) {
	const u64 brick = mortonKey(u32(x >> 2), u32(y >> 2), u32(z >> 2));
	return (brick << 6) | u32((x & 3) + (y & 3) * 4 + (z & 3) * 16);
}

// LSD radix sort of the 54 bits that the keys have, stable
template <typename T>
void radixSort(Array<T>& items, Array<T>& tmp) {
	tmp.resize(items.size());
	for (u32 pass = 0; pass < 6; ++pass) {
		const u32 shift = pass * 9;
		u32 counts[513] = {};
		for (const T& i : items) ++counts[((i.key >> shift) & 511) + 1];
		for (u32 i = 0; i < 512; ++i) counts[i + 1] += counts[i];
		for (const T& i : items) tmp[counts[(i.key >> shift) & 511]++] = i;
		items.swap(tmp);
	}
}

void sortAndMergeVoxels(Array<Voxel>& voxels, Array<Voxel>& tmp) {
	if (voxels.empty()) return;
	radixSort(voxels, tmp);
	u32 out = 0;
	for (u32 i = 0; i < (u32)voxels.size(); ++i) {
		if (out > 0 && voxels[out - 1].key == voxels[i].key) {
			Voxel& v = voxels[out - 1];
			v.normal += voxels[i].normal;
			v.color += voxels[i].color;
			v.ao += voxels[i].ao;
			v.weight += voxels[i].weight;
		}
		else {
			voxels[out++] = voxels[i];
		}
	}
	voxels.resize(out);
}

struct VoxelSourceAttributes {
	const Vec3* positions;
	const Vec3* normals;	// can be null
	const u32* colors;		// can be null
	const u32* ao;			// can be null
};

void clampVoxelWeight(Voxel& v) {
	if (v.weight > 1.f) {
		const float s = 1.f / v.weight;
		v.normal = v.normal * s;
		v.color = v.color * s;
		v.ao *= s;
		v.weight = 1.f;
	}
}

void filterVoxelsByCoverage(Array<Voxel>& voxels, float min_coverage) {
	if (voxels.empty()) return;
	u32 best = 0;
	u32 out = 0;
	for (u32 i = 0; i < (u32)voxels.size(); ++i) {
		if (voxels[i].weight > voxels[best].weight) best = i;
		if (voxels[i].weight >= min_coverage) {
			Voxel v = voxels[i];
			clampVoxelWeight(v);
			voxels[out++] = v;
		}
	}
	if (out == 0) {
		Voxel v = voxels[best];
		clampVoxelWeight(v);
		voxels[0] = v;
		out = 1;
	}
	voxels.resize(out);
}

void voxelizeTriangles(Array<Voxel>& out, const VoxelSourceAttributes& src, const u32* indices, u32 index_count, const Vec3& origin, float size, i32 max_coord) {
	const float inv_size = 1.f / size;
	const float inv_face_area = inv_size * inv_size;
	for (u32 t = 0; t + 2 < index_count; t += 3) {
		const u32 i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
		const Vec3 p0 = src.positions[i0], p1 = src.positions[i1], p2 = src.positions[i2];
		const Vec3 e1 = p1 - p0, e2 = p2 - p0;
		const Vec3 cross_n = cross(e1, e2);
		const float area = 0.5f * length(cross_n);
		if (area <= 1e-12f) continue;

		Vec3 normal = src.normals ? src.normals[i0] + src.normals[i1] + src.normals[i2] : cross_n;
		if (dot(normal, normal) < 1e-20f) normal = cross_n;
		if (dot(normal, normal) < 1e-20f) normal = Vec3(0, 1, 0);
		normal = normalize(normal);

		Vec3 color(1, 1, 1);
		float alpha_scale = 1.f;
		if (src.colors) {
			color = Vec3(0, 0, 0);
			float a_sum = 0.f;
			const u32 tri[3] = { i0, i1, i2 };
			for (u32 i : tri) {
				const u32 c = src.colors[i];
				color += Vec3(float(c & 0xff), float((c >> 8) & 0xff), float((c >> 16) & 0xff)) * (1.f / 255.f);
				a_sum += float(c >> 24) * (1.f / 255.f);
			}
			color = color * (1.f / 3.f);
			alpha_scale = clamp(a_sum * (1.f / 3.f), 0.1f, 1.f);
		}
		float ao = 1.f;
		if (src.ao) ao = float((src.ao[i0] & 0xff) + (src.ao[i1] & 0xff) + (src.ao[i2] & 0xff)) * (1.f / (3.f * 255.f));

		const float max_edge = maximum(length(e1), maximum(length(e2), length(p2 - p1)));
		const u32 n = clamp((u32)ceilf(max_edge * (2.f * inv_size)), 1u, 64u);
		const float inv_n = 1.f / float(n);
		const float w = (area * alpha_scale * inv_face_area) / float(n * n);
		const Vec3 wn = normal * w;
		const Vec3 wc = color * w;
		const float wao = ao * w;

		auto emit_sample = [&](float u, float v_bary) {
			const Vec3 p = p0 + e1 * u + e2 * v_bary;
			const i32 x = clamp((i32)floorf((p.x - origin.x) * inv_size), 0, max_coord);
			const i32 y = clamp((i32)floorf((p.y - origin.y) * inv_size), 0, max_coord);
			const i32 z = clamp((i32)floorf((p.z - origin.z) * inv_size), 0, max_coord);
			Voxel v;
			v.key = voxelKey(x, y, z);
			v.c[0] = x; v.c[1] = y; v.c[2] = z;
			v.normal = wn;
			v.color = wc;
			v.ao = wao;
			v.weight = w;
			out.push(v);
		};
		for (u32 i = 0; i < n; ++i) {
			for (u32 j = 0; j < n - i; ++j) {
				emit_sample((float(i) + (1.f / 3.f)) * inv_n, (float(j) + (1.f / 3.f)) * inv_n);
				if (i + j + 1 < n) {
					emit_sample((float(i) + (2.f / 3.f)) * inv_n, (float(j) + (2.f / 3.f)) * inv_n);
				}
			}
		}
	}
}

void downsampleVoxels(Array<Voxel>& out, const Voxel* voxels, u32 count, float scale) {
	for (u32 i = 0; i < count; ++i) {
		Voxel v = voxels[i];
		v.c[0] >>= 1; v.c[1] >>= 1; v.c[2] >>= 1;
		v.key = voxelKey(v.c[0], v.c[1], v.c[2]);
		v.normal = v.normal * scale;
		v.color = v.color * scale;
		v.ao *= scale;
		v.weight *= scale;
		out.push(v);
	}
}

void appendVoxelClusters(VoxelLevel& level, const Array<Voxel>& voxels, int refined, const Vec3& origin, float size, float error) {
	u32 begin = 0;
	const u32 count = (u32)voxels.size();
	while (begin < count) {
		u32 end = begin;
		u32 bricks = 0;
		u64 brick = ~0ull;
		while (end < count) {
			const u64 b = voxels[end].key >> 6;
			if (b != brick) {
				if (bricks == VOXEL_BRICKS_PER_CLUSTER) break;
				++bricks;
				brick = b;
			}
			++end;
		}
		VoxelCluster cluster;
		cluster.first = (u32)level.voxels.size();
		cluster.count = end - begin;
		cluster.refined = refined;
		i32 mn[3] = { 0x7fffFFFF, 0x7fffFFFF, 0x7fffFFFF };
		i32 mx[3] = { -1, -1, -1 };
		for (u32 i = begin; i < end; ++i) {
			level.voxels.push(voxels[i]);
			for (u32 a = 0; a < 3; ++a) {
				mn[a] = minimum(mn[a], voxels[i].c[a]);
				mx[a] = maximum(mx[a], voxels[i].c[a]);
			}
		}
		const Vec3 lo = origin + Vec3((float)mn[0], (float)mn[1], (float)mn[2]) * size;
		const Vec3 hi = origin + Vec3(float(mx[0] + 1), float(mx[1] + 1), float(mx[2] + 1)) * size;
		cluster.bounds.center = (lo + hi) * 0.5f;
		cluster.bounds.radius = length(hi - lo) * 0.5f;
		cluster.bounds.error = error;
		level.clusters.push(cluster);
		begin = end;
	}
}

void sortVoxelLevel(VoxelLevel& level, VoxelLevel& tmp, const Vec3& origin, float size, IAllocator& allocator) {
	Array<KeyIndex> order(allocator), order_tmp(allocator);
	order.resize((u32)level.clusters.size());
	for (u32 i = 0; i < (u32)level.clusters.size(); ++i) {
		const Vec3 p = (level.clusters[i].bounds.center - origin) * (1.f / size);
		order[i].key = mortonKey((u32)clamp(p.x, 0.f, 65535.f), (u32)clamp(p.y, 0.f, 65535.f), (u32)clamp(p.z, 0.f, 65535.f));
		order[i].index = i;
	}
	radixSort(order, order_tmp);
	tmp.voxels.clear();
	tmp.clusters.clear();
	tmp.voxels.reserve((u32)level.voxels.size());
	for (const KeyIndex& o : order) {
		VoxelCluster c = level.clusters[o.index];
		const u32 first = (u32)tmp.voxels.size();
		for (u32 i = 0; i < c.count; ++i) tmp.voxels.push(level.voxels[c.first + i]);
		c.first = first;
		tmp.clusters.push(c);
	}
	level.voxels.swap(tmp.voxels);
	level.clusters.swap(tmp.clusters);
}

u8 voxelByte(float v) { return (u8)clamp(v * 255.f + 0.5f, 0.f, 255.f); }

u32 packVoxelCluster(MeshClusters& out, const Voxel* voxels, u32 count, const Vec3& origin, float size) {
	MeshVoxelCluster rec;
	i32 mn[3] = { 0x7fffFFFF, 0x7fffFFFF, 0x7fffFFFF };
	for (u32 i = 0; i < count; ++i) {
		for (u32 a = 0; a < 3; ++a) mn[a] = minimum(mn[a], voxels[i].c[a] >> 2);
	}
	rec.first_word = (u32)out.voxel_words.size();
	rec.base[0] = mn[0] * 4; rec.base[1] = mn[1] * 4; rec.base[2] = mn[2] * 4;
	rec.size = size;
	rec.origin = origin;
	rec.brick_count = 0;

	u32 i = 0;
	while (i < count) {
		const u64 brick = voxels[i].key >> 6;
		u64 mask = 0;
		u32 j = i;
		for (; j < count && (voxels[j].key >> 6) == brick; ++j) mask |= 1ull << (voxels[j].key & 63);
		const i32 bx = (voxels[i].c[0] >> 2) - mn[0], by = (voxels[i].c[1] >> 2) - mn[1], bz = (voxels[i].c[2] >> 2) - mn[2];
		ASSERT(bx >= 0 && by >= 0 && bz >= 0 && (u32)bx <= MeshClusters::MAX_VOXEL_BRICK_COORD && (u32)by <= MeshClusters::MAX_VOXEL_BRICK_COORD && (u32)bz <= MeshClusters::MAX_VOXEL_BRICK_COORD);
		out.voxel_words.push((u32)mask);
		out.voxel_words.push((u32)(mask >> 32));
		out.voxel_words.push(u32(bx) | (u32(by) << 10) | (u32(bz) << 20));
		out.voxel_words.push(i);
		++rec.brick_count;
		i = j;
	}
	for (u32 k = 0; k < count; ++k) {
		const Voxel& v = voxels[k];
		Vec3 n = v.normal;
		n = dot(n, n) > 1e-12f ? normalize(n) : Vec3(0, 1, 0);
		const float inv = 1.f / maximum(v.weight, 1e-6f);
		out.voxel_words.push(packClusterFrame(n, nullptr));
		out.voxel_words.push(u32(voxelByte(v.color.x * inv)) | (u32(voxelByte(v.color.y * inv)) << 8) | (u32(voxelByte(v.color.z * inv)) << 16) | (u32(voxelByte(v.ao * inv)) << 24));
	}
	rec.word_count = (u32)out.voxel_words.size() - rec.first_word;
	out.voxel_clusters.push(rec);
	return (u32)out.voxel_clusters.size() - 1;
}

bool hasMoreThanOneBrick(const VoxelLevel& l) {
	if (l.clusters.empty()) return false;
	const VoxelCluster& c = l.clusters[0];
	const u64 brick = l.voxels[c.first].key >> 6;
	for (u32 i = 1; i < c.count; ++i) {
		if ((l.voxels[c.first + i].key >> 6) != brick) return true;
	}
	return false;
}

int emitVoxelGroup(ClusterBuildContext& ctx, const VoxelLevel& voxel_level, u32 first, u32 count, const DagBounds& simplified, int group_depth, const Vec3& origin, float size) {
	beginClusterGroup(ctx, count, group_depth);
	for (u32 i = 0; i < count; ++i) {
		const VoxelCluster& c = voxel_level.clusters[first + i];
		MeshClusterGPU cluster;
		memset(&cluster, 0, sizeof(cluster));
		cluster.first_index = packVoxelCluster(ctx.out, voxel_level.voxels.begin() + c.first, c.count, origin, size);
		cluster.index_count = 0;
		cluster.group = ctx.group_id_counter;
		cluster.refined_group = (u32)c.refined;
		cluster.center = c.bounds.center;
		cluster.radius = c.bounds.radius;
		const DagBounds lod = ctx.group_bounds[c.refined];
		cluster.lod_center = lod.center;
		cluster.lod_radius = lod.radius;
		cluster.error = lod.error;
		cluster.parent_center = simplified.center;
		cluster.parent_radius = simplified.radius;
		cluster.parent_error = simplified.error;
		ctx.out.clusters.push(cluster);
	}
	ctx.group_bounds.push(simplified);
	return (int)ctx.group_id_counter++;
}

} // anonymous namespace

ClusterVoxelBuilder::ClusterVoxelBuilder(IAllocator& allocator)
	: allocator(allocator)
	, staged_levels(allocator)
{}

ClusterVoxelBuilder::~ClusterVoxelBuilder() {
	for (VoxelLevel* lvl : staged_levels) LUMIX_DELETE(allocator, lvl);
}

float ClusterVoxelBuilder::computeAvgEdge(const Vec3* pos, const u32* indices, u32 index_count) const {
	double sum = 0.0;
	u32 edges = 0;
	for (u32 t = 0; t + 2 < index_count; t += 3) {
		const Vec3 p0 = pos[indices[t]], p1 = pos[indices[t + 1]], p2 = pos[indices[t + 2]];
		sum += length(p1 - p0) + length(p2 - p1) + length(p0 - p2);
		edges += 3;
	}
	return edges > 0 ? float(sum / double(edges)) : 0.f;
}

bool ClusterVoxelBuilder::init(const ClusterVoxelSource* src, const Vec3* pos, const Vec3* norm, u32 vertex_count,
	const Array<DagCluster>& leaf_clusters, const Array<u32>& cluster_indices, bool dilate_borders)
{
	source = src;
	positions = pos;
	normals = norm;
	enabled = false;
	if (!src) return false;

	min_coverage = clamp(src->min_coverage, 0.f, 1.f);
	downsample_scale = clamp(src->downsample_scale, 0.125f, 1.f);
	stall_threshold = clamp(src->stall_threshold, SIMPLIFY_RATIO, 1.f);
	max_resolution = clamp(float(src->max_resolution), 64.f, VOXEL_MAX_RESOLUTION);

	Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (u32 i = 0; i < vertex_count; ++i) {
		mn = minimum(mn, pos[i]);
		mx = maximum(mx, pos[i]);
	}
	const Vec3 ext = mx - mn;
	const float mesh_diagonal = length(ext);
	const float max_ext = maximum(ext.x, maximum(ext.y, ext.z));
	// Leave margin around the initial AABB when border dilation is enabled so dilated vertices do not clamp to the grid walls.
	const float pad = dilate_borders ? max_ext * 0.05f : 0.f;
	origin = mn - Vec3(pad);
	extent = max_ext + pad * 2.f;
	if (src->error > 0.f && mesh_diagonal > 0.f) {
		error_threshold = src->error * mesh_diagonal;
	}
	if (extent > 0.f) {
		float min_avg_edge = FLT_MAX;
		for (const DagCluster& c : leaf_clusters) {
			const float e = computeAvgEdge(pos, cluster_indices.data() + c.index_offset, c.index_count);
			if (e > 0.f) min_avg_edge = minimum(min_avg_edge, e);
		}
		if (min_avg_edge == FLT_MAX) min_avg_edge = 0.f;
		base_size = maximum(min_avg_edge * 0.5f, extent / max_resolution);
	}
	enabled = extent > 0.f && base_size > 0.f;
	return enabled;
}

float ClusterVoxelBuilder::levelError(u32 k) const {
	return levelSize(k) * VOXEL_HALF_DIAGONAL;
}

u32 ClusterVoxelBuilder::chooseLevel(float min_error, float group_avg_edge) const {
	const float edge_error = group_avg_edge * 0.5f * VOXEL_HALF_DIAGONAL;
	const float target_error = maximum(min_error, edge_error);
	u32 k = 0;
	while (k < 20 && levelError(k) < target_error) ++k;
	if (k > 0 && levelError(k - 1) >= min_error && target_error < levelError(k - 1) * 1.41421356f) {
		--k;
	}
	return k;
}

bool ClusterVoxelBuilder::shouldSwitchGroup(u32 merged_count, u32 simplified_count, float prev_error, float simplify_error, float group_avg_edge) const {
	const bool stalled = simplified_count > merged_count * stall_threshold || simplified_count >= merged_count;
	const float next_error = maximum(prev_error, simplify_error);
	const float err_limit = source->error > 0.f ? error_threshold : group_avg_edge * 0.5f;
	return stalled || next_error >= err_limit;
}

void ClusterVoxelBuilder::voxelizeGroup(ClusterBuildContext& ctx, const u32* tri_indices, u32 tri_index_count, int refined, u32 k) {
	const float size = levelSize(k);
	const float error = levelError(k);
	const i32 max_coord = (i32)(extent / size) + 1;
	const VoxelSourceAttributes src_attrs = { ctx.positions, normals, source->colors, source->ao };
	Array<Voxel> vox_temp(allocator), vox_sort(allocator);
	voxelizeTriangles(vox_temp, src_attrs, tri_indices, tri_index_count, origin, size, max_coord);
	sortAndMergeVoxels(vox_temp, vox_sort);
	filterVoxelsByCoverage(vox_temp, min_coverage);
	if (!vox_temp.empty()) {
		while ((u32)staged_levels.size() <= k) staged_levels.push(LUMIX_NEW(allocator, VoxelLevel)(allocator));
		appendVoxelClusters(*staged_levels[k], vox_temp, refined, origin, size, error);
	}
	else {
		ctx.group_bounds[refined].error = FLT_MAX;
		const MeshGroupGPU& g = ctx.out.groups[refined];
		for (u32 ci = 0; ci < g.cluster_count; ++ci) {
			ctx.out.clusters[g.first_cluster + ci].parent_error = FLT_MAX;
		}
	}
}

void ClusterVoxelBuilder::buildStagedLevels(ClusterBuildContext& ctx, int first_depth) {
	PROFILE_BLOCK("clod voxels");
	if (staged_levels.empty()) return;
	u32 first_k = (u32)staged_levels.size();
	u32 last_k = 0;
	for (u32 k = 0; k < (u32)staged_levels.size(); ++k) {
		if (!staged_levels[k]->clusters.empty()) {
			first_k = minimum(first_k, k);
			last_k = maximum(last_k, k);
		}
	}
	if (first_k >= (u32)staged_levels.size()) return;

	VoxelLevel cur(allocator), next(allocator), tmp_level(allocator);
	Array<Voxel> vox_temp(allocator), vox_sort(allocator);
	int voxel_depth = first_depth;
	for (u32 k = first_k; ; ++k) {
		if (k < (u32)staged_levels.size() && !staged_levels[k]->clusters.empty()) {
			const VoxelLevel& src_lvl = *staged_levels[k];
			for (const VoxelCluster& sc : src_lvl.clusters) {
				VoxelCluster c = sc;
				c.first = (u32)cur.voxels.size();
				for (u32 vi = 0; vi < sc.count; ++vi) cur.voxels.push(src_lvl.voxels[sc.first + vi]);
				cur.clusters.push(c);
			}
		}
		if (cur.clusters.empty()) {
			if (k >= last_k) break;
			continue;
		}
		const float size = levelSize(k);
		sortVoxelLevel(cur, tmp_level, origin, size, allocator);
		if (k >= last_k && cur.clusters.size() <= 1 && !hasMoreThanOneBrick(cur)) {
			DagBounds bounds = cur.clusters[0].bounds;
			bounds.error = FLT_MAX;
			emitVoxelGroup(ctx, cur, 0, 1, bounds, voxel_depth, origin, size);
			++voxel_depth;
			break;
		}
		const float next_size = levelSize(k + 1);
		const float next_error = levelError(k + 1);
		next.voxels.clear();
		next.clusters.clear();
		for (u32 first = 0; first < (u32)cur.clusters.size(); first += VOXEL_GROUP_CLUSTERS) {
			const u32 count = minimum(VOXEL_GROUP_CLUSTERS, (u32)cur.clusters.size() - first);
			const VoxelCluster* group = &cur.clusters[first];
			DagBounds child_lods[VOXEL_GROUP_CLUSTERS];
			for (u32 ci = 0; ci < count; ++ci) child_lods[ci] = ctx.group_bounds[group[ci].refined];
			DagBounds bounds = dagBoundsMerge(child_lods, count, sizeof(DagBounds));
			bounds.error = next_error;
			const int id = emitVoxelGroup(ctx, cur, first, count, bounds, voxel_depth, origin, size);
			vox_temp.clear();
			const VoxelCluster& last = group[count - 1];
			downsampleVoxels(vox_temp, cur.voxels.begin() + group[0].first, last.first + last.count - group[0].first, downsample_scale);
			sortAndMergeVoxels(vox_temp, vox_sort);
			filterVoxelsByCoverage(vox_temp, min_coverage);
			appendVoxelClusters(next, vox_temp, id, origin, next_size, next_error);
		}
		cur.voxels.swap(next.voxels);
		cur.clusters.swap(next.clusters);
		++voxel_depth;
	}
	u32 bricks = 0;
	for (const MeshVoxelCluster& v : ctx.out.voxel_clusters) bricks += v.brick_count;
	logInfo("Voxel levels (per-group): base size ", base_size, ", first_k ", first_k, " (size ", levelSize(first_k), "), ", voxel_depth - first_depth, " levels, ", (u32)ctx.out.voxel_clusters.size()
		, " voxel clusters, ", bricks, " bricks, ", (u32)ctx.out.voxel_words.size() * 4, " bytes, triangle clusters below: ", (u32)ctx.out.clusters.size() - (u32)ctx.out.voxel_clusters.size());
}

} // namespace Lumix
