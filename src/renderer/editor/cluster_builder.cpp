#include "cluster_builder.h"

#include "core/crt.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/string.h"

#include "meshoptimizer/meshoptimizer.h"

namespace Lumix {

namespace {

// the sizes of a cluster. The indices of a cluster are bytes, so it has at most 256 vertices (UE has 256 vertices and 128 triangles too). meshoptimizer recommends 64 and 124
// for mesh shaders, but unwelded geometry such as leaves has about as many vertices as triangles, so 64 vertices stop it at about 65 triangles.
constexpr u32 MAX_CLUSTER_VERTICES = 256;
constexpr u32 MAX_CLUSTER_TRIANGLES = 128;
constexpr u32 MIN_CLUSTER_TRIANGLES = MAX_CLUSTER_TRIANGLES / 3;
constexpr u32 PARTITION_SIZE = 16;
constexpr float CLUSTER_SPLIT_FACTOR = 2.0f;
constexpr int OPTIMIZE_CLUSTERS_LEVEL = 1;
constexpr float SIMPLIFY_RATIO = 0.5f;
constexpr float SIMPLIFY_THRESHOLD = 0.85f;
constexpr float SIMPLIFY_ERROR_FACTOR_SLOPPY = 2.0f;
constexpr float UV_STEP = 1.f / 16384.f;	// the largest step of quantized UVs, unless the range of a cluster is too large for 16 bits
constexpr u32 INVALID_INDEX = 0xffFFffFF;

// Cluster LOD DAG builder, based on meshoptimizer's clusterlod (MIT License, Copyright (c) 2016-2026 Arseny Kapoulkine).

struct DagBounds {
	Vec3 center;
	float radius;
	float error;
};

struct DagCluster {
	u32 index_offset;
	u32 index_count;
	int refined;
	DagBounds bounds;
};

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

DagBounds dagBoundsMerge(const DagBounds* bounds, u32 count, size_t stride) {
	const meshopt_Bounds merged = meshopt_computeSphereBounds(&bounds[0].center.x, count, stride, &bounds[0].radius, stride);
	float error = 0.f;
	for (u32 i = 0; i < count; ++i) {
		const auto* b = reinterpret_cast<const DagBounds*>(reinterpret_cast<const char*>(bounds) + i * stride);
		error = maximum(error, b->error);
	}
	return { Vec3(merged.center[0], merged.center[1], merged.center[2]), merged.radius, error };
}

void clusterize(Array<DagCluster>& clusters, Array<u32>& cluster_indices, const Vec3* positions, u32 vertex_count, const u32* indices, u32 index_count, int refined = -1) {
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
	}
}

Array<u32> partitionClusters(Array<DagCluster>& clusters, const Vec3* positions, const Array<u32>& indices, const Array<u32>& remap) {
	IAllocator& allocator = clusters.getAllocator();
	const u32 cluster_count = clusters.size();

	if (cluster_count <= PARTITION_SIZE) {
		Array<u32> offsets(allocator);
		offsets.reserve(2);
		offsets.push(0);
		offsets.push(cluster_count);
		return offsets;
	}

	Array<u32> cluster_counts(allocator);
	cluster_counts.resize(cluster_count);
	Array<u32> cluster_indices(allocator);
	cluster_indices.reserve(indices.size());

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

	clusters.swap(result);
	return partition_offsets;
}

void lockBoundary(Array<u8>& locks, const DagCluster* clusters, const u32* group_offsets, u32 group_count, const Array<u32>& cluster_indices, const Array<u32>& remap) {
	// bit 7 is temporary storage indicating the vertex was seen by an earlier group
	for (u8& lock : locks) lock &= ~((1 << 0) | (1 << 7));

	for (u32 i = 0; i < group_count; ++i) {
		const DagCluster* group = clusters + group_offsets[i];
		const u32 group_size = group_offsets[i + 1] - group_offsets[i];

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

// sphere around the spheres, the centre is the average of their centres
ClusterSphere unionSpheres(const ClusterSphere* spheres, u32 count) {
	ClusterSphere res;
	res.center = Vec3(0, 0, 0);
	for (u32 i = 0; i < count; ++i) res.center += spheres[i].center;
	res.center = res.center * (1.f / maximum(count, 1u));
	res.radius = 0;
	for (u32 i = 0; i < count; ++i) res.radius = maximum(res.radius, length(spheres[i].center - res.center) + spheres[i].radius);
	return res;
}

// A tree over the clusters [first, first + count) (the clusters of one level are consecutive), appends its nodes to `out.nodes` and its root to `out.roots`.
// Clusters are neighbours in the array if they were made by the same group or by groups that were next to each other, so that nodes are compact.
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
		node.min_attribute_error = FLT_MAX;
		node.max_parent_attribute_error = 0;
		for (u32 i = 0; i < n; ++i) {
			const MeshClusterGPU& cl = out.clusters[first + c + i];
			bounds[i] = { cl.center, cl.radius };
			lods[lod_count++] = { cl.lod_center, cl.lod_radius };
			if (cl.parent_error < NO_PARENT) lods[lod_count++] = { cl.parent_center, cl.parent_radius };
			node.min_error = minimum(node.min_error, cl.error);
			node.max_parent_error = maximum(node.max_parent_error, cl.parent_error);
			node.min_attribute_error = minimum(node.min_attribute_error, cl.attribute_error);
			node.max_parent_attribute_error = maximum(node.max_parent_attribute_error, cl.parent_attribute_error);
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
			node.min_attribute_error = FLT_MAX;
			node.max_parent_attribute_error = 0;
			for (u32 i = 0; i < n; ++i) {
				const MeshNodeGPU& child = out.nodes[level_first + c + i];
				bounds[i] = { child.center, child.radius };
				lods[i] = { child.lod_center, child.lod_radius };
				node.min_error = minimum(node.min_error, child.min_error);
				node.max_parent_error = maximum(node.max_parent_error, child.max_parent_error);
				node.min_attribute_error = minimum(node.min_attribute_error, child.min_attribute_error);
				node.max_parent_attribute_error = maximum(node.max_parent_attribute_error, child.max_parent_attribute_error);
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

} // anonymous namespace

// ---- voxel levels, see MeshVoxelCluster ----
namespace {

constexpr u32 VOXEL_BRICKS_PER_CLUSTER = 64;
static_assert(VOXEL_BRICKS_PER_CLUSTER <= 64, "voxel_bricks.hlsl uses 64-bit masks and 6-bit brick indices");
constexpr u32 VOXEL_GROUP_CLUSTERS = 8;		// clusters of a group that is downsampled to the next level
constexpr float VOXEL_MAX_RESOLUTION = 4000.f;	// the voxels of the finest level along the longest side of the mesh, so that bricks are less than 1024 apart
constexpr float VOXEL_HALF_DIAGONAL = 0.8660254f;	// the distance from the center of a voxel to its corner, in voxels: the error of a voxel level
constexpr float VOXEL_MIN_COVERAGE = 0.25f;		// minimum accumulated surface area / (size * size) for a voxel to survive (prevents conservative-overlap and OR-downsample bloat)
constexpr float VOXEL_STALL_THRESHOLD = 0.65f;	// switch a group to voxels if simplification leaves more than this fraction of its triangles

float computeAvgTriangleEdge(const Vec3* positions, const u32* indices, u32 index_count) {
	double sum = 0.0;
	u32 edges = 0;
	for (u32 t = 0; t + 2 < index_count; t += 3) {
		const Vec3 p0 = positions[indices[t]], p1 = positions[indices[t + 1]], p2 = positions[indices[t + 2]];
		sum += length(p1 - p0) + length(p2 - p1) + length(p0 - p2);
		edges += 3;
	}
	return edges > 0 ? float(sum / double(edges)) : 0.f;
}

// A voxel of the grid of a level, the sums of the attributes of what falls in it. The key orders voxels by the Morton code of their brick, and then by their place in the brick.
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

struct KeyIndex {
	u64 key;
	u32 index;
};

// the 16 low bits of x with two zero bits between them
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

// sorts the voxels and adds the ones with the same key up
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

// Prunes sparse voxels below `min_coverage` (keeping at least the densest voxel if all are below) and clamps surviving coverage to 1.0 so stacked leaves do not inflate downsampling.
void filterVoxelsByCoverage(Array<Voxel>& voxels, float min_coverage) {
	if (voxels.empty()) return;
	u32 best = 0;
	u32 out = 0;
	for (u32 i = 0; i < (u32)voxels.size(); ++i) {
		if (voxels[i].weight > voxels[best].weight) best = i;
		if (voxels[i].weight >= min_coverage) {
			Voxel v = voxels[i];
			if (v.weight > 1.f) {
				const float s = 1.f / v.weight;
				v.normal = v.normal * s;
				v.color = v.color * s;
				v.ao *= s;
				v.weight = 1.f;
			}
			voxels[out++] = v;
		}
	}
	if (out == 0) {
		Voxel v = voxels[best];
		if (v.weight > 1.f) {
			const float s = 1.f / v.weight;
			v.normal = v.normal * s;
			v.color = v.color * s;
			v.ao *= s;
			v.weight = 1.f;
		}
		voxels[0] = v;
		out = 1;
	}
	voxels.resize(out);
}

// Area-weighted point-sampled voxelization (stratified barycentric grid per triangle) so grazing edges and sparse leaf cards do not bloat into full cubes.
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

// Downsamples voxels to a grid twice as large, scaling each child's coverage by `scale` (0.25 preserves 2D surface area, 0.5 preserves 1D structures).
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

// Splits the sorted voxels into clusters of at most VOXEL_BRICKS_PER_CLUSTER bricks and appends them to the level, `bounds.error` of them is `error`.
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

// Orders the clusters of the level by the Morton code of their centers, so that the clusters that are grouped are neighbours.
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

// The payload of a cluster, see MeshVoxelCluster. Returns the index of its record.
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

	// bricks
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
	// voxels
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

} // anonymous namespace

bool buildMeshClusters(Span<const Vec3> positions, Span<const Vec3> normals, Span<const Vec2> uvs, Span<const u32> indices, bool dilate_borders, MeshClusters& out, IAllocator& allocator, const ClusterVoxelSource* voxels) {
	PROFILE_FUNCTION();
	if (indices.length() < 3 || positions.length() == 0) return false;
	const u32 vertex_count = positions.length();

	// Dilation moves the vertices of the simplified clusters, in the positions that are given to it, from level to level. So the positions are a copy that it can
	// change, and every cluster takes the positions that its vertices have when the cluster is emitted below.
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

	// position-only remap mapping vertices with the same position to the same index
	Array<u32> remap(allocator);
	remap.resize(vertex_count);
	{
		PROFILE_BLOCK("clod position remap");
		meshopt_generatePositionRemap(remap.data(), &cluster_positions[0].x, vertex_count, sizeof(Vec3));
	}

	// protect UV seams for permissive simplification
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
		clusterize(clusters, cluster_indices, cluster_positions, vertex_count, indices.begin(), indices.length());
		for (DagCluster& cluster : clusters) {
			cluster.bounds = dagBoundsCompute(cluster_positions, vertex_count, cluster_indices.data() + cluster.index_offset, cluster.index_count, 0.f);
		}
	}

	// Emits the clusters of a group, with the error and bounds of the simplification that the group goes through to make the next level
	// (the error is FLT_MAX if there is none: the group is the last one, or it could not be simplified). Groups are emitted in order of levels, so clusters of a level are consecutive.
	Array<u32> level_ranges(allocator);	// [first cluster, number of clusters] of the levels above the finest
	int level = -1;
	u32 level_first = 0;
	u32 group_id_counter = 0;
	Array<DagBounds> group_bounds(allocator);
	bool leaves_done = false;
	auto close_level = [&]() {
		if (level > 0) {
			level_ranges.push(level_first);
			level_ranges.push(out.clusters.size() - level_first);
		}
	};
	auto begin_group = [&](u32 count, int group_depth) {
		MeshGroupGPU group_entry;
		group_entry.first_cluster = out.clusters.size();
		group_entry.cluster_count = count;
		group_entry.stream_offset = 0;
		group_entry.stream_size = 0;
		out.groups.push(group_entry);
		if (group_depth != level) {
			if (level == 0) {
				// the finest clusters are the first ones, they contain every triangle once
				out.leaf_index_count = out.indices.size();
				out.leaf_cluster_count = out.clusters.size();
				leaves_done = true;
			}
			close_level();
			level = group_depth;
			level_first = out.clusters.size();
		}
	};
	auto emit_group = [&](const DagCluster* group, u32 count, const DagBounds& simplified, int group_depth) -> int {
		PROFILE_BLOCK("clod output group");
		begin_group(count, group_depth);
		for (u32 i = 0; i < count; ++i) {
			const DagCluster& c = group[i];
			MeshClusterGPU cluster;
			memset(&cluster, 0, sizeof(cluster));
			cluster.first_index = out.indices.size();
			cluster.index_count = c.index_count;
			cluster.group = group_id_counter;
			cluster.refined_group = c.refined >= 0 ? (u32)c.refined : MeshClusters::NO_GROUP;
			const u32* src_indices = cluster_indices.data() + c.index_offset;
			for (u32 j = 0; j < c.index_count; ++j) {
				out.indices.push(src_indices[j]);
				if (dilate_borders) out.index_positions.push(cluster_positions[src_indices[j]]);
			}
			// the bounds of the geometry (as it is in this cluster), for culling
			const ClusterSphere bounds = computeBounds(cluster_positions, &out.indices[cluster.first_index], cluster.index_count);
			cluster.center = bounds.center;
			cluster.radius = bounds.radius;
			// the bounds and error of the group that made the cluster, 0 for the original geometry. With dilation the bounds of the cluster are larger than
			// those of its group (they have the dilated vertices), the group is what the error is projected from.
			const DagBounds lod = c.refined >= 0 ? group_bounds[c.refined] : c.bounds;
			cluster.lod_center = lod.center;
			cluster.lod_radius = lod.radius;
			cluster.error = lod.error;
			cluster.parent_center = simplified.center;
			cluster.parent_radius = simplified.radius;
			cluster.parent_error = simplified.error;
			out.clusters.push(cluster);
		}
		group_bounds.push(simplified);
		return (int)group_id_counter++;
	};

	// Voxel levels (per-group switch). Each group switches from triangles to voxels independently when simplification stalls or its error reaches
	// `voxels->error * mesh_diagonal` or half its average triangle edge length. All groups share one power-of-2 grid hierarchy
	// (voxel_origin, size(k) = voxel_base_size * 2^k), so groups entering at different depths merge cleanly upward.
	Vec3 voxel_origin(0, 0, 0);
	float voxel_extent = 0.f;
	float voxel_base_size = 0.f;
	float voxel_error_threshold = FLT_MAX;
	const float voxel_min_coverage = voxels ? clamp(voxels->min_coverage, 0.f, 1.f) : VOXEL_MIN_COVERAGE;
	const float voxel_downsample_scale = voxels ? clamp(voxels->downsample_scale, 0.125f, 1.f) : 0.25f;
	const float voxel_stall_threshold = voxels ? clamp(voxels->stall_threshold, SIMPLIFY_RATIO, 1.f) : VOXEL_STALL_THRESHOLD;
	const float voxel_max_resolution = voxels ? clamp(float(voxels->max_resolution), 64.f, VOXEL_MAX_RESOLUTION) : VOXEL_MAX_RESOLUTION;
	if (voxels) {
		Vec3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (u32 i = 0; i < vertex_count; ++i) {
			mn = minimum(mn, cluster_positions[i]);
			mx = maximum(mx, cluster_positions[i]);
		}
		const Vec3 extent = mx - mn;
		const float mesh_diagonal = length(extent);
		const float max_extent = maximum(extent.x, maximum(extent.y, extent.z));
		// Leave margin around the initial AABB when border dilation is enabled so dilated vertices do not clamp to the grid walls.
		const float pad = dilate_borders ? max_extent * 0.05f : 0.f;
		voxel_origin = mn - Vec3(pad);
		voxel_extent = max_extent + pad * 2.f;
		if (voxels->error > 0.f && mesh_diagonal > 0.f) {
			voxel_error_threshold = voxels->error * mesh_diagonal;
		}
		if (voxel_extent > 0.f) {
			// Use the smallest leaf cluster's average edge so fine foliage is not coarsened by large trunk/wall triangles.
			float min_avg_edge = FLT_MAX;
			for (const DagCluster& c : clusters) {
				const float e = computeAvgTriangleEdge(cluster_positions, cluster_indices.data() + c.index_offset, c.index_count);
				if (e > 0.f) min_avg_edge = minimum(min_avg_edge, e);
			}
			if (min_avg_edge == FLT_MAX) min_avg_edge = 0.f;
			voxel_base_size = maximum(min_avg_edge * 0.5f, voxel_extent / voxel_max_resolution);
		}
	}
	const bool use_voxels = voxels != nullptr && voxel_extent > 0.f && voxel_base_size > 0.f;
	auto voxel_level_size = [&](u32 k) -> float { return voxel_base_size * float(1u << k); };
	auto voxel_level_error = [&](u32 k) -> float { return voxel_level_size(k) * VOXEL_HALF_DIAGONAL; };
	auto choose_voxel_level = [&](float min_error, float group_avg_edge) -> u32 {
		const float edge_error = group_avg_edge * 0.5f * VOXEL_HALF_DIAGONAL;
		const float target_error = maximum(min_error, edge_error);
		u32 k = 0;
		while (k < 20 && voxel_level_error(k) < target_error) ++k;
		// Snap to the nearest power-of-2 level in log space without ever violating DAG monotonicity (voxel_level_error >= min_error).
		if (k > 0 && voxel_level_error(k - 1) >= min_error && target_error < voxel_level_error(k - 1) * 1.41421356f) {
			--k;
		}
		return k;
	};
	Array<VoxelLevel> staged_voxel_levels(allocator);
	auto ensure_staged_level = [&](u32 k) -> VoxelLevel& {
		while ((u32)staged_voxel_levels.size() <= k) staged_voxel_levels.emplace(allocator);
		return staged_voxel_levels[k];
	};
	// emits the clusters [first, first + count) of the level as a group
	auto emit_voxel_group = [&](const VoxelLevel& voxel_level, u32 first, u32 count, const DagBounds& simplified, int group_depth, float size) -> int {
		begin_group(count, group_depth);
		for (u32 i = 0; i < count; ++i) {
			const VoxelCluster& c = voxel_level.clusters[first + i];
			MeshClusterGPU cluster;
			memset(&cluster, 0, sizeof(cluster));
			cluster.first_index = packVoxelCluster(out, voxel_level.voxels.begin() + c.first, c.count, voxel_origin, size);
			cluster.index_count = 0;
			cluster.group = group_id_counter;
			cluster.refined_group = (u32)c.refined;
			cluster.center = c.bounds.center;
			cluster.radius = c.bounds.radius;
			const DagBounds lod = group_bounds[c.refined];
			cluster.lod_center = lod.center;
			cluster.lod_radius = lod.radius;
			cluster.error = lod.error;
			cluster.parent_center = simplified.center;
			cluster.parent_radius = simplified.radius;
			cluster.parent_error = simplified.error;
			out.clusters.push(cluster);
		}
		group_bounds.push(simplified);
		return (int)group_id_counter++;
	};
	Array<Voxel> vox_temp(allocator), vox_temp_sort(allocator);
	auto voxelize_group_to_level = [&](const u32* tri_indices, u32 tri_index_count, int refined, u32 k) {
		const float size = voxel_level_size(k);
		const float error = voxel_level_error(k);
		const i32 max_coord = (i32)(voxel_extent / size) + 1;
		const VoxelSourceAttributes source = { cluster_positions, mesh_normals, voxels->colors, voxels->ao };
		vox_temp.clear();
		voxelizeTriangles(vox_temp, source, tri_indices, tri_index_count, voxel_origin, size, max_coord);
		sortAndMergeVoxels(vox_temp, vox_temp_sort);
		filterVoxelsByCoverage(vox_temp, voxel_min_coverage);
		if (!vox_temp.empty()) {
			appendVoxelClusters(ensure_staged_level(k), vox_temp, refined, voxel_origin, size, error);
		}
		else {
			group_bounds[refined].error = FLT_MAX;
			const MeshGroupGPU& g = out.groups[refined];
			for (u32 ci = 0; ci < g.cluster_count; ++ci) {
				out.clusters[g.first_cluster + ci].parent_error = FLT_MAX;
			}
		}
	};
	// Merges all staged per-group voxel levels upward into a single root brick.
	auto build_staged_voxel_levels = [&](int first_depth) {
		PROFILE_BLOCK("clod voxels");
		if (staged_voxel_levels.empty()) return;
		u32 first_k = (u32)staged_voxel_levels.size();
		u32 last_k = 0;
		for (u32 k = 0; k < (u32)staged_voxel_levels.size(); ++k) {
			if (!staged_voxel_levels[k].clusters.empty()) {
				first_k = minimum(first_k, k);
				last_k = maximum(last_k, k);
			}
		}
		if (first_k >= (u32)staged_voxel_levels.size()) return;

		auto has_more_than_one_brick = [&](const VoxelLevel& l) {
			if (l.clusters.empty()) return false;
			const VoxelCluster& c = l.clusters[0];
			const u64 brick = l.voxels[c.first].key >> 6;
			for (u32 i = 1; i < c.count; ++i) {
				if ((l.voxels[c.first + i].key >> 6) != brick) return true;
			}
			return false;
		};

		VoxelLevel cur(allocator), next(allocator), tmp_level(allocator);
		int voxel_depth = first_depth;
		for (u32 k = first_k; ; ++k) {
			if (k < (u32)staged_voxel_levels.size() && !staged_voxel_levels[k].clusters.empty()) {
				const VoxelLevel& src_lvl = staged_voxel_levels[k];
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
			const float size = voxel_level_size(k);
			sortVoxelLevel(cur, tmp_level, voxel_origin, size, allocator);
			if (k >= last_k && cur.clusters.size() <= 1 && !has_more_than_one_brick(cur)) {
				DagBounds bounds = cur.clusters[0].bounds;
				bounds.error = FLT_MAX;
				emit_voxel_group(cur, 0, 1, bounds, voxel_depth, size);
				++voxel_depth;
				break;
			}
			const float next_size = voxel_level_size(k + 1);
			const float next_error = voxel_level_error(k + 1);
			next.voxels.clear();
			next.clusters.clear();
			for (u32 first = 0; first < (u32)cur.clusters.size(); first += VOXEL_GROUP_CLUSTERS) {
				const u32 count = minimum(VOXEL_GROUP_CLUSTERS, (u32)cur.clusters.size() - first);
				const VoxelCluster* group = &cur.clusters[first];
				// Merge the children's LOD spheres (group_bounds[c.refined]) so the parent sphere always encloses every child's LOD sphere.
				DagBounds child_lods[VOXEL_GROUP_CLUSTERS];
				for (u32 ci = 0; ci < count; ++ci) child_lods[ci] = group_bounds[group[ci].refined];
				DagBounds bounds = dagBoundsMerge(child_lods, count, sizeof(DagBounds));
				bounds.error = next_error;
				const int id = emit_voxel_group(cur, first, count, bounds, voxel_depth, size);
				vox_temp.clear();
				const VoxelCluster& last = group[count - 1];
				downsampleVoxels(vox_temp, cur.voxels.begin() + group[0].first, last.first + last.count - group[0].first, voxel_downsample_scale);
				sortAndMergeVoxels(vox_temp, vox_temp_sort);
				filterVoxelsByCoverage(vox_temp, voxel_min_coverage);
				appendVoxelClusters(next, vox_temp, id, voxel_origin, next_size, next_error);
			}
			cur.voxels.swap(next.voxels);
			cur.clusters.swap(next.clusters);
			++voxel_depth;
		}
		u32 bricks = 0;
		for (const MeshVoxelCluster& v : out.voxel_clusters) bricks += v.brick_count;
		logInfo("Voxel levels (per-group): base size ", voxel_base_size, ", first_k ", first_k, " (size ", voxel_level_size(first_k), "), ", voxel_depth - first_depth, " levels, ", (u32)out.voxel_clusters.size()
			, " voxel clusters, ", bricks, " bricks, ", (u32)out.voxel_words.size() * 4, " bytes, triangle clusters below: ", (u32)out.clusters.size() - (u32)out.voxel_clusters.size());
	};

	Array<DagCluster> pending(allocator);
	Array<u32> pending_indices(allocator);
	pending.reserve(clusters.size());
	pending_indices.reserve(u32(cluster_indices.size() * SIMPLIFY_THRESHOLD));

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

	int depth = 0;
	while (clusters.size() > 1) {
		PROFILE_BLOCK("clod level");

		Array<u32> group_offsets(allocator);
		{
			PROFILE_BLOCK("clod partition");
			group_offsets = partitionClusters(clusters, cluster_positions, cluster_indices, remap);
		}

		pending.clear();
		pending_indices.clear();

		const u32 group_count = group_offsets.size() - 1;
		{
			PROFILE_BLOCK("clod lock boundary");
			lockBoundary(locks, clusters.data(), group_offsets.data(), group_count, cluster_indices, remap);
		}

		Array<GroupResult> results(allocator);
		results.reserve(group_count);
		for (u32 i = 0; i < group_count; ++i) results.emplace(allocator);

		auto simplify_and_classify_group = [&](const DagCluster* group, u32 group_size, GroupResult& res) -> bool {
			for (u32 j = 0; j < group_size; ++j) {
				appendSlice(res.merged, cluster_indices.data() + group[j].index_offset, group[j].index_count);
			}

			const u32 target_size = u32((res.merged.size() / 3) * SIMPLIFY_RATIO) * 3;
			DagBounds bounds = dagBoundsMerge(&group[0].bounds, group_size, sizeof(DagCluster));
			const float prev_error = bounds.error;

			float error = 0.f;
			{
				PROFILE_BLOCK("clod simplify");
				simplifyGroup(res.simplified, cluster_positions, vertex_count, mesh_normals, res.merged, locks, target_size, !use_voxels, &error);
			}

			if (use_voxels) {
				const float group_avg_edge = computeAvgTriangleEdge(cluster_positions, res.merged.data(), res.merged.size());
				const bool stalled = res.simplified.size() > res.merged.size() * voxel_stall_threshold || res.simplified.size() >= res.merged.size();
				const float next_error = maximum(prev_error, error);
				const float err_limit = voxels->error > 0.f ? voxel_error_threshold : group_avg_edge * 0.5f;
				if (stalled || next_error >= err_limit) {
					const u32 k = choose_voxel_level(prev_error, group_avg_edge);
					bounds.error = voxel_level_error(k);
					res.bounds = bounds;
					res.stuck = true;
					res.to_voxels = true;
					res.voxel_level = k;
					return false;
				}
			}
			else if (res.simplified.size() > res.merged.size() * SIMPLIFY_THRESHOLD) {
				bounds.error = FLT_MAX;
				res.bounds = bounds;
				res.stuck = true;
				return false;
			}

			bounds.error = maximum(bounds.error, error);
			res.bounds = bounds;
			res.stuck = false;
			return true;
		};

		PROFILE_BLOCK("clod groups");
		if (!dilate_borders) {
			jobs::forEach(group_count, 1, [&](u32 from, u32 to) {
				for (u32 i = from; i < to; ++i) {
					const DagCluster* group = clusters.data() + group_offsets[i];
					const u32 group_size = group_offsets[i + 1] - group_offsets[i];
					GroupResult& res = results[i];
					if (!simplify_and_classify_group(group, group_size, res)) continue;

					{
						PROFILE_BLOCK("clod clusterize group");
						clusterize(res.clusters, res.indices, cluster_positions, vertex_count, res.simplified.data(), res.simplified.size());
					}

					for (DagCluster& c : res.clusters) c.bounds = res.bounds;
				}
			});

			for (u32 i = 0; i < group_count; ++i) {
				const DagCluster* group = clusters.data() + group_offsets[i];
				const u32 group_size = group_offsets[i + 1] - group_offsets[i];
				GroupResult& res = results[i];

				const int refined = emit_group(group, group_size, res.bounds, depth);
				if (res.to_voxels) {
					voxelize_group_to_level(res.merged.data(), res.merged.size(), refined, res.voxel_level);
					continue;
				}
				if (res.stuck) continue;

				const u32 index_offset = pending_indices.size();
				appendSlice(pending_indices, res.indices.data(), res.indices.size());
				for (DagCluster& c : res.clusters) {
					c.index_offset += index_offset;
					c.refined = refined;
					pending.push(c);
				}
			}
		}
		else {
			// dilateBorders mutates dilated_positions in place, so emit_group must copy pre-dilation positions before dilation runs
			jobs::forEach(group_count, 1, [&](u32 from, u32 to) {
				for (u32 i = from; i < to; ++i) {
					const DagCluster* group = clusters.data() + group_offsets[i];
					const u32 group_size = group_offsets[i + 1] - group_offsets[i];
					simplify_and_classify_group(group, group_size, results[i]);
				}
			});

			for (u32 i = 0; i < group_count; ++i) {
				const DagCluster* group = clusters.data() + group_offsets[i];
				const u32 group_size = group_offsets[i + 1] - group_offsets[i];
				results[i].refined = emit_group(group, group_size, results[i].bounds, depth);
				if (results[i].to_voxels) {
					voxelize_group_to_level(results[i].merged.data(), results[i].merged.size(), results[i].refined, results[i].voxel_level);
				}
			}

			jobs::forEach(group_count, 1, [&](u32 from, u32 to) {
				Array<u64> dilate_table(allocator);
				for (u32 i = from; i < to; ++i) {
					GroupResult& res = results[i];
					if (res.stuck) continue;

					{
						PROFILE_BLOCK("clod dilate borders");
						dilateBorders(dilated_positions.begin(), res.merged, res.simplified, locks, remap, res.bounds, dilate_offsets, dilate_table);
					}

					{
						PROFILE_BLOCK("clod clusterize group");
						clusterize(res.clusters, res.indices, cluster_positions, vertex_count, res.simplified.data(), res.simplified.size(), res.refined);
					}

					for (DagCluster& c : res.clusters) c.bounds = res.bounds;
				}
			});

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
		}

		clusters.swap(pending);
		cluster_indices.swap(pending_indices);
		depth++;
	}

	if (!clusters.empty()) {
		ASSERT(clusters.size() == 1);
		if (use_voxels) {
			const float avg_edge = computeAvgTriangleEdge(cluster_positions, cluster_indices.data() + clusters[0].index_offset, clusters[0].index_count);
			const u32 k = choose_voxel_level(clusters[0].bounds.error, avg_edge);
			DagBounds bounds = clusters[0].bounds;
			bounds.error = voxel_level_error(k);
			const int refined = emit_group(clusters.data(), clusters.size(), bounds, depth);
			voxelize_group_to_level(cluster_indices.data() + clusters[0].index_offset, clusters[0].index_count, refined, k);
			depth++;
		}
		else {
			DagBounds bounds = clusters[0].bounds;
			bounds.error = FLT_MAX;
			emit_group(clusters.data(), clusters.size(), bounds, depth);
		}
	}
	if (use_voxels) build_staged_voxel_levels(depth);

	if (!leaves_done) {
		out.leaf_index_count = out.indices.size();
		out.leaf_cluster_count = out.clusters.size();
	}
	close_level();
	// the trees over the clusters of the levels: the finest clusters, then the ones that each level has made
	{
		PROFILE_BLOCK("build cluster nodes");
		buildNodes(out, 0, out.leaf_cluster_count);
		for (u32 i = 0; i + 1 < (u32)level_ranges.size(); i += 2) buildNodes(out, level_ranges[i], level_ranges[i + 1]);
	}

	return true;
}

static constexpr float QUAT_RANGE = 0.70710678f; // the three smaller components of a unit quaternion are not larger than 1 / sqrt(2)

u32 packClusterFrame(const Vec3& normal, const Vec3* tangent) {
	Vec3 n = normal;
	const float normal_length = length(n);
	n = normal_length > 0 ? n * (1.f / normal_length) : Vec3(0, 1, 0);

	// the tangent is made perpendicular to the normal, which stays exactly as it is
	Vec3 t = tangent ? *tangent : Vec3(0, 0, 0);
	t = t - n * dot(n, t);
	float tangent_length = length(t);
	if (tangent_length < 1e-4f) {
		t = fabsf(n.x) < 0.9f ? cross(n, Vec3(1, 0, 0)) : cross(n, Vec3(0, 1, 0));
		tangent_length = length(t);
	}
	t = t * (1.f / tangent_length);
	const Vec3 b = cross(n, t);

	// rotation with columns t, b, n (a right handed frame), as a quaternion (x, y, z, w)
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

	// smallest three: the largest component is left out (it is positive, the sign of the whole quaternion does not matter)
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

	// The lattice: a power of two, so that integers are exact in float and in the shader. Integers must be exact in float too, so they are below 2^24.
	// (Clusters with dilated borders have positions of their own, out.index_positions, they are in the lattice too.)
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

	Array<u32> local_of(allocator);	// mesh vertex -> vertex of the cluster that is processed
	local_of.resize(vertex_count);
	for (u32& l : local_of) l = INVALID_INDEX;
	Array<u32> vertices(allocator);	// vertices of the cluster that is processed
	Array<i32> cluster_lattice(allocator);	// the positions of the vertices of the cluster, 3 for each, in the lattice

	// the stream has a piece for each group, every piece starts at a 32 bit boundary (it is what the address of a group points to), the headers are not in it
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
		if (cluster.index_count == 0) {
			// a cluster of voxels, its payload is in the stream of the group, see MeshVoxelCluster
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
			MeshClusterVertexGPU header;
			memset(&header, 0, sizeof(header));
			header.pos_start[0] = voxel.base[0];
			header.pos_start[1] = voxel.base[1];
			header.pos_start[2] = voxel.base[2];
			header.vertex_count = voxel.brick_count;
			header.pos_bit_offset = (u32)(bit_cursor - group_start_bit);
			header.pos_step = voxel.size;
			header.uv_min[0] = voxel.origin.x;
			header.uv_min[1] = voxel.origin.y;
			header.uv_extent[0] = voxel.origin.z;
			for (u32 w = 0; w < voxel.word_count; ++w) write_bits(out.voxel_words[voxel.first_word + w], 32);
			out.vertex_headers.push(header);
			continue;
		}
		vertices.clear();
		cluster_lattice.clear();
		for (u32 k = 0; k < cluster.index_count; ++k) {
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

		// UVs: the fewest bits per axis that keep the step at most UV_STEP, at most 16
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

		// bits of a vertex after its position: the frame, UV, AO (only the first of 4 channels) and the color
		const u32 attribute_bits = 32 + (source.uvs ? uv_bits[0] + uv_bits[1] : 0) + (source.ao ? 8 : 0) + (source.colors ? 32 : 0);
		header.pos_bits = bits[0] | (bits[1] << 8) | (bits[2] << 16) | (attribute_bits << 24);
		if (bit_cursor + u64((u32)vertices.size()) * (bits[0] + bits[1] + bits[2] + attribute_bits) >= (1ull << 32)) {
			logError("Vertices of clusters are larger than 512 MB");
			return false;
		}

		for (u32 vi = 0; vi < (u32)vertices.size(); ++vi) {
			const u32 g = vertices[vi];
			for (u32 a = 0; a < 3; ++a) write_bits(u32(cluster_lattice[vi * 3 + a] - mn[a]), bits[a]);

			// the attributes follow the position of the vertex, in the same bit stream
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


// edges key[i] -> value[i] as a list for each key, without duplicates: the values of the key `k` are list[first[k] .. first[k] + count[k])
static void buildAdjacency(const Array<u32>& key, const Array<u32>& value, u32 key_count, Array<u32>& first, Array<u32>& count, Array<u32>& list) {
	first.resize(key_count + 1);
	count.resize(key_count);
	for (u32& f : first) f = 0;
	for (u32& c : count) c = 0;
	for (u32 k : key) ++first[k + 1];
	for (u32 i = 0; i < key_count; ++i) first[i + 1] += first[i];
	list.resize(key.size());
	for (u32 i = 0; i < (u32)key.size(); ++i) {
		const u32 k = key[i];
		bool exists = false;
		for (u32 j = 0; j < count[k]; ++j) exists = exists || list[first[k] + j] == value[i];
		if (!exists) list[first[k] + count[k]++] = value[i];
	}
}

bool buildMeshPages(const MeshClusterGPU* clusters, u32 cluster_count, const MeshGroupGPU* groups, u32 group_count, MeshPages& out, IAllocator& allocator) {
	PROFILE_FUNCTION();
	out.pages.clear();
	out.parents.clear();
	out.children.clear();
	out.root_bytes = 0;
	if (group_count == 0) return false;
	Array<u32> group_index_bytes(allocator);
	group_index_bytes.resize(group_count);
	for (u32& b : group_index_bytes) b = 0;
	for (u32 i = 0; i < cluster_count; ++i) {
		if (clusters[i].group >= group_count) return false;
		if (clusters[i].refined_group != MeshClusters::NO_GROUP && clusters[i].refined_group >= group_count) return false;
		group_index_bytes[clusters[i].group] += clusters[i].index_count;
	}

	// Permanent groups are never unloaded:
	//  - the groups that have no parent (their clusters have no parent error): nothing coarser covers their part of the mesh, so without them the mesh would have holes
	//    that nothing would ever ask for. They are the coarsest level, or groups that could not be simplified, whatever their size,
	//  - and the coarsest of the others, as long as the permanent groups are not larger than PERMANENT_BYTES (the groups of a mesh are in the order of the levels, the
	//    parents of a group have higher numbers).
	auto payload_bytes = [&](u32 g) -> u64 { return u64(groups[g].stream_size) + u64(group_index_bytes[g]); };
	Array<u8> permanent(allocator);
	permanent.resize(group_count);
	for (u8& p : permanent) p = 0;
	for (u32 i = 0; i < cluster_count; ++i) {
		if (clusters[i].parent_error >= 1e30f) permanent[clusters[i].group] = 1;
	}
	u64 permanent_bytes = 0;
	for (u32 g = 0; g < group_count; ++g) {
		if (permanent[g]) permanent_bytes += payload_bytes(g);
	}
	out.root_bytes = (u32)minimum(permanent_bytes, (u64)0xffFFffFFu);
	for (u32 g = group_count; g > 0;) {
		--g;
		if (permanent[g]) continue;
		const u64 bytes = payload_bytes(g);
		if (permanent_bytes + bytes > ClusterStream::PERMANENT_BYTES) break;
		permanent[g] = 1;
		permanent_bytes += bytes;
	}

	Array<u32> group_page(allocator);
	group_page.resize(group_count);
	u64 page_bytes = 0;
	for (u32 g = 0; g < group_count; ++g) {
		const u64 bytes = payload_bytes(g);
		const u32 is_permanent = permanent[g] ? 1 : 0;
		if (out.pages.empty() || out.pages[out.pages.size() - 1].permanent != is_permanent || (page_bytes > 0 && page_bytes + bytes > ClusterStream::PAGE_BYTES)) {
			out.pages.push(MeshPageFile{g, 0, is_permanent, 0, 0, 0, 0});
			page_bytes = 0;
		}
		out.pages[out.pages.size() - 1].group_count++;
		group_page[g] = (u32)out.pages.size() - 1;
		page_bytes += bytes;
	}

	// The priority of a page: the largest error that it removes, that is the parent error of the clusters of its groups (the error of what is drawn until the page is loaded).
	// Non-negative floats compare as integers. Permanent pages are never requested.
	Array<float> page_error(allocator);
	page_error.resize((u32)out.pages.size());
	for (float& e : page_error) e = 0;
	for (u32 i = 0; i < cluster_count; ++i) {
		const float e = clusters[i].parent_error;
		if (e >= 1e30f || !(e > 0)) continue;
		float& page_max = page_error[group_page[clusters[i].group]];
		page_max = maximum(page_max, e);
	}
	for (u32 p = 0; p < (u32)out.pages.size(); ++p) {
		if (out.pages[p].permanent) {
			out.pages[p].priority = 0xffFFffFFu;
			continue;
		}
		memcpy(&out.pages[p].priority, &page_error[p], sizeof(u32));
	}

	// the edges between the pages (a page is loaded and unloaded as a whole, so the ones inside of a page are not needed)
	Array<u32> child_page(allocator), parent_page(allocator);
	for (u32 i = 0; i < cluster_count; ++i) {
		if (clusters[i].refined_group == MeshClusters::NO_GROUP) continue;
		const u32 c = group_page[clusters[i].refined_group];	// the finer clusters
		const u32 p = group_page[clusters[i].group];			// the group that they were simplified to
		if (c == p) continue;
		if (p < c) return false;	// not in the order of the levels, the streamer needs it to know that there is no cycle
		child_page.push(c);
		parent_page.push(p);
	}
	Array<u32> first(allocator), count(allocator), list(allocator);
	buildAdjacency(child_page, parent_page, (u32)out.pages.size(), first, count, list);
	for (u32 p = 0; p < (u32)out.pages.size(); ++p) {
		out.pages[p].parent_first = (u32)out.parents.size();
		out.pages[p].parent_count = count[p];
		for (u32 j = 0; j < count[p]; ++j) {
			out.parents.push(list[first[p] + j]);
			++out.pages[list[first[p] + j]].child_count;
		}
	}
	// the same edges the other way round
	u32 child_at = 0;
	for (MeshPageFile& page : out.pages) {
		page.child_first = child_at;
		child_at += page.child_count;
		page.child_count = 0;	// counted again while the children are added
	}
	out.children.resize(out.parents.size());
	for (u32 p = 0; p < (u32)out.pages.size(); ++p) {
		for (u32 j = 0; j < out.pages[p].parent_count; ++j) {
			MeshPageFile& parent = out.pages[out.parents[out.pages[p].parent_first + j]];
			out.children[parent.child_first + parent.child_count++] = p;
		}
	}
	return true;
}

} // namespace Lumix
