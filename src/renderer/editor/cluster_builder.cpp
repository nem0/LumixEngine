#include "cluster_builder.h"

#include "core/crt.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/string.h"

#include "meshoptimizer/meshoptimizer.h"

#include <float.h>
#include <math.h>

namespace Lumix {

namespace {

constexpr u32 MAX_CLUSTER_VERTICES = 64;
constexpr u32 MAX_CLUSTER_TRIANGLES = 128;
constexpr float UV_STEP = 1.f / 16384.f;	// the largest step of quantized UVs, unless the range of a cluster is too large for 16 bits
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
				carry_over();
				continue;
			}

			const ClusterSphere group_sphere = mergeSpheres(out.clusters, sorted, from, member_count);

			// back to vertices of the mesh: each corner takes the wedge whose triangle is the most similar to the simplified one
			// and the attributes of the chosen wedges are compared to what they should be, that is a part of the error of the group
			// UVs per world unit of the triangles of the group: the median, a few edges that jump to another place of the atlas do not matter
			float uv_per_unit = 0;
			if (uv_ratios.size() > 0) {
				sort(uv_ratios.begin(), uv_ratios.end());
				uv_per_unit = uv_ratios[uv_ratios.size() / 2];
			}
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
				if (len == 0) {
					// no area, it is not seen and has no normal to choose a wedge by. Dropped, so that its arbitrary wedges do not get to the next level
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
				carry_over();
				continue;
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

		if ((u32)out.clusters.size() > level_first_cluster) {
			level_ranges.push(level_first_cluster);
			level_ranges.push((u32)out.clusters.size() - level_first_cluster);
		}
		if (!progress) break;
		current.swap(next);
	}

	// the trees over the clusters of the levels: the finest clusters, then the ones that each level has made
	buildNodes(out, 0, leaf_clusters);
	for (u32 i = 0; i + 1 < (u32)level_ranges.size(); i += 2) buildNodes(out, level_ranges[i], level_ranges[i + 1]);

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
	double max_coordinate = 0;
	for (u32 i = 0; i < vertex_count; ++i) {
		const Vec3 p = source.positions[i];
		max_coordinate = maximum(max_coordinate, (double)maximum(fabsf(p.x), maximum(fabsf(p.y), fabsf(p.z))));
	}
	float step = 1.f / 16384.f;
	while (max_coordinate / step >= 16777216.0) step *= 2.f;
	Array<i32> lattice(allocator);
	lattice.resize(vertex_count * 3);
	for (u32 i = 0; i < vertex_count; ++i) {
		const Vec3 p = source.positions[i];
		lattice[i * 3 + 0] = (i32)floor((double)p.x / step + 0.5);
		lattice[i * 3 + 1] = (i32)floor((double)p.y / step + 0.5);
		lattice[i * 3 + 2] = (i32)floor((double)p.z / step + 0.5);
	}

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

	const u64 header_bits = u64(cluster_count) * sizeof(MeshClusterVertexGPU) * 8;
	u64 bit_cursor = 0;
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
		vertices.clear();
		for (u32 k = 0; k < cluster.index_count; ++k) {
			const u32 g = out.indices[cluster.first_index + k];
			if (local_of[g] == INVALID_INDEX) {
				local_of[g] = (u32)vertices.size();
				vertices.push(g);
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
		for (u32 g : vertices) {
			for (u32 a = 0; a < 3; ++a) {
				mn[a] = minimum(mn[a], lattice[g * 3 + a]);
				mx[a] = maximum(mx[a], lattice[g * 3 + a]);
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
		header.pos_bit_offset = (u32)(header_bits + bit_cursor);
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
		if (header_bits + bit_cursor + u64((u32)vertices.size()) * (bits[0] + bits[1] + bits[2] + attribute_bits) >= (1ull << 32)) {
			logError("Vertices of clusters are larger than 512 MB");
			return false;
		}

		for (u32 g : vertices) {
			for (u32 a = 0; a < 3; ++a) write_bits(u32(lattice[g * 3 + a] - mn[a]), bits[a]);

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

	return true;
}

} // namespace Lumix
