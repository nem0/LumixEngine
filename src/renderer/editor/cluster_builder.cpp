#include "cluster_builder_internal.h"

#include "core/crt.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/string.h"

#include "meshoptimizer/meshoptimizer.h"

namespace Lumix {

namespace {

// The sizes of a cluster. The indices of a cluster are bytes, so it has at most 256 vertices.
constexpr u32 MAX_CLUSTER_VERTICES = 256;
constexpr u32 MAX_CLUSTER_TRIANGLES = 128;
constexpr u32 MIN_CLUSTER_TRIANGLES = MAX_CLUSTER_TRIANGLES / 3;
constexpr u32 PARTITION_SIZE = 16;
constexpr float CLUSTER_SPLIT_FACTOR = 2.0f;
constexpr int OPTIMIZE_CLUSTERS_LEVEL = 1;
constexpr float SIMPLIFY_RATIO = 0.5f;
constexpr float SIMPLIFY_THRESHOLD = 0.85f;
constexpr float SIMPLIFY_ERROR_FACTOR_SLOPPY = 2.0f;
constexpr float UV_STEP = 1.f / 16384.f;
constexpr u32 INVALID_INDEX = 0xffFFffFF;

// Cluster LOD DAG builder, based on meshoptimizer's clusterlod (MIT License, Copyright (c) 2016-2026 Arseny Kapoulkine).

template <typename T>
void appendSlice(Array<T>& dst, const T* src, u32 count) {
	if (count == 0) return;
	const u32 offset = dst.size();
	const u32 new_size = offset + count;
	if (new_size > dst.capacity()) {
		dst.reserve(maximum(dst.capacity() < 4 ? 4u : dst.capacity() + dst.capacity() / 2, new_size));
	}
	dst.resize(new_size);
	memcpy(dst.data() + offset, src, count * sizeof(T));
}

DagBounds dagBoundsCompute(const Vec3* positions, u32 vertex_count, const u32* indices, u32 index_count, float error) {
	const meshopt_Bounds b = meshopt_computeClusterBounds(indices, index_count, &positions[0].x, vertex_count, sizeof(Vec3));
	return { Vec3(b.center[0], b.center[1], b.center[2]), b.radius, error };
}

void clusterize(Array<DagCluster>& clusters, Array<u32>& cluster_indices, const Vec3* positions, u32 vertex_count, const u32* indices, u32 index_count, int refined = -1, u32 material = 0) {
	const size_t max_meshlets = meshopt_buildMeshletsBound(index_count, MAX_CLUSTER_VERTICES, MIN_CLUSTER_TRIANGLES);
	const size_t buffer_size = max_meshlets * sizeof(meshopt_Meshlet) + index_count * (sizeof(u32) + sizeof(u8));

	alignas(meshopt_Meshlet) char buffer_stack[32768];
	Array<char> buffer_heap(clusters.getAllocator());
	char* buffer = buffer_stack;
	if (buffer_size > sizeof(buffer_stack)) {
		buffer_heap.resize(u32(buffer_size));
		buffer = buffer_heap.data();
	}

	auto* meshlets = reinterpret_cast<meshopt_Meshlet*>(buffer);
	auto* meshlet_vertices = reinterpret_cast<u32*>(meshlets + max_meshlets);
	auto* meshlet_triangles = reinterpret_cast<u8*>(meshlet_vertices + index_count);

	const u32 meshlet_count = u32(meshopt_buildMeshletsFlex(
		meshlets, meshlet_vertices, meshlet_triangles, indices, index_count,
		&positions[0].x, vertex_count, sizeof(Vec3),
		MAX_CLUSTER_VERTICES, MIN_CLUSTER_TRIANGLES, MAX_CLUSTER_TRIANGLES, 0.f, CLUSTER_SPLIT_FACTOR));

	const u32 cluster_offset = clusters.size();
	clusters.resize(cluster_offset + meshlet_count);

	u32 index_offset = cluster_indices.size();
	cluster_indices.resize(index_offset + index_count);

	for (u32 i = 0; i < meshlet_count; ++i) {
		const meshopt_Meshlet& meshlet = meshlets[i];
		DagCluster& cluster = clusters[cluster_offset + i];

		meshopt_optimizeMeshletLevel(&meshlet_vertices[meshlet.vertex_offset], meshlet.vertex_count, &meshlet_triangles[meshlet.triangle_offset], meshlet.triangle_count, OPTIMIZE_CLUSTERS_LEVEL);

		cluster.index_offset = index_offset;
		cluster.index_count = meshlet.triangle_count * 3;
		for (u32 j = 0; j < cluster.index_count; ++j) {
			cluster_indices[index_offset + j] = meshlet_vertices[meshlet.vertex_offset + meshlet_triangles[meshlet.triangle_offset + j]];
		}
		index_offset += cluster.index_count;
		cluster.refined = refined;
		cluster.material = material;
	}
}

struct PartitionGroup {
	u32 offset;
	u32 count;
	bool is_single_root;
};

void partitionMaterialSlice(DagCluster* clusters, u32 cluster_count, u32 base_offset, const Vec3* positions, const Array<u32>& indices, const Array<u32>& remap, Array<PartitionGroup>& out_groups, IAllocator& allocator) {
	if (cluster_count <= 1) {
		if (cluster_count == 1) out_groups.push({ base_offset, 1, true });
		return;
	}
	if (cluster_count <= PARTITION_SIZE) {
		out_groups.push({ base_offset, cluster_count, false });
		return;
	}

	Array<u32> cluster_counts(allocator);
	cluster_counts.resize(cluster_count);
	Array<u32> cluster_indices(allocator);
	cluster_indices.reserve(cluster_count * MAX_CLUSTER_TRIANGLES * 3);

	for (u32 i = 0; i < cluster_count; ++i) {
		const DagCluster& cluster = clusters[i];
		cluster_counts[i] = cluster.index_count;
		for (u32 j = 0; j < cluster.index_count; ++j) {
			cluster_indices.push(remap[indices[cluster.index_offset + j]]);
		}
	}

	Array<u32> cluster_part(allocator);
	cluster_part.resize(cluster_count);
	const u32 partition_count = u32(meshopt_partitionClusters(
		cluster_part.data(), cluster_indices.data(), cluster_indices.size(), cluster_counts.data(), cluster_counts.size(),
		&positions[0].x, remap.size(), sizeof(Vec3), PARTITION_SIZE));

	// sort partitions spatially so neighbouring clusters stay close in the output (buildNodes relies on this)
	Array<Vec3> partition_point(allocator);
	partition_point.resize(partition_count);
	for (u32 i = 0; i < cluster_count; ++i) {
		partition_point[cluster_part[i]] = clusters[i].bounds.center;
	}

	Array<u32> partition_remap(allocator);
	partition_remap.resize(partition_count);
	meshopt_spatialSortRemap(partition_remap.data(), &partition_point[0].x, partition_count, sizeof(Vec3));

	for (u32 i = 0; i < cluster_count; ++i) {
		cluster_part[i] = partition_remap[cluster_part[i]];
	}

	Array<u32> partition_offsets(allocator);
	partition_offsets.resize(partition_count + 1);
	memset(partition_offsets.data(), 0, partition_offsets.byte_size());
	for (u32 i = 0; i < cluster_count; ++i) partition_offsets[cluster_part[i] + 1]++;
	for (u32 i = 1; i <= partition_count; ++i) partition_offsets[i] += partition_offsets[i - 1];

	Array<u32> write(allocator);
	partition_offsets.copyTo(write);
	Array<DagCluster> result(allocator);
	result.resize(cluster_count);
	for (u32 i = 0; i < cluster_count; ++i) {
		result[write[cluster_part[i]]++] = clusters[i];
	}
	memcpy(clusters, result.data(), cluster_count * sizeof(DagCluster));

	for (u32 p = 0; p < partition_count; ++p) {
		const u32 cnt = partition_offsets[p + 1] - partition_offsets[p];
		if (cnt > 0) out_groups.push({ base_offset + partition_offsets[p], cnt, false });
	}
}

Array<PartitionGroup> partitionClusters(Array<DagCluster>& clusters, const Vec3* positions, const Array<u32>& indices, const Array<u32>& remap) {
	IAllocator& allocator = clusters.getAllocator();
	Array<PartitionGroup> groups(allocator);
	const u32 total = clusters.size();
	if (total == 0) return groups;

	// Group clusters by material (preserving relative order within each material) so triangle clusters and simplification groups stay single-material.
	u32 max_mat = 0;
	for (const DagCluster& c : clusters) max_mat = maximum(max_mat, c.material);
	if (max_mat > 0) {
		Array<u32> mat_offsets(allocator);
		mat_offsets.resize(max_mat + 2);
		memset(mat_offsets.data(), 0, mat_offsets.byte_size());
		for (const DagCluster& c : clusters) mat_offsets[c.material + 1]++;
		for (u32 m = 1; m <= max_mat + 1; ++m) mat_offsets[m] += mat_offsets[m - 1];
		Array<u32> write(allocator);
		mat_offsets.copyTo(write);
		Array<DagCluster> sorted(allocator);
		sorted.resize(total);
		for (const DagCluster& c : clusters) sorted[write[c.material]++] = c;
		clusters.swap(sorted);
		for (u32 m = 0; m <= max_mat; ++m) {
			const u32 start = mat_offsets[m];
			const u32 count = mat_offsets[m + 1] - start;
			if (count > 0) partitionMaterialSlice(clusters.data() + start, count, start, positions, indices, remap, groups, allocator);
		}
	}
	else {
		partitionMaterialSlice(clusters.data(), total, 0, positions, indices, remap, groups, allocator);
	}
	return groups;
}

void lockBoundary(Array<u8>& locks, const DagCluster* clusters, const PartitionGroup* groups, u32 group_count, const Array<u32>& cluster_indices, const Array<u32>& remap) {
	// bit 7 is temporary storage indicating the vertex was seen by an earlier group
	for (u8& lock : locks) lock &= ~((1 << 0) | (1 << 7));

	for (u32 i = 0; i < group_count; ++i) {
		const DagCluster* group = clusters + groups[i].offset;
		const u32 group_size = groups[i].count;

		for (u32 j = 0; j < group_size; ++j) {
			const DagCluster& cluster = group[j];
			for (u32 k = 0; k < cluster.index_count; ++k) {
				const u32 r = remap[cluster_indices[cluster.index_offset + k]];
				locks[r] |= locks[r] >> 7;
			}
		}

		for (u32 j = 0; j < group_size; ++j) {
			const DagCluster& cluster = group[j];
			for (u32 k = 0; k < cluster.index_count; ++k) {
				const u32 r = remap[cluster_indices[cluster.index_offset + k]];
				locks[r] |= 1 << 7;
			}
		}
	}

	for (u32 i = 0, c = locks.size(); i < c; ++i) {
		const u32 r = remap[i];
		locks[i] = (locks[r] & 1) | (locks[i] & meshopt_SimplifyVertex_Protect);
	}
}

void simplifyFallback(Array<u32>& lod, const Vec3* positions, u32 vertex_count, const Array<u32>& indices, const Array<u8>& locks, u32 target_count, float* error) {
	struct SloppyVertex {
		Vec3 pos;
		u32 id;
	};

	IAllocator& allocator = lod.getAllocator();
	const u32 index_count = indices.size();
	Array<SloppyVertex> subset(allocator);
	subset.resize(index_count);
	Array<u8> subset_locks(allocator);
	subset_locks.resize(index_count);
	lod.resize(index_count);

	for (u32 i = 0; i < index_count; ++i) {
		const u32 v = indices[i];
		ASSERT(v < vertex_count);
		subset[i] = { positions[v], v };
		subset_locks[i] = locks[v];
		lod[i] = i;
	}

	lod.resize(u32(meshopt_simplifySloppy(lod.data(), lod.data(), lod.size(), &subset[0].pos.x, subset.size(), sizeof(SloppyVertex), subset_locks.data(), target_count, FLT_MAX, error)));
	*error *= meshopt_simplifyScale(&subset[0].pos.x, subset.size(), sizeof(SloppyVertex));

	for (u32& v : lod) v = subset[v].id;
}

void simplifyGroup(Array<u32>& lod, const Vec3* positions, u32 vertex_count, const Vec3* normals, const Array<u32>& indices, const Array<u8>& locks, u32 target_count, bool allow_fallback, float* error) {
	ASSERT(target_count <= u32(indices.size()));
	lod.resize(indices.size());

	static constexpr float attribute_weights[3] = { 0.5f, 0.5f, 0.5f };
	const u32 options = meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute | meshopt_SimplifyErrorClamped | meshopt_SimplifyPermissive;

	lod.resize(u32(meshopt_simplifyWithAttributes(
		lod.data(), indices.data(), indices.size(),
		&positions[0].x, vertex_count, sizeof(Vec3),
		normals ? &normals[0].x : nullptr, normals ? sizeof(Vec3) : 0, normals ? attribute_weights : nullptr, normals ? 3 : 0,
		locks.data(), target_count, FLT_MAX, options, error)));

	if (allow_fallback && u32(lod.size()) > target_count) {
		simplifyFallback(lod, positions, vertex_count, indices, locks, target_count, error);
		*error *= SIMPLIFY_ERROR_FACTOR_SLOPPY;
	}

	// replace degenerate outputs with the first triangle to keep DAG invariants
	if (lod.empty() && !indices.empty()) {
		lod.resize(3);
		memcpy(lod.data(), indices.data(), 3 * sizeof(u32));
	}
}

u64* edgeLookup(Array<u64>& table, u64 key) {
	const u32 mask = table.size() - 1;
	u32 h = u32((key * 0x9E3779B97F4A7C15ull) >> 32) & mask;
	while (table[h] != ~0ull && table[h] != key) h = (h + 1) & mask;
	return &table[h];
}

float boundaryArea(const Vec3* positions, const Array<u32>& indices, const Array<u8>& locks, const Array<u32>& remap, Array<u64>& table) {
	memset(table.data(), -1, table.byte_size());

	for (u32 i = 0, c = indices.size(); i < c; i += 3) {
		for (int e = 0; e < 3; ++e) {
			const u32 a = remap[indices[i + e]];
			const u32 b = remap[indices[i + (e == 2 ? 0 : e + 1)]];
			const u64 id = (u64)a << 32 | b;
			*edgeLookup(table, id) = id;
		}
	}

	float area = 0.f;
	for (u32 i = 0, c = indices.size(); i < c; i += 3) {
		bool border = false;
		for (int e = 0; e < 3; ++e) {
			const u32 a = remap[indices[i + e]];
			const u32 b = remap[indices[i + (e == 2 ? 0 : e + 1)]];
			const u64 id = (u64)b << 32 | a;
			border |= (locks[a] & locks[b] & 1) == 0 && *edgeLookup(table, id) == ~0ull;
		}
		if (!border) continue;

		const Vec3& va = positions[indices[i + 0]];
		const Vec3& vb = positions[indices[i + 1]];
		const Vec3& vc = positions[indices[i + 2]];
		area += length(cross(vb - va, vc - va)) * 0.5f;
	}
	return area;
}

void dilateBorders(Vec3* positions, const Array<u32>& merged, const Array<u32>& simplified, const Array<u8>& locks, const Array<u32>& remap, DagBounds& bounds, Array<Vec4>& dilate_offsets, Array<u64>& edge_table) {
	u32 table_size = 16;
	while (table_size < u32(merged.size()) + u32(merged.size()) / 2) table_size *= 2;
	edge_table.resize(table_size);

	const float old_area = boundaryArea(positions, merged, locks, remap, edge_table);
	if (old_area == 0.f) return;

	const float new_area = boundaryArea(positions, simplified, locks, remap, edge_table);
	if (new_area >= old_area || new_area < old_area / 4) return;

	float perimeter = 0.f;
	for (u32 i = 0, c = simplified.size(); i < c; i += 3) {
		for (int e = 0; e < 3; ++e) {
			const u32 ia = simplified[i + e];
			const u32 ib = simplified[i + (e == 2 ? 0 : e + 1)];
			const u32 ic = simplified[i + (e + 2) % 3];
			const u32 a = remap[ia];
			const u32 b = remap[ib];
			const u64 id = (u64)b << 32 | a;

			if ((locks[a] & locks[b] & 1) != 0 || *edgeLookup(edge_table, id) != ~0ull) continue;

			const Vec3 ev = positions[ib] - positions[ia];
			const float el = length(ev);
			const Vec3 cv = positions[ia] - positions[ic];
			const float cp = dot(cv, ev) / (el > 0.f ? el * el : 1.f);
			const Vec3 nv = cv - ev * cp;
			const float nl = length(nv);
			const float ns = nl > 0.f ? el / nl : 0.f;
			const Vec4 delta(nv * ns, el);

			if ((locks[a] & 1) == 0) dilate_offsets[a] += delta;
			if ((locks[b] & 1) == 0) dilate_offsets[b] += delta;
			perimeter += el;
		}
	}

	const float distance = perimeter > 0.f ? (old_area - new_area) / perimeter : 0.f;
	for (u32 v : simplified) {
		const u32 r = remap[v];
		if (locks[r] & 1) continue;

		Vec3& vi = positions[v];
		Vec3& vr = positions[r];
		Vec4& n = dilate_offsets[r];

		if (n.w > 0.f) {
			const Vec3 dir = Vec3(n.x, n.y, n.z) * (1.f / n.w);
			const float nn = dot(dir, dir);
			const float ns = distance / maximum(nn, 0.15f);
			vr += dir * ns;
			n = Vec4(0);
			bounds.radius = maximum(bounds.radius, length(vr - bounds.center));
		}

		vi = vr;
	}
}

struct ClusterSphere {
	Vec3 center;
	float radius;
};

ClusterSphere computeBounds(const Vec3* positions, const u32* indices, u32 count) {
	Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX);
	Vec3 mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (u32 i = 0; i < count; ++i) {
		const Vec3 p = positions[indices[i]];
		mn = Vec3(minimum(mn.x, p.x), minimum(mn.y, p.y), minimum(mn.z, p.z));
		mx = Vec3(maximum(mx.x, p.x), maximum(mx.y, p.y), maximum(mx.z, p.z));
	}
	ClusterSphere res;
	res.center = (mn + mx) * 0.5f;
	res.radius = 0;
	for (u32 i = 0; i < count; ++i) {
		res.radius = maximum(res.radius, length(positions[indices[i]] - res.center));
	}
	return res;
}

ClusterSphere unionSpheres(const ClusterSphere* spheres, u32 count) {
	ClusterSphere res;
	res.center = Vec3(0, 0, 0);
	for (u32 i = 0; i < count; ++i) res.center += spheres[i].center;
	res.center = res.center * (1.f / maximum(count, 1u));
	res.radius = 0;
	for (u32 i = 0; i < count; ++i) res.radius = maximum(res.radius, length(spheres[i].center - res.center) + spheres[i].radius);
	return res;
}

void buildNodes(MeshClusters& out, u32 first, u32 count) {
	if (count == 0) return;
	constexpr u32 FANOUT = MeshClusters::NODE_FANOUT;
	constexpr float NO_PARENT = FLT_MAX * 0.5f;

	u32 level_first = out.nodes.size();
	for (u32 c = 0; c < count; c += FANOUT) {
		const u32 n = minimum(FANOUT, count - c);
		ClusterSphere bounds[FANOUT];
		ClusterSphere lods[FANOUT * 2];
		u32 lod_count = 0;
		MeshNodeGPU node;
		memset(&node, 0, sizeof(node));
		node.min_error = FLT_MAX;
		node.max_parent_error = 0;
		for (u32 i = 0; i < n; ++i) {
			const MeshClusterGPU& cl = out.clusters[first + c + i];
			bounds[i] = { cl.center, cl.radius };
			lods[lod_count++] = { cl.lod_center, cl.lod_radius };
			if (cl.parent_error < NO_PARENT) lods[lod_count++] = { cl.parent_center, cl.parent_radius };
			node.min_error = minimum(node.min_error, cl.error);
			node.max_parent_error = maximum(node.max_parent_error, cl.parent_error);
		}
		const ClusterSphere b = unionSpheres(bounds, n);
		const ClusterSphere l = unionSpheres(lods, lod_count);
		node.center = b.center;
		node.radius = b.radius;
		node.lod_center = l.center;
		node.lod_radius = l.radius;
		node.first = first + c;
		node.count_and_flags = n | MeshClusters::LEAF_FLAG;
		out.nodes.push(node);
	}
	u32 level_count = out.nodes.size() - level_first;
	while (level_count > 1) {
		const u32 next_first = out.nodes.size();
		for (u32 c = 0; c < level_count; c += FANOUT) {
			const u32 n = minimum(FANOUT, level_count - c);
			ClusterSphere bounds[FANOUT];
			ClusterSphere lods[FANOUT];
			MeshNodeGPU node;
			memset(&node, 0, sizeof(node));
			node.min_error = FLT_MAX;
			node.max_parent_error = 0;
			for (u32 i = 0; i < n; ++i) {
				const MeshNodeGPU& child = out.nodes[level_first + c + i];
				bounds[i] = { child.center, child.radius };
				lods[i] = { child.lod_center, child.lod_radius };
				node.min_error = minimum(node.min_error, child.min_error);
				node.max_parent_error = maximum(node.max_parent_error, child.max_parent_error);
			}
			const ClusterSphere b = unionSpheres(bounds, n);
			const ClusterSphere l = unionSpheres(lods, n);
			node.center = b.center;
			node.radius = b.radius;
			node.lod_center = l.center;
			node.lod_radius = l.radius;
			node.first = level_first + c;
			node.count_and_flags = n;
			out.nodes.push(node);
		}
		level_first = next_first;
		level_count = out.nodes.size() - level_first;
	}
	out.roots.push(level_first);
}

struct GroupResult {
	explicit GroupResult(IAllocator& alloc)
		: clusters(alloc)
		, indices(alloc)
		, merged(alloc)
		, simplified(alloc)
	{}

	DagBounds bounds = {};
	bool stuck = false;
	bool to_voxels = false;
	u32 voxel_level = 0;
	int refined = -1;
	Array<DagCluster> clusters;
	Array<u32> indices;
	Array<u32> merged;
	Array<u32> simplified;
};

int emitTriangleGroup(ClusterBuildContext& ctx, const DagCluster* group, u32 count, const Array<u32>& cluster_indices, const DagBounds& simplified, int group_depth) {
	PROFILE_BLOCK("clod output group");
	beginClusterGroup(ctx, count, group_depth);
	for (u32 i = 0; i < count; ++i) {
		const DagCluster& c = group[i];
		ASSERT(c.index_count <= CLUSTER_INDEX_COUNT_MASK && c.material <= 0xffffu);
		MeshClusterGPU cluster;
		memset(&cluster, 0, sizeof(cluster));
		cluster.first_index = ctx.out.indices.size();
		cluster.index_count = c.index_count | (c.material << CLUSTER_MATERIAL_SHIFT);
		cluster.group = ctx.group_id_counter;
		cluster.refined_group = c.refined >= 0 ? (u32)c.refined : MeshClusters::NO_GROUP;
		const u32* src_indices = cluster_indices.data() + c.index_offset;
		for (u32 j = 0; j < c.index_count; ++j) {
			ctx.out.indices.push(src_indices[j]);
			if (ctx.dilate_borders) ctx.out.index_positions.push(ctx.positions[src_indices[j]]);
		}
		const ClusterSphere bounds = computeBounds(ctx.positions, &ctx.out.indices[cluster.first_index], c.index_count);
		cluster.center = bounds.center;
		cluster.radius = bounds.radius;
		const DagBounds lod = c.refined >= 0 ? ctx.group_bounds[c.refined] : c.bounds;
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

void simplifyAndClassifyGroup(const DagCluster* group, u32 group_size, bool is_single_root, const Array<u32>& cluster_indices,
	const Vec3* positions, u32 vertex_count, const Vec3* normals, const Array<u8>& locks,
	const ClusterVoxelBuilder& voxel_builder, GroupResult& res)
{
	for (u32 j = 0; j < group_size; ++j) {
		appendSlice(res.merged, cluster_indices.data() + group[j].index_offset, group[j].index_count);
	}

	if (is_single_root) {
		DagBounds bounds = group[0].bounds;
		if (voxel_builder.enabled) {
			const float avg_edge = voxel_builder.computeAvgEdge(positions, res.merged.data(), res.merged.size());
			const u32 k = voxel_builder.chooseLevel(bounds.error, avg_edge);
			bounds.error = voxel_builder.levelError(k);
			res.bounds = bounds;
			res.stuck = true;
			res.to_voxels = true;
			res.voxel_level = k;
		}
		else {
			bounds.error = FLT_MAX;
			res.bounds = bounds;
			res.stuck = true;
		}
		return;
	}

	const u32 target_size = u32((res.merged.size() / 3) * SIMPLIFY_RATIO) * 3;
	DagBounds bounds = dagBoundsMerge(&group[0].bounds, group_size, sizeof(DagCluster));
	const float prev_error = bounds.error;

	float error = 0.f;
	{
		PROFILE_BLOCK("clod simplify");
		simplifyGroup(res.simplified, positions, vertex_count, normals, res.merged, locks, target_size, !voxel_builder.enabled, &error);
	}

	if (voxel_builder.enabled) {
		const float group_avg_edge = voxel_builder.computeAvgEdge(positions, res.merged.data(), res.merged.size());
		if (voxel_builder.shouldSwitchGroup(res.merged.size(), res.simplified.size(), prev_error, error, group_avg_edge)) {
			const u32 k = voxel_builder.chooseLevel(prev_error, group_avg_edge);
			bounds.error = voxel_builder.levelError(k);
			res.bounds = bounds;
			res.stuck = true;
			res.to_voxels = true;
			res.voxel_level = k;
			return;
		}
	}
	else if (res.simplified.size() > res.merged.size() * SIMPLIFY_THRESHOLD) {
		bounds.error = FLT_MAX;
		res.bounds = bounds;
		res.stuck = true;
		return;
	}

	bounds.error = maximum(bounds.error, error);
	res.bounds = bounds;
	res.stuck = false;
}

} // anonymous namespace

DagBounds dagBoundsMerge(const DagBounds* bounds, u32 count, size_t stride) {
	const meshopt_Bounds merged = meshopt_computeSphereBounds(&bounds[0].center.x, count, stride, &bounds[0].radius, stride);
	float error = 0.f;
	for (u32 i = 0; i < count; ++i) {
		const auto* b = reinterpret_cast<const DagBounds*>(reinterpret_cast<const char*>(bounds) + i * stride);
		error = maximum(error, b->error);
	}
	return { Vec3(merged.center[0], merged.center[1], merged.center[2]), merged.radius, error };
}

void closeClusterLevel(ClusterBuildContext& ctx) {
	if (ctx.level > 0) {
		ctx.level_ranges.push(ctx.level_first);
		ctx.level_ranges.push(ctx.out.clusters.size() - ctx.level_first);
	}
}

void beginClusterGroup(ClusterBuildContext& ctx, u32 count, int group_depth) {
	MeshGroupGPU group_entry;
	group_entry.first_cluster = ctx.out.clusters.size();
	group_entry.cluster_count = count;
	group_entry.stream_offset = 0;
	group_entry.stream_size = 0;
	ctx.out.groups.push(group_entry);
	if (group_depth != ctx.level) {
		if (ctx.level == 0) {
			// the finest clusters are the first ones, they contain every triangle once
			ctx.out.leaf_index_count = ctx.out.indices.size();
			ctx.out.leaf_cluster_count = ctx.out.clusters.size();
			ctx.leaves_done = true;
		}
		closeClusterLevel(ctx);
		ctx.level = group_depth;
		ctx.level_first = ctx.out.clusters.size();
	}
}

bool buildMeshClusters(Span<const Vec3> positions, Span<const Vec3> normals, Span<const Vec2> uvs, Span<const u32> indices, bool dilate_borders, MeshClusters& out, IAllocator& allocator, const ClusterVoxelSource* voxels) {
	const ClusterSubmesh submesh = { indices, 0 };
	return buildMeshClusters(positions, normals, uvs, Span<const ClusterSubmesh>(&submesh, 1), dilate_borders, out, allocator, voxels);
}

bool buildMeshClusters(Span<const Vec3> positions, Span<const Vec3> normals, Span<const Vec2> uvs, Span<const ClusterSubmesh> submeshes, bool dilate_borders, MeshClusters& out, IAllocator& allocator, const ClusterVoxelSource* voxels) {
	PROFILE_FUNCTION();
	if (submeshes.length() == 0 || positions.length() == 0) return false;
	const u32 vertex_count = positions.length();

	Array<Vec3> dilated_positions(allocator);
	if (dilate_borders) {
		dilated_positions.resize(vertex_count);
		memcpy(dilated_positions.begin(), positions.begin(), vertex_count * sizeof(Vec3));
	}
	const Vec3* cluster_positions = dilate_borders ? dilated_positions.begin() : positions.begin();
	const Vec3* mesh_normals = normals.length() == vertex_count ? normals.begin() : nullptr;

	Array<u8> locks(allocator);
	locks.resize(vertex_count);
	memset(locks.data(), 0, locks.byte_size());

	Array<u32> remap(allocator);
	remap.resize(vertex_count);
	{
		PROFILE_BLOCK("clod position remap");
		meshopt_generatePositionRemap(remap.data(), &cluster_positions[0].x, vertex_count, sizeof(Vec3));
	}

	if (uvs.length() == vertex_count) {
		PROFILE_BLOCK("clod protect seams");
		for (u32 i = 0; i < vertex_count; ++i) {
			const u32 r = remap[i];
			if (r != i && uvs[i] != uvs[r]) locks[i] |= meshopt_SimplifyVertex_Protect;
		}
	}

	Array<Vec4> dilate_offsets(allocator);
	if (dilate_borders) {
		dilate_offsets.resize(vertex_count);
		memset(dilate_offsets.data(), 0, dilate_offsets.byte_size());
	}

	Array<u32> cluster_indices(allocator);
	Array<DagCluster> clusters(allocator);
	{
		PROFILE_BLOCK("clod initial clusterize");
		for (const ClusterSubmesh& sub : submeshes) {
			if (sub.indices.length() < 3) continue;
			clusterize(clusters, cluster_indices, cluster_positions, vertex_count, sub.indices.begin(), sub.indices.length(), -1, sub.material);
		}
		if (clusters.empty()) return false;
		for (DagCluster& cluster : clusters) {
			cluster.bounds = dagBoundsCompute(cluster_positions, vertex_count, cluster_indices.data() + cluster.index_offset, cluster.index_count, 0.f);
		}
	}

	ClusterBuildContext ctx(out, allocator, cluster_positions, dilate_borders);
	ClusterVoxelBuilder voxel_builder(allocator);
	voxel_builder.init(voxels, cluster_positions, mesh_normals, vertex_count, clusters, cluster_indices, dilate_borders);

	Array<DagCluster> pending(allocator);
	Array<u32> pending_indices(allocator);
	pending.reserve(clusters.size());
	pending_indices.reserve(u32(cluster_indices.size() * SIMPLIFY_THRESHOLD));

	int depth = 0;
	while (!clusters.empty()) {
		PROFILE_BLOCK("clod level");

		Array<PartitionGroup> groups(allocator);
		{
			PROFILE_BLOCK("clod partition");
			groups = partitionClusters(clusters, cluster_positions, cluster_indices, remap);
		}

		pending.clear();
		pending_indices.clear();

		const u32 group_count = groups.size();
		{
			PROFILE_BLOCK("clod lock boundary");
			lockBoundary(locks, clusters.data(), groups.data(), group_count, cluster_indices, remap);
		}

		Array<GroupResult> results(allocator);
		results.reserve(group_count);
		for (u32 i = 0; i < group_count; ++i) results.emplace(allocator);

		PROFILE_BLOCK("clod groups");
		// 1. Simplify each group in parallel and classify whether it continues, stops, or switches to voxels.
		jobs::forEach(group_count, 1, [&](u32 from, u32 to) {
			for (u32 i = from; i < to; ++i) {
				const DagCluster* group = clusters.data() + groups[i].offset;
				const u32 group_size = groups[i].count;
				simplifyAndClassifyGroup(group, group_size, groups[i].is_single_root, cluster_indices, cluster_positions, vertex_count, mesh_normals, locks, voxel_builder, results[i]);
			}
		});

		// 2. Emit pre-dilation clusters for this level and stage any groups switching to voxels.
		for (u32 i = 0; i < group_count; ++i) {
			const DagCluster* group = clusters.data() + groups[i].offset;
			const u32 group_size = groups[i].count;
			results[i].refined = emitTriangleGroup(ctx, group, group_size, cluster_indices, results[i].bounds, depth);
			if (results[i].to_voxels) {
				voxel_builder.voxelizeGroup(ctx, results[i].merged.data(), results[i].merged.size(), results[i].refined, results[i].voxel_level);
			}
		}

		// 3. Optionally dilate open borders (mutates dilated_positions in place) and re-clusterize simplified geometry in parallel.
		jobs::forEach(group_count, 1, [&](u32 from, u32 to) {
			Array<u64> dilate_table(allocator);
			for (u32 i = from; i < to; ++i) {
				GroupResult& res = results[i];
				if (res.stuck) continue;
				const u32 group_material = clusters[groups[i].offset].material;
				if (dilate_borders) {
					PROFILE_BLOCK("clod dilate borders");
					dilateBorders(dilated_positions.begin(), res.merged, res.simplified, locks, remap, res.bounds, dilate_offsets, dilate_table);
				}
				{
					PROFILE_BLOCK("clod clusterize group");
					clusterize(res.clusters, res.indices, cluster_positions, vertex_count, res.simplified.data(), res.simplified.size(), res.refined, group_material);
				}
				for (DagCluster& c : res.clusters) c.bounds = res.bounds;
			}
		});

		// 4. Gather next level's clusters.
		for (u32 i = 0; i < group_count; ++i) {
			GroupResult& res = results[i];
			if (res.stuck) continue;
			const u32 index_offset = pending_indices.size();
			appendSlice(pending_indices, res.indices.data(), res.indices.size());
			for (DagCluster& c : res.clusters) {
				c.index_offset += index_offset;
				pending.push(c);
			}
		}

		clusters.swap(pending);
		cluster_indices.swap(pending_indices);
		depth++;
	}

	if (voxel_builder.enabled) voxel_builder.buildStagedLevels(ctx, depth);

	if (!ctx.leaves_done) {
		out.leaf_index_count = out.indices.size();
		out.leaf_cluster_count = out.clusters.size();
	}
	closeClusterLevel(ctx);
	{
		PROFILE_BLOCK("build cluster nodes");
		buildNodes(out, 0, out.leaf_cluster_count);
		for (u32 i = 0; i + 1 < (u32)ctx.level_ranges.size(); i += 2) buildNodes(out, ctx.level_ranges[i], ctx.level_ranges[i + 1]);
	}

	return true;
}

static constexpr float QUAT_RANGE = 0.70710678f;

u32 packClusterFrame(const Vec3& normal, const Vec3* tangent) {
	Vec3 n = normal;
	const float normal_length = length(n);
	n = normal_length > 0 ? n * (1.f / normal_length) : Vec3(0, 1, 0);

	Vec3 t = tangent ? *tangent : Vec3(0, 0, 0);
	t = t - n * dot(n, t);
	float tangent_length = length(t);
	if (tangent_length < 1e-4f) {
		t = fabsf(n.x) < 0.9f ? cross(n, Vec3(1, 0, 0)) : cross(n, Vec3(0, 1, 0));
		tangent_length = length(t);
	}
	t = t * (1.f / tangent_length);
	const Vec3 b = cross(n, t);

	const float m00 = t.x, m01 = b.x, m02 = n.x;
	const float m10 = t.y, m11 = b.y, m12 = n.y;
	const float m20 = t.z, m21 = b.z, m22 = n.z;
	float q[4];
	const float trace = m00 + m11 + m22;
	if (trace > 0) {
		const float s = sqrtf(trace + 1.f) * 2.f;
		q[3] = 0.25f * s;
		q[0] = (m21 - m12) / s;
		q[1] = (m02 - m20) / s;
		q[2] = (m10 - m01) / s;
	}
	else if (m00 > m11 && m00 > m22) {
		const float s = sqrtf(1.f + m00 - m11 - m22) * 2.f;
		q[3] = (m21 - m12) / s;
		q[0] = 0.25f * s;
		q[1] = (m01 + m10) / s;
		q[2] = (m02 + m20) / s;
	}
	else if (m11 > m22) {
		const float s = sqrtf(1.f + m11 - m00 - m22) * 2.f;
		q[3] = (m02 - m20) / s;
		q[0] = (m01 + m10) / s;
		q[1] = 0.25f * s;
		q[2] = (m12 + m21) / s;
	}
	else {
		const float s = sqrtf(1.f + m22 - m00 - m11) * 2.f;
		q[3] = (m10 - m01) / s;
		q[0] = (m02 + m20) / s;
		q[1] = (m12 + m21) / s;
		q[2] = 0.25f * s;
	}
	const float q_length = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	for (float& v : q) v /= q_length;

	u32 largest = 0;
	for (u32 i = 1; i < 4; ++i) {
		if (fabsf(q[i]) > fabsf(q[largest])) largest = i;
	}
	const float sign = q[largest] < 0 ? -1.f : 1.f;
	u32 packed = largest << 30;
	u32 shift = 0;
	for (u32 i = 0; i < 4; ++i) {
		if (i == largest) continue;
		const float v = clamp(q[i] * sign / QUAT_RANGE, -1.f, 1.f);
		const i32 iv = (i32)floorf(v * 511.f + 0.5f);
		packed |= (u32(iv) & 0x3ff) << shift;
		shift += 10;
	}
	return packed;
}

void unpackClusterFrame(u32 packed, Vec3& normal, Vec3& tangent) {
	const u32 largest = packed >> 30;
	float abc[3];
	for (u32 i = 0; i < 3; ++i) {
		const i32 v = (i32)((packed >> (i * 10)) & 0x3ff);
		abc[i] = float(v >= 512 ? v - 1024 : v) * (QUAT_RANGE / 511.f);
	}
	const float d = sqrtf(maximum(0.f, 1.f - (abc[0] * abc[0] + abc[1] * abc[1] + abc[2] * abc[2])));
	float q[4];
	u32 next = 0;
	for (u32 i = 0; i < 4; ++i) q[i] = i == largest ? d : abc[next++];
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	normal = Vec3(2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y));
	tangent = Vec3(1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y));
}

bool packClusterVertices(const ClusterVertexSource& source, MeshClusters& out, IAllocator& allocator) {
	PROFILE_FUNCTION();
	if (!source.positions || !source.normals || source.vertex_count == 0 || out.clusters.empty()) return false;
	const u32 vertex_count = source.vertex_count;
	const u32 cluster_count = (u32)out.clusters.size();

	double max_coordinate = 0;
	auto add_coordinate = [&](const Vec3& p) {
		max_coordinate = maximum(max_coordinate, (double)maximum(fabsf(p.x), maximum(fabsf(p.y), fabsf(p.z))));
	};
	for (u32 i = 0; i < vertex_count; ++i) add_coordinate(source.positions[i]);
	for (const Vec3& p : out.index_positions) add_coordinate(p);
	float step = 1.f / 16384.f;
	while (max_coordinate / step >= 16777216.0) step *= 2.f;
	auto to_lattice = [&](const Vec3& p, i32* result) {
		result[0] = (i32)floor((double)p.x / step + 0.5);
		result[1] = (i32)floor((double)p.y / step + 0.5);
		result[2] = (i32)floor((double)p.z / step + 0.5);
	};

	out.attribute_flags = 0;
	if (source.uvs) out.attribute_flags |= (u32)ClusterAttribute::UV;
	if (source.ao) out.attribute_flags |= (u32)ClusterAttribute::AO;
	if (source.colors) out.attribute_flags |= (u32)ClusterAttribute::COLOR;
	if (source.tangents) out.attribute_flags |= (u32)ClusterAttribute::TANGENT;
	out.attribute_stride = 4 + (source.uvs ? 4 : 0) + (source.ao ? 4 : 0) + (source.colors ? 4 : 0);

	out.vertex_headers.clear();
	out.vertex_words.clear();
	out.local_indices.resize(out.indices.size());
	out.vertex_count = 0;
	out.leaf_vertex_count = 0;

	Array<u32> local_of(allocator);
	local_of.resize(vertex_count);
	for (u32& l : local_of) l = INVALID_INDEX;
	Array<u32> vertices(allocator);
	Array<i32> cluster_lattice(allocator);

	u64 bit_cursor = 0;
	u64 group_start_bit = 0;
	u32 current_group = MeshClusters::NO_GROUP;
	auto align_cursor = [&]() { bit_cursor = (bit_cursor + 31) & ~u64(31); };
	auto write_bits = [&](u32 value, u32 count) {
		if (count == 0) return;
		const u32 word = (u32)(bit_cursor >> 5);
		const u32 shift = (u32)(bit_cursor & 31);
		while ((u32)out.vertex_words.size() < word + 2) out.vertex_words.push(0);
		out.vertex_words[word] |= value << shift;
		if (shift + count > 32) out.vertex_words[word + 1] |= value >> (32 - shift);
		bit_cursor += count;
	};

	for (u32 ci = 0; ci < cluster_count; ++ci) {
		const MeshClusterGPU& cluster = out.clusters[ci];
		if (cluster.group >= (u32)out.groups.size() || (current_group != MeshClusters::NO_GROUP && cluster.group < current_group)) {
			logError("Cluster ", ci, " has an invalid group");
			return false;
		}
		if (cluster.group != current_group) {
			align_cursor();
			if (current_group != MeshClusters::NO_GROUP) out.groups[current_group].stream_size = u32(bit_cursor / 8 - out.groups[current_group].stream_offset);
			current_group = cluster.group;
			group_start_bit = bit_cursor;
			out.groups[current_group].stream_offset = (u32)(bit_cursor / 8);
		}
		const u32 idx_count = clusterIndexCount(cluster);
		if (idx_count == 0) {
			if (cluster.first_index >= (u32)out.voxel_clusters.size()) {
				logError("Cluster ", ci, " has no triangles and no voxels");
				return false;
			}
			const MeshVoxelCluster& voxel = out.voxel_clusters[cluster.first_index];
			align_cursor();
			if (bit_cursor + u64(voxel.word_count) * 32 >= (1ull << 32)) {
				logError("Vertices of clusters are larger than 512 MB");
				return false;
			}
			MeshVoxelClusterHeaderGPU vh = {};
			vh.base_voxel[0] = voxel.base[0];
			vh.base_voxel[1] = voxel.base[1];
			vh.base_voxel[2] = voxel.base[2];
			vh.brick_count = voxel.brick_count;
			vh.payload_bit_offset = (u32)(bit_cursor - group_start_bit);
			vh.voxel_size = voxel.size;
			vh.grid_origin = voxel.origin;
			MeshClusterVertexGPU header;
			memcpy(&header, &vh, sizeof(header));
			for (u32 w = 0; w < voxel.word_count; ++w) write_bits(out.voxel_words[voxel.first_word + w], 32);
			out.vertex_headers.push(header);
			continue;
		}
		vertices.clear();
		cluster_lattice.clear();
		for (u32 k = 0; k < idx_count; ++k) {
			const u32 g = out.indices[cluster.first_index + k];
			if (local_of[g] == INVALID_INDEX) {
				local_of[g] = (u32)vertices.size();
				vertices.push(g);
				i32 p[3];
				to_lattice(out.index_positions.empty() ? source.positions[g] : out.index_positions[cluster.first_index + k], p);
				cluster_lattice.push(p[0]);
				cluster_lattice.push(p[1]);
				cluster_lattice.push(p[2]);
			}
			out.local_indices[cluster.first_index + k] = (u16)local_of[g];
		}
		if ((u32)vertices.size() > MeshClusters::MAX_CLUSTER_VERTICES) {
			logError("Cluster ", ci, " has ", (u32)vertices.size(), " vertices");
			return false;
		}
		for (u32 g : vertices) local_of[g] = INVALID_INDEX;

		MeshClusterVertexGPU header;
		memset(&header, 0, sizeof(header));
		i32 mn[3] = { 0x7fffFFFF, 0x7fffFFFF, 0x7fffFFFF };
		i32 mx[3] = { -0x7fffFFFF, -0x7fffFFFF, -0x7fffFFFF };
		Vec2 uv_min(FLT_MAX, FLT_MAX), uv_max(-FLT_MAX, -FLT_MAX);
		for (u32 v = 0; v < (u32)vertices.size(); ++v) {
			const u32 g = vertices[v];
			for (u32 a = 0; a < 3; ++a) {
				mn[a] = minimum(mn[a], cluster_lattice[v * 3 + a]);
				mx[a] = maximum(mx[a], cluster_lattice[v * 3 + a]);
			}
			if (source.uvs) {
				uv_min = Vec2(minimum(uv_min.x, source.uvs[g].x), minimum(uv_min.y, source.uvs[g].y));
				uv_max = Vec2(maximum(uv_max.x, source.uvs[g].x), maximum(uv_max.y, source.uvs[g].y));
			}
		}
		u32 bits[3];
		for (u32 a = 0; a < 3; ++a) {
			const u32 range = u32(mx[a] - mn[a]);
			bits[a] = 0;
			while (bits[a] < 32 && (range >> bits[a]) != 0) ++bits[a];
			if (bits[a] > MeshClusters::MAX_POSITION_BITS) {
				logError("Cluster ", ci, " is too large for its positions");
				return false;
			}
			header.pos_start[a] = mn[a];
		}
		header.vertex_count = (u32)vertices.size();
		header.pos_bit_offset = (u32)(bit_cursor - group_start_bit);
		header.pos_step = step;

		u32 uv_bits[2] = { 0, 0 };
		if (source.uvs) {
			header.uv_min[0] = uv_min.x;
			header.uv_min[1] = uv_min.y;
			header.uv_extent[0] = uv_max.x - uv_min.x;
			header.uv_extent[1] = uv_max.y - uv_min.y;
			for (u32 a = 0; a < 2; ++a) {
				while (uv_bits[a] < 16 && header.uv_extent[a] > UV_STEP * float((1u << uv_bits[a]) - 1)) ++uv_bits[a];
			}
		}
		header.uv_bits = uv_bits[0] | (uv_bits[1] << 8);

		const u32 attribute_bits = 32 + (source.uvs ? uv_bits[0] + uv_bits[1] : 0) + (source.ao ? 8 : 0) + (source.colors ? 32 : 0);
		header.pos_bits = bits[0] | (bits[1] << 8) | (bits[2] << 16) | (attribute_bits << 24);
		if (bit_cursor + u64((u32)vertices.size()) * (bits[0] + bits[1] + bits[2] + attribute_bits) >= (1ull << 32)) {
			logError("Vertices of clusters are larger than 512 MB");
			return false;
		}

		for (u32 vi = 0; vi < (u32)vertices.size(); ++vi) {
			const u32 g = vertices[vi];
			for (u32 a = 0; a < 3; ++a) write_bits(u32(cluster_lattice[vi * 3 + a] - mn[a]), bits[a]);

			const Vec3 tangent = source.tangents ? source.tangents[g] : Vec3(0, 0, 0);
			write_bits(packClusterFrame(source.normals[g], source.tangents ? &tangent : nullptr), 32);
			if (source.uvs) {
				for (u32 a = 0; a < 2; ++a) {
					const float extent = header.uv_extent[a];
					const float v = (a == 0 ? source.uvs[g].x : source.uvs[g].y) - header.uv_min[a];
					const float max_q = float((1u << uv_bits[a]) - 1);
					write_bits(extent > 0 ? (u32)clamp(floorf(v / extent * max_q + 0.5f), 0.f, max_q) : 0, uv_bits[a]);
				}
			}
			if (source.ao) write_bits(source.ao[g] & 0xff, 8);
			if (source.colors) write_bits(source.colors[g], 32);
		}
		out.vertex_count += (u32)vertices.size();
		out.vertex_headers.push(header);
		if (ci + 1 == out.leaf_cluster_count) out.leaf_vertex_count = out.vertex_count;
	}

	// shaders read two words at any bit
	const u32 words = (u32)((bit_cursor + 31) >> 5) + 2;
	while ((u32)out.vertex_words.size() < words) out.vertex_words.push(0);
	if (current_group != MeshClusters::NO_GROUP) out.groups[current_group].stream_size = words * 4 - out.groups[current_group].stream_offset;

	return true;
}

} // namespace Lumix
