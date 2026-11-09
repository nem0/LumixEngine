#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/math.h"
#include "core/span.h"

namespace Lumix {

// One node of a mesh's cluster DAG, also the GPU layout (96 bytes), read by data/shaders/nanite.hlsl.
// Clusters of all DAG levels reference the vertices of the original mesh, so no new vertices are created.
//
// A cluster is drawn if its `error` is small enough on the screen and the `error` of its parent is not.
// All clusters with the same parent share `parent_center`, `parent_radius` and `parent_error`, so they
// always make the same decision about the parent and the chosen cut has no holes and no overlaps.
struct MeshClusterGPU {
	Vec3 center;			// bounding sphere of the cluster geometry, used for culling
	float radius;
	Vec3 lod_center;		// bounding sphere used to project `error`
	float lod_radius;
	Vec3 parent_center;		// same for `parent_error`
	float parent_radius;
	float error;			// object space geometric error of this cluster
	float parent_error;		// FLT_MAX if the cluster has no parent
	u32 first_index;		// into MeshClusters::indices
	u32 index_count;
	Vec3 cone_axis;			// average normal of the triangles, front faces are counter clockwise
	float cone_cutoff;		// the cluster is back facing if dot(center - camera, cone_axis) >= cone_cutoff * length(center - camera) + radius, 1 if never
	// `error` is the error of positions. The error of attributes (normals and UVs that the triangles took from other places) is separate, the
	// error that is compared to the screen is `error + weight * attribute_error`, the weight is chosen when rendering. Both never get smaller
	// from a cluster to its parent, so that the cut is valid for every weight.
	float attribute_error;
	float parent_attribute_error;	// 0 if the cluster has no parent
	float padding0;
	float padding1;
};

// A node of the tree over the clusters of a mesh (64 bytes, the GPU layout, read by data/shaders/nanite.hlsl). The clusters of every level of the
// DAG have their own tree, the trees are built over runs of consecutive clusters. The shader walks the trees from the roots and skips a node,
// with everything under it, if no cluster of it can be selected: its smallest error is too large for the distance, or the largest error of the
// parents of its clusters is small enough to be drawn instead of any of them. So the clusters of the levels that are not near the cut are never tested.
struct MeshNodeGPU {
	Vec3 center;			// sphere around the clusters of the node, for the frustum
	float radius;
	Vec3 lod_center;		// sphere around the spheres of lod and parent bounds of the clusters, the distance to the node is measured from it
	float lod_radius;
	float min_error;		// the smallest error of a cluster of the node
	float max_parent_error;	// the largest error of a parent of a cluster of the node, FLT_MAX if a cluster has no parent
	u32 first;				// of a leaf: the first cluster of a run of consecutive ones. Otherwise the first child node, children are consecutive
	u32 count_and_flags;	// number of clusters or children, LEAF_FLAG is set in leaves
	float min_attribute_error;			// the smallest and the largest (of parents) error of attributes of the clusters of the node
	float max_parent_attribute_error;
	float padding0;
	float padding1;
};

// Vertices of clusters are not shared between clusters, each cluster has its own (at most MeshClusters::MAX_CLUSTER_VERTICES), so that they can be
// packed relative to the cluster. The header of a cluster (48 bytes, the GPU layout, read by vertex_pulling.hlsli) says where they are.
// Vertices are interleaved in one bit stream that follows the headers (in the same buffer), a vertex is
//   - its position: integers of a lattice of the whole mesh (position = integer * pos_step), the same for every cluster, so that a vertex that is
//     in more clusters is at the same place in all of them. A cluster stores `integer - pos_start` with pos_bits bits per axis, bit packed,
//   - then its attributes, not aligned: the frame (normal and tangent, 32 bits), the UV (uv_bits per axis), AO (8 bits) and color (32 bits), if the mesh has them
// so the vertex `local` of a cluster starts at the bit `pos_bit_offset + local * (bits of x + y + z + attribute bits)`, where the number of the
// attribute bits is in the top byte of pos_bits.
// The indices of the clusters are u16, local to the cluster.
struct MeshClusterVertexGPU {
	i32 pos_start[3];		// lattice position of the minimum of the cluster
	u32 pos_bits;			// bits of x | bits of y << 8 | bits of z << 16 | bits of attributes of a vertex << 24
	u32 uv_bits;			// bits of u | bits of v << 8
	u32 vertex_count;
	u32 pos_bit_offset;		// of the first vertex, from the start of the buffer of headers followed by the vertices (headers are in the first bits)
	float pos_step;
	float uv_min[2];		// UVs are stored as uv_bits integers in [uv_min, uv_min + uv_extent]
	float uv_extent[2];
};
static_assert(sizeof(MeshClusterVertexGPU) == 48);

// layout of the attribute stream, the order of the attributes is fixed
enum class ClusterAttribute : u32 {
	UV = 1 << 0,		// u16 x2
	AO = 1 << 1,		// u8 (normalized), only the first of the 4 channels of the source
	COLOR = 1 << 2,		// u8 x4 (normalized)
	TANGENT = 1 << 3	// the frame has a tangent, otherwise only a normal (the tangent is any perpendicular vector)
};
// The first u32 of every vertex is the tangent frame: a quaternion that rotates (1, 0, 0) to the tangent and (0, 0, 1) to the normal, 10 bits for
// each of the three smaller components and 2 bits for the index of the largest one (smallest three).

// reads `count` (at most 32) bits from bit `bit` of `words`
inline u32 readClusterBits(const u32* words, u64 bit, u32 count) {
	if (count == 0) return 0;
	const u32 shift = u32(bit & 31);
	const u32* w = words + (bit >> 5);
	u32 v = shift == 0 ? w[0] : (w[0] >> shift) | (w[1] << (32 - shift));
	return count >= 32 ? v : v & ((1u << count) - 1);
}

struct MeshClusters {
	explicit MeshClusters(IAllocator& allocator)
		: clusters(allocator), indices(allocator), nodes(allocator), roots(allocator)
		, vertex_headers(allocator), local_indices(allocator), vertex_words(allocator)
	{}

	static constexpr u32 LEAF_FLAG = 0x80000000u;
	static constexpr u32 NODE_FANOUT = 8;
	static constexpr u32 MAX_CLUSTER_VERTICES = 64;
	static constexpr u32 MAX_POSITION_BITS = 25;	// per axis of a cluster

	Array<MeshClusterGPU> clusters;
	Array<u32> indices;			// indices of vertices of the mesh, used while the clusters are built. See local_indices
	Array<MeshNodeGPU> nodes;
	Array<u32> roots;			// indices of the roots of the trees (one for each level)
	u32 leaf_index_count = 0; // the first leaf_index_count indices are the finest clusters, they contain every triangle once
	u32 leaf_cluster_count = 0; // the finest clusters are the first ones, a selection can not have more clusters than that

	// vertices of clusters, see MeshClusterVertexGPU. Made by packClusterVertices
	Array<MeshClusterVertexGPU> vertex_headers;	// for each cluster
	Array<u16> local_indices;	// the same as `indices`, but local to the vertices of the cluster
	Array<u32> vertex_words;	// packed positions and attributes of all vertices, the clusters one after another (+ padding at the end)
	u32 attribute_stride = 0;	// bytes of attributes of each vertex if they were not packed, the layout of vertex pulling defines
	u32 attribute_flags = 0;	// ClusterAttribute
	u32 vertex_count = 0;
	u32 leaf_vertex_count = 0;	// vertices of the finest clusters, they are the first ones
};

// position of vertex `local` of the cluster `header`, from the vertex stream of the buffer in which the stream starts at `stream_bit_base`
inline Vec3 decodeClusterPosition(const MeshClusterVertexGPU& header, const u32* vertex_words, u32 stream_bit_base, u32 local) {
	const u32 bx = header.pos_bits & 0xff, by = (header.pos_bits >> 8) & 0xff, bz = (header.pos_bits >> 16) & 0xff;
	const u64 bit = u64(header.pos_bit_offset - stream_bit_base) + u64(local) * (bx + by + bz + (header.pos_bits >> 24));
	const i32 x = header.pos_start[0] + (i32)readClusterBits(vertex_words, bit, bx);
	const i32 y = header.pos_start[1] + (i32)readClusterBits(vertex_words, bit + bx, by);
	const i32 z = header.pos_start[2] + (i32)readClusterBits(vertex_words, bit + bx + by, bz);
	return Vec3((float)x, (float)y, (float)z) * header.pos_step;
}

} // namespace Lumix
