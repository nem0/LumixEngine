#include "cluster_builder.h"

#include "core/crt.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/string.h"

#include "meshoptimizer/meshoptimizer.h"

#include <float.h>

namespace Lumix {

namespace {

constexpr u32 MAX_CLUSTER_VERTICES = 64;
constexpr u32 MAX_CLUSTER_TRIANGLES = 128;
constexpr u32 CLUSTERS_PER_GROUP = 4;
constexpr u32 MAX_GROUP_SIZE = 32;	// clusters in a group, the upper levels have small clusters
constexpr u32 INVALID_INDEX = 0xffFFffFF;

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

// sphere containing all the input spheres
ClusterSphere mergeSpheres(const Array<MeshClusterGPU>& clusters, const Array<u32>& ids, u32 from, u32 count) {
	ClusterSphere res;
	res.center = Vec3(0, 0, 0);
	for (u32 i = 0; i < count; ++i) res.center += clusters[ids[from + i]].lod_center;
	res.center = res.center * (1.f / count);
	res.radius = 0;
	for (u32 i = 0; i < count; ++i) {
		const MeshClusterGPU& c = clusters[ids[from + i]];
		res.radius = maximum(res.radius, length(c.lod_center - res.center) + c.lod_radius);
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

struct Builder {
	Builder(IAllocator& allocator, MeshClusters& out) : allocator(allocator), out(out) {}

	// Splits triangles into clusters and appends them to `out`.
	// `positions` and `indices` can be a local copy of a part of the mesh, `global_of_local` then maps local vertex indices to the mesh's ones.
	void addClusters(const Vec3* positions, u32 vertex_count, const u32* indices, u32 index_count, const u32* global_of_local, const Vec3* global_positions, float error, float attribute_error, const ClusterSphere* lod, Array<u32>& ids) {
		const size_t max_meshlets = meshopt_buildMeshletsBound(index_count, MAX_CLUSTER_VERTICES, MAX_CLUSTER_TRIANGLES);
		Array<meshopt_Meshlet> meshlets(allocator);
		Array<u32> meshlet_vertices(allocator);
		Array<u8> meshlet_triangles(allocator);
		meshlets.resize((u32)max_meshlets);
		meshlet_vertices.resize((u32)max_meshlets * MAX_CLUSTER_VERTICES);
		meshlet_triangles.resize((u32)max_meshlets * MAX_CLUSTER_TRIANGLES * 3);

		const size_t count = meshopt_buildMeshlets(meshlets.begin(), meshlet_vertices.begin(), meshlet_triangles.begin(), indices, index_count, &positions[0].x, vertex_count, sizeof(Vec3), MAX_CLUSTER_VERTICES, MAX_CLUSTER_TRIANGLES, 0.f);
		for (size_t i = 0; i < count; ++i) {
			const meshopt_Meshlet& m = meshlets[(u32)i];
			MeshClusterGPU c;
			memset(&c, 0, sizeof(c));
			c.first_index = out.indices.size();
			c.index_count = m.triangle_count * 3;
			for (u32 j = 0; j < c.index_count; ++j) {
				const u32 local = meshlet_vertices[m.vertex_offset + meshlet_triangles[m.triangle_offset + j]];
				out.indices.push(global_of_local ? global_of_local[local] : local);
			}
			const ClusterSphere bounds = computeBounds(global_positions, &out.indices[c.first_index], c.index_count);
			c.center = bounds.center;
			c.radius = bounds.radius;
			c.error = error;
			c.attribute_error = attribute_error;
			c.lod_center = lod ? lod->center : bounds.center;
			c.lod_radius = lod ? lod->radius : bounds.radius;
			c.parent_error = FLT_MAX;
			// normal cone, for the culling of back facing clusters
			const meshopt_Bounds cone = meshopt_computeMeshletBounds(&meshlet_vertices[m.vertex_offset], &meshlet_triangles[m.triangle_offset], m.triangle_count, &positions[0].x, vertex_count, sizeof(Vec3));
			c.cone_axis = Vec3(cone.cone_axis[0], cone.cone_axis[1], cone.cone_axis[2]);
			c.cone_cutoff = cone.cone_cutoff;
			ids.push(out.clusters.size());
			out.clusters.push(c);
		}
	}

	IAllocator& allocator;
	MeshClusters& out;
};

} // anonymous namespace

bool buildMeshClusters(Span<const Vec3> positions, Span<const Vec3> normals, Span<const Vec2> uvs, Span<const u32> indices, MeshClusters& out, IAllocator& allocator) {
	PROFILE_FUNCTION();
	if (indices.length() < 3 || positions.length() == 0) return false;
	const u32 vertex_count = positions.length();
	const Vec3* pos = positions.begin();

	Builder builder(allocator, out);
	Array<u32> current(allocator);
	builder.addClusters(pos, vertex_count, indices.begin(), indices.length(), nullptr, pos, 0.f, 0.f, nullptr, current);
	out.leaf_index_count = out.indices.size();
	const u32 leaf_clusters = current.size();
	out.leaf_cluster_count = leaf_clusters;
	Array<u32> level_ranges(allocator);	// [first cluster, number of clusters] of the levels, the clusters that a level has made
	// how many leaf clusters can be culled by their normal cone at all (cutoff 1: the normals spread too much), and how many triangles are in them
	u32 cone_clusters = 0, cone_triangles = 0, leaf_triangles = 0;
	for (u32 id : current) {
		const MeshClusterGPU& c = out.clusters[id];
		leaf_triangles += c.index_count / 3;
		if (c.cone_cutoff < 1.f) { ++cone_clusters; cone_triangles += c.index_count / 3; }
	}
	u32 stuck_groups = 0, simplified_groups = 0, levels = 0;
	double position_error_sum = 0, attribute_error_sum = 0;	// of the simplified groups, to see if the attributes dominate the error

	// Vertices of meshes are split by normals and UVs (flat shading, atlas tiles), so that the surface is open in index space at every
	// seam. The simplifier treats that as borders, or at least restricts collapses at them, and groups can't be simplified. So the grouping and
	// the simplification work on a mesh whose vertices are welded by position (up to 0.1 mm). Triangles of coarse clusters still reference
	// vertices of the mesh, see the choice of wedges below.
	Array<u32> welded(allocator); // mesh vertex -> welded vertex
	Array<Vec3> welded_positions(allocator);
	u32 welded_count = 0;
	{
		auto quantize = [](float v) { return floorf(v * 10000.f + 0.5f) / 10000.f + 0.f; }; // + 0 turns -0 into 0, the welding compares bits
		Array<Vec3> quantized(allocator);
		quantized.resize(vertex_count);
		for (u32 i = 0; i < vertex_count; ++i) quantized[i] = Vec3(quantize(pos[i].x), quantize(pos[i].y), quantize(pos[i].z));
		welded.resize(vertex_count);
		welded_count = (u32)meshopt_generateVertexRemap(welded.begin(), nullptr, vertex_count, quantized.begin(), vertex_count, sizeof(Vec3));
		welded_positions.resize(welded_count);
		for (u32 i = 0; i < vertex_count; ++i) welded_positions[welded[i]] = pos[i];
	}

	// All the mesh vertices (wedges) at every welded vertex. Simplified triangles choose from all of them, not only from the ones that the
	// triangles of the level below used: a wrong choice there would lose the right wedge for the levels above.
	const bool use_global_wedges = normals.length() == vertex_count;
	Array<u32> wedge_start(allocator);	// per welded vertex, into wedge_list
	Array<u32> wedge_list(allocator);
	Array<u8> welded_seam(allocator);	// the wedges of the vertex differ in normal or UV
	if (use_global_wedges) {
		wedge_start.resize(welded_count + 1);
		for (u32& c : wedge_start) c = 0;
		for (u32 i = 0; i < vertex_count; ++i) ++wedge_start[welded[i] + 1];
		for (u32 i = 0; i < welded_count; ++i) wedge_start[i + 1] += wedge_start[i];
		wedge_list.resize(vertex_count);
		Array<u32> cursor(allocator);
		cursor.resize(welded_count);
		for (u32 i = 0; i < welded_count; ++i) cursor[i] = wedge_start[i];
		for (u32 i = 0; i < vertex_count; ++i) wedge_list[cursor[welded[i]]++] = i;
		welded_seam.resize(welded_count);
		for (u32 w = 0; w < welded_count; ++w) {
			welded_seam[w] = 0;
			const u32 first = wedge_list[wedge_start[w]];
			for (u32 j = wedge_start[w] + 1; j < wedge_start[w + 1]; ++j) {
				const u32 v = wedge_list[j];
				if (dot(normals[v], normals[first]) < 0.9999f || (uvs.length() == vertex_count && length(uvs[v] - uvs[first]) > 1e-5f)) welded_seam[w] = 1;
			}
		}
	}

	Array<u32> remap(allocator); // welded vertex -> group local vertex, only valid while a group is processed
	remap.resize(welded_count);
	for (u32& r : remap) r = INVALID_INDEX;

	for (u32 level = 0; level < 64 && current.size() > 1; ++level) {
		// group neighbouring clusters
		Array<u32> cluster_indices(allocator);
		Array<u32> cluster_index_counts(allocator);
		for (u32 id : current) {
			const MeshClusterGPU& c = out.clusters[id];
			cluster_index_counts.push(c.index_count);
			for (u32 i = 0; i < c.index_count; ++i) cluster_indices.push(welded[out.indices[c.first_index + i]]);
		}

		Array<u32> partition_of(allocator);
		partition_of.resize(current.size());
		// A group has about as many triangles as CLUSTERS_PER_GROUP full clusters. The clusters of the upper levels are small, a group of 4 of
		// them has almost only vertices on its border, which are locked, and can not be simplified.
		const u32 average_triangles = maximum(cluster_indices.size() / 3 / maximum((u32)current.size(), 1u), 1u);
		const u32 group_size = clamp(CLUSTERS_PER_GROUP * MAX_CLUSTER_TRIANGLES / average_triangles, CLUSTERS_PER_GROUP, MAX_GROUP_SIZE);
		u32 partition_count = (u32)meshopt_partitionClusters(partition_of.begin(), cluster_indices.begin(), cluster_indices.size(), cluster_index_counts.begin(), current.size(), &welded_positions[0].x, welded_count, sizeof(Vec3), group_size);

		// what is left fits into one group: it is one, there is no neighbour to match the border of, the border is not locked (see below)
		if (current.size() <= MAX_GROUP_SIZE) {
			for (u32& p : partition_of) p = 0;
			partition_count = 1;
		}

		// sort clusters by partition
		Array<u32> partition_start(allocator);
		partition_start.resize(partition_count + 1);
		for (u32& s : partition_start) s = 0;
		for (u32 p : partition_of) ++partition_start[p + 1];
		for (u32 i = 0; i < partition_count; ++i) partition_start[i + 1] += partition_start[i];
		Array<u32> sorted(allocator);
		sorted.resize(current.size());
		{
			Array<u32> cursor(allocator);
			cursor.resize(partition_count);
			for (u32 i = 0; i < partition_count; ++i) cursor[i] = partition_start[i];
			for (u32 i = 0; i < (u32)current.size(); ++i) sorted[cursor[partition_of[i]]++] = current[i];
		}

		Array<u32> next(allocator);
		bool progress = false;
		const u32 level_first_cluster = out.clusters.size();
		// errors of the groups of this level, logged for big meshes: they show where the error grows
		float level_error = 0, level_position_error = 0, level_attribute_error = 0, level_error_sum = 0;
		u32 level_groups = 0, level_stuck = 0, level_triangles = 0, level_degenerate = 0, level_logged = 0;
		const u32 level_input = current.size();
		for (u32 p = 0; p < partition_count; ++p) {
			const u32 from = partition_start[p];
			const u32 member_count = partition_start[p + 1] - from;
			if (member_count == 0) continue;

			auto carry_over = [&]() { for (u32 i = 0; i < member_count; ++i) next.push(sorted[from + i]); };
			if (member_count < 2) {
				carry_over();
				continue;
			}

			// local copy of the group (welded vertices), so that the cost of simplification does not depend on the size of the whole mesh
			Array<u32> welded_of_local(allocator);
			Array<Vec3> local_positions(allocator);
			Array<u32> local_indices(allocator);
			// the mesh vertices (wedges) that the triangles of the group use at a local vertex, with the normal of the triangle:
			// the wedge a coarse triangle takes is the one of the most similar triangle
			Array<i32> first_wedge(allocator);	// per local vertex, -1 if none
			Array<i32> next_wedge(allocator);
			Array<u32> wedge_vertex(allocator);
			Array<Vec3> wedge_normal(allocator);
			// local vertices that are on a seam, the triangles of the group use more than one mesh vertex there. Only there a simplified triangle can
			// take attributes that do not belong to it. Continuous attributes (smooth surfaces) are interpolated over the simplified triangles, that
			// is a part of the shape and the position error already measures it.
			Array<u32> first_vertex(allocator);
			Array<u8> seam(allocator);
			Array<float> uv_ratios(allocator);	// UV length / length of the edge, of the edges of the triangles of the group
			for (u32 i = 0; i < member_count; ++i) {
				const MeshClusterGPU& c = out.clusters[sorted[from + i]];
				for (u32 j = 0; j + 2 < c.index_count; j += 3) {
					const u32 tri[3] = { out.indices[c.first_index + j], out.indices[c.first_index + j + 1], out.indices[c.first_index + j + 2] };
					Vec3 n = cross(pos[tri[1]] - pos[tri[0]], pos[tri[2]] - pos[tri[0]]);
					const float len = length(n);
					n = len > 0 ? n * (1.f / len) : Vec3(0, 0, 0);
					if (uvs.length()) {
						for (u32 k = 0; k < 3; ++k) {
							const float edge_length = length(pos[tri[k]] - pos[tri[(k + 1) % 3]]);
							if (edge_length > 1e-6f) uv_ratios.push(length(uvs[tri[k]] - uvs[tri[(k + 1) % 3]]) / edge_length);
						}
					}
					for (u32 k = 0; k < 3; ++k) {
						const u32 w = welded[tri[k]];
						if (remap[w] == INVALID_INDEX) {
							remap[w] = welded_of_local.size();
							welded_of_local.push(w);
							local_positions.push(welded_positions[w]);
							first_wedge.push(-1);
							first_vertex.push(tri[k]);
							seam.push(use_global_wedges ? welded_seam[w] : 0);
						}
						const u32 local = remap[w];
						if (!use_global_wedges && first_vertex[local] != tri[k]) seam[local] = 1;
						local_indices.push(local);
						next_wedge.push(first_wedge[local]);
						first_wedge[local] = next_wedge.size() - 1;
						wedge_vertex.push(tri[k]);
						wedge_normal.push(n);
					}
				}
			}
			for (u32 w : welded_of_local) remap[w] = INVALID_INDEX;

			Array<u32> simplified(allocator);
			simplified.resize(local_indices.size());
			const size_t target_count = (local_indices.size() / 2 / 3) * 3;
			float simplify_error = 0;
			const size_t simplified_count = meshopt_simplify(simplified.begin(), local_indices.begin(), local_indices.size(), &local_positions[0].x, local_positions.size(), sizeof(Vec3), target_count, FLT_MAX, (partition_count == 1 ? 0u : (u32)meshopt_SimplifyLockBorder) | meshopt_SimplifyErrorAbsolute, &simplify_error); // the only group has no neighbours that its border has to match
			if (simplified_count < 3 || simplified_count > local_indices.size() * 0.95f) {
				// stuck, the clusters stay as they are and are tried again with different neighbours in the next level
				++stuck_groups;
				++level_stuck;
				carry_over();
				continue;
			}
			++simplified_groups;

			const ClusterSphere group_sphere = mergeSpheres(out.clusters, sorted, from, member_count);

			// back to vertices of the mesh: each corner takes the wedge whose triangle is the most similar to the simplified one
			// and the attributes of the chosen wedges are compared to what they should be, that is a part of the error of the group
			// UVs per world unit of the triangles of the group: the median, a few edges that jump to another place of the atlas do not matter
			float uv_per_unit = 0;
			if (uv_ratios.size() > 0) {
				sort(uv_ratios.begin(), uv_ratios.end());
				uv_per_unit = uv_ratios[uv_ratios.size() / 2];
			}
			float normal_error = 0;	// the largest angle between the normal of a chosen wedge and the normal of its simplified triangle, radians
			float uv_error = 0;		// the largest difference of the UV length of an edge of a simplified triangle to the expected one, world units
			// The error of attributes of the group: wrong attributes are wrong on the triangle that has them, and no wider than it: a sliver along a
			// crease can have a wrong normal and UVs of the other side, but it is as wide as its smallest height, so that is how far the error shows.
			// (Measured against the average edge of the group, that was a sliver of 0.0006 that made the whole level inaccurate.)
			float attribute_error = 0;
			Array<u32> mesh_indices(allocator);
			mesh_indices.reserve((u32)simplified_count);
			for (u32 t = 0; t + 2 < (u32)simplified_count; t += 3) {
				Vec3 n = cross(local_positions[simplified[t + 1]] - local_positions[simplified[t]], local_positions[simplified[t + 2]] - local_positions[simplified[t]]);
				const float len = length(n);
				n = len > 0 ? n * (1.f / len) : Vec3(0, 0, 0);
				++level_triangles;
				if (len == 0) {
					// no area, it is not seen and has no normal to choose a wedge by. Dropped, so that its arbitrary wedges do not get to the next level
					++level_degenerate;
					continue;
				}
				u32 corner[3];
				float triangle_angle = 0;	// of this triangle, as normal_error
				float triangle_uv = 0;		// and uv_error
				for (u32 k = 0; k < 3; ++k) {
					float best_dot = -FLT_MAX;
					if (use_global_wedges) {
						// the wedge whose normal is the closest to the normal of the simplified triangle
						const u32 w = welded_of_local[simplified[t + k]];
						corner[k] = wedge_list[wedge_start[w]];
						for (u32 j = wedge_start[w]; j < wedge_start[w + 1]; ++j) {
							const float d = dot(normals[wedge_list[j]], n);
							if (d > best_dot) { best_dot = d; corner[k] = wedge_list[j]; }
						}
					}
					else {
						i32 best = -1;
						for (i32 w = first_wedge[simplified[t + k]]; w >= 0; w = next_wedge[w]) {
							const float d = dot(wedge_normal[w], n);
							if (d > best_dot) { best_dot = d; best = w; }
						}
						corner[k] = wedge_vertex[best];
					}
					if (normals.length() && len > 0 && seam[simplified[t + k]]) {
						const float angle = acosf(clamp(dot(normals[corner[k]], n), -1.f, 1.f));
						triangle_angle = maximum(triangle_angle, angle);
						normal_error = maximum(normal_error, angle);
					}
				}
				mesh_indices.push(corner[0]);
				mesh_indices.push(corner[1]);
				mesh_indices.push(corner[2]);
				if (uvs.length() && uv_per_unit > 0) {
					for (u32 k = 0; k < 3; ++k) {
						if (!seam[simplified[t + k]] && !seam[simplified[t + (k + 1) % 3]]) continue;
						const u32 a = corner[k], b = corner[(k + 1) % 3];
						const float uv_length = length(uvs[a] - uvs[b]);
						const float expected = uv_per_unit * length(pos[a] - pos[b]);
						const float mismatch = fabsf(uv_length - expected) / uv_per_unit;
						triangle_uv = maximum(triangle_uv, mismatch);
						uv_error = maximum(uv_error, mismatch);
					}
				}
				if (triangle_angle > 0 || triangle_uv > 0) {
					// the smallest height of the triangle is its area (len / 2) over its longest edge
					const float longest = maximum(length(local_positions[simplified[t + 1]] - local_positions[simplified[t]]), maximum(length(local_positions[simplified[t + 2]] - local_positions[simplified[t + 1]]), length(local_positions[simplified[t]] - local_positions[simplified[t + 2]])));
					const float height = longest > 0 ? len / longest : 0.f;
					const float severity = maximum(minimum(triangle_angle, 1.f), longest > 0 ? minimum(triangle_uv / longest, 1.f) : 0.f);
					attribute_error = maximum(attribute_error, severity * height);
				}
			}
			if (mesh_indices.size() < 3) {
				// nothing left but triangles without an area
				--simplified_groups;
				++stuck_groups;
				++level_stuck;
				carry_over();
				continue;
			}

			// (for the log:)
			float edge_sum = 0;
			for (u32 t = 0; t + 2 < (u32)simplified_count; ++t) edge_sum += length(local_positions[simplified[t + (t % 3 == 2 ? -2 : 1)]] - local_positions[simplified[t]]);
			const float average_edge = (float)(edge_sum / maximum((u32)simplified_count, 1u));
			if (leaf_clusters >= 2000 && attribute_error > 1e-3f && level_logged < 2) {
				// the groups that have a large error of attributes
				++level_logged;
				u32 seam_vertices = 0;
				for (u8 v : seam) seam_vertices += v;
				logInfo("    level ", level, " group of ", member_count, " clusters: normal angle ", normal_error, ", uv error ", uv_error, ", uv per unit ", uv_per_unit, ", average edge ", average_edge, ", attribute error ", attribute_error,
					", seam vertices ", seam_vertices, " of ", (u32)seam.size(), ", position error ", simplify_error, ", triangles in ", (u32)local_indices.size() / 3, " out ", (u32)mesh_indices.size() / 3);
			}

			// The errors of positions add up from level to level. The error of attributes is measured on each level, it does not add up, but it
			// does not get smaller either: the group has at least the error of attributes of its children.
			float group_error = 0;
			float group_attribute_error = attribute_error;
			for (u32 i = 0; i < member_count; ++i) {
				group_error = maximum(group_error, out.clusters[sorted[from + i]].error);
				group_attribute_error = maximum(group_attribute_error, out.clusters[sorted[from + i]].attribute_error);
			}
			group_error += simplify_error;
			++level_groups;
			level_error = maximum(level_error, group_error + group_attribute_error);	// with the weight 1
			level_error_sum += group_error + group_attribute_error;
			level_position_error = maximum(level_position_error, simplify_error);
			level_attribute_error = maximum(level_attribute_error, attribute_error);
			position_error_sum += simplify_error;
			attribute_error_sum += attribute_error;

			for (u32 i = 0; i < member_count; ++i) {
				MeshClusterGPU& c = out.clusters[sorted[from + i]];
				c.parent_center = group_sphere.center;
				c.parent_radius = group_sphere.radius;
				c.parent_error = group_error;
				c.parent_attribute_error = group_attribute_error;
			}

			builder.addClusters(pos, vertex_count, mesh_indices.begin(), (u32)mesh_indices.size(), nullptr, pos, group_error, group_attribute_error, &group_sphere, next);
			progress = true;
		}

		if (leaf_clusters >= 2000) logInfo("  level ", level, ": ", level_input, " clusters, ", level_groups, " groups simplified, ", level_stuck, " stuck, group error: largest ", level_error, ", mean ", (float)(level_error_sum / maximum(level_groups, 1u)), ", largest of positions ", level_position_error, ", of attributes ", level_attribute_error, ", simplified triangles ", level_triangles, " of them without an area ", level_degenerate);
		if ((u32)out.clusters.size() > level_first_cluster) {
			level_ranges.push(level_first_cluster);
			level_ranges.push((u32)out.clusters.size() - level_first_cluster);
		}
		++levels;
		if (!progress) break;
		current.swap(next);
	}

	// the trees over the clusters of the levels: the finest clusters, then the ones that each level has made
	buildNodes(out, 0, leaf_clusters);
	for (u32 i = 0; i + 1 < (u32)level_ranges.size(); i += 2) buildNodes(out, level_ranges[i], level_ranges[i + 1]);

	// a healthy hierarchy ends with one (or very few) clusters at the top, and has about as many clusters in all levels together as twice the leaves
	logInfo("Cluster DAG: ", leaf_clusters, " leaf clusters, ", (u32)out.clusters.size(), " in total, ", (u32)current.size(), " at the top, ", levels, " levels, ", simplified_groups, " groups simplified, ", stuck_groups, " stuck, ", cone_clusters, " leaf clusters with a cone (", cone_triangles, " of ", leaf_triangles, " triangles), tree of ", (u32)out.nodes.size(), " nodes and ", (u32)out.roots.size(), " roots, error of groups: position ", (float)(position_error_sum / maximum(simplified_groups, 1u)), ", attributes ", (float)(attribute_error_sum / maximum(simplified_groups, 1u)));
	return true;
}

} // namespace Lumix
