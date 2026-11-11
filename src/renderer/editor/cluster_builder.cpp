#include "cluster_builder.h"

#include "core/crt.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/string.h"

#include "meshoptimizer/meshoptimizer.h"

// the builder of the DAG of clusters is the one of meshoptimizer (its implementation is in external/meshoptimizer/clusterlod.cpp), see buildMeshClusters
#include "meshoptimizer/clusterlod.h"

#include <float.h>
#include <math.h>

namespace Lumix {

namespace {

// the sizes meshoptimizer recommends for meshlets (its triangle count is a multiple of 4, and 124 leaves room for hardware that needs a few spare slots)
constexpr u32 MAX_CLUSTER_VERTICES = 64;
constexpr u32 MAX_CLUSTER_TRIANGLES = 124;
constexpr float UV_STEP = 1.f / 16384.f;	// the largest step of quantized UVs, unless the range of a cluster is too large for 16 bits
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

bool buildMeshClusters(Span<const Vec3> positions, Span<const Vec3> normals, Span<const Vec2> uvs, Span<const u32> indices, bool dilate_borders, MeshClusters& out, IAllocator& allocator) {
	PROFILE_FUNCTION();
	if (indices.length() < 3 || positions.length() == 0) return false;
	const u32 vertex_count = positions.length();

	// What meshoptimizer's own example (demo/nanite.cpp) does: the simplifier is attribute aware, normals have a weight and UV seams are protected (the UVs have no
	// weight), it works on the vertices of the mesh as they are, and the result refers to them, so there is nothing to map back.
	struct Attributes {
		Vec3 normal;
		Vec2 uv;
	};
	Array<Attributes> attributes(allocator);
	attributes.resize(vertex_count);
	for (u32 i = 0; i < vertex_count; ++i) {
		attributes[i].normal = normals.length() == vertex_count ? normals[i] : Vec3(0, 0, 0);
		attributes[i].uv = uvs.length() == vertex_count ? uvs[i] : Vec2(0, 0);
	}
	const float attribute_weights[3] = { 0.5f, 0.5f, 0.5f };

	clodConfig config = clodDefaultConfig(MAX_CLUSTER_TRIANGLES);
	config.max_vertices = MAX_CLUSTER_VERTICES;
	config.partition_sort = true;	// the clusters of a level that are next to each other in the output are close, the trees over them (buildNodes) need that

	// Dilation moves the vertices of the simplified clusters, in the positions that are given to it, from level to level. So the positions are a copy that it can
	// change, and every cluster takes the positions that its vertices have when the cluster is given to the callback below.
	Array<Vec3> dilated_positions(allocator);
	if (dilate_borders) {
		dilated_positions.resize(vertex_count);
		memcpy(dilated_positions.begin(), positions.begin(), vertex_count * sizeof(Vec3));
		config.simplify_dilate_borders = true;
	}
	const Vec3* cluster_positions = dilate_borders ? dilated_positions.begin() : positions.begin();

	clodMesh mesh = {};
	mesh.indices = indices.begin();
	mesh.index_count = indices.length();
	mesh.vertex_count = vertex_count;
	mesh.vertex_positions = &cluster_positions[0].x;
	mesh.vertex_positions_stride = sizeof(Vec3);
	mesh.vertex_attributes = &attributes[0].normal.x;
	mesh.vertex_attributes_stride = sizeof(Attributes);
	mesh.attribute_weights = attribute_weights;
	mesh.attribute_count = lengthOf(attribute_weights);
	mesh.attribute_protect_mask = (1 << 3) | (1 << 4);	// the UVs, after the normal

	// The callback gets the clusters of a group, with the error and the bounds of the simplification that the group goes through to make the next level
	// (the error is FLT_MAX if there is none: the group is the last one, or it could not be simplified). The clusters of a group were made by a group of the level
	// before, that is what their own error and bounds are. The groups come in the order of the levels, so the clusters of a level are consecutive.
	Array<u32> level_ranges(allocator);	// [first cluster, number of clusters] of the levels above the finest
	int level = -1;
	u32 level_first = 0;
	u32 group_count = 0;
	Array<clodBounds> group_bounds(allocator);	// the bounds and the error of each group, by the id that the callback returns
	bool leaves_done = false;
	auto close_level = [&]() {
		if (level > 0) {
			level_ranges.push(level_first);
			level_ranges.push(out.clusters.size() - level_first);
		}
	};
	auto output = [&](clodGroup group, const clodCluster* clusters, size_t count) -> int {
		if (group.depth != level) {
			if (level == 0) {
				// the finest clusters are the first ones, they contain every triangle once
				out.leaf_index_count = out.indices.size();
				out.leaf_cluster_count = out.clusters.size();
				leaves_done = true;
			}
			close_level();
			level = group.depth;
			level_first = out.clusters.size();
		}
		for (size_t i = 0; i < count; ++i) {
			const clodCluster& c = clusters[i];
			MeshClusterGPU cluster;
			memset(&cluster, 0, sizeof(cluster));
			cluster.first_index = out.indices.size();
			cluster.index_count = (u32)c.index_count;
			for (size_t j = 0; j < c.index_count; ++j) {
				out.indices.push(c.indices[j]);
				if (dilate_borders) out.index_positions.push(cluster_positions[c.indices[j]]);
			}
			// the bounds of the geometry (as it is in this cluster), for culling
			const ClusterSphere bounds = computeBounds(cluster_positions, &out.indices[cluster.first_index], cluster.index_count);
			cluster.center = bounds.center;
			cluster.radius = bounds.radius;
			// the bounds and the error of the group that made the cluster, 0 for the original geometry. With dilation the bounds of the cluster are larger than the
			// ones of its group (they have the dilated vertices), the group is what the error is projected from.
			const clodBounds lod = c.refined >= 0 ? group_bounds[c.refined] : c.bounds;
			cluster.lod_center = Vec3(lod.center[0], lod.center[1], lod.center[2]);
			cluster.lod_radius = lod.radius;
			cluster.error = lod.error;
			cluster.parent_center = Vec3(group.simplified.center[0], group.simplified.center[1], group.simplified.center[2]);
			cluster.parent_radius = group.simplified.radius;
			cluster.parent_error = group.simplified.error;
			out.clusters.push(cluster);
		}
		group_bounds.push(group.simplified);
		return (int)group_count++;
	};
	clodBuild(config, mesh, output);
	if (!leaves_done) {
		out.leaf_index_count = out.indices.size();
		out.leaf_cluster_count = out.clusters.size();
	}
	close_level();
	logInfo("Clusters: ", level_ranges.size() / 2 + 1, " levels, ", group_count, " groups, ", out.clusters.size(), " clusters, ", out.leaf_cluster_count, " of them in the finest level");

	// the trees over the clusters of the levels: the finest clusters, then the ones that each level has made
	buildNodes(out, 0, out.leaf_cluster_count);
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

	return true;
}

} // namespace Lumix
