#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/math.h"
#include "core/span.h"

namespace Lumix {

// One node of a mesh's cluster DAG, also the GPU layout (80 bytes), read by data/shaders/nanite.hlsl.
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
	// `error` is the error of positions. The error of attributes (normals and UVs that the triangles took from other places) is separate, the
	// error that is compared to the screen is `error + weight * attribute_error`, the weight is chosen when rendering. Both never get smaller
	// from a cluster to its parent, so that the cut is valid for every weight.
	float attribute_error;
	float parent_attribute_error;	// 0 if the cluster has no parent
	// Clusters are streamed in groups (MeshGroupGPU). A group is the set of sibling clusters, the ones that share their parent.
	u32 group;				// the group of this cluster, an index into MeshClusters::groups
	u32 refined_group;		// the group of clusters that were simplified to make this cluster (the finer ones, "children"), MeshClusters::NO_GROUP for the finest clusters
};
static_assert(sizeof(MeshClusterGPU) == 80);

// A group of sibling clusters, they are consecutive in MeshClusters::clusters. The unit of streaming: the vertex stream and the indices of the clusters of a group
// are contiguous, and the groups of a page are consecutive. A group may be streamed in only after the group of its parent (the coarser clusters),
// the groups that are RESIDENT (the coarsest ones) are never streamed out, so every mesh can always be drawn.
struct MeshGroupGPU {
	enum Flags : u32 { RESIDENT = 1 << 0 };
	u32 first_cluster;
	u32 cluster_count;
	u32 page;				// not used in the model file (zero), ClusterStream::init makes the pages when the model is loaded; the groups of a page are streamed together
	u32 flags;				// Flags, same as page
	u32 stream_offset;		// byte offset of the part of the vertex stream of the group, from the start of the stream (4 byte aligned, the stream has no headers)
	u32 stream_size;		// bytes of it (the last group has some bytes of padding more, shaders read two words at any bit)
	u32 padding0;
	u32 padding1;
};
static_assert(sizeof(MeshGroupGPU) == 32);

// What the shaders know of a group (the group table of a mesh in the buffer of cluster data, indexed by MeshClusterGPU::group). The addresses are bytes from the start of
// the buffer, so that the data of a group can be anywhere in it.
struct MeshGroupAddressGPU {
	enum Flags : u32 {
		LOADED = 1 << 0,		// the data of the group is in the buffer, the clusters of a group that is not loaded are not drawn (the parents are, see nanite.hlsl)
		PERMANENT = 1 << 1		// never unloaded
	};
	// The payload of a group is in one range, in this order: MeshClusterGPU for each cluster, MeshClusterVertexGPU for each cluster, the vertex stream, the indices.
	// The cluster `c` of the group has its record at record_address + (c - first_cluster) * 80, its header at header_address + (c - first_cluster) * 48 and its
	// indices at index_address + (c - first_cluster) * index_stride. Meaningful only if the group is LOADED.
	u32 record_address;
	u32 header_address;
	u32 stream_address;		// of the vertex stream of the group, positions and attributes of its clusters, see MeshClusterVertexGPU::pos_bit_offset
	u32 index_address;
	u32 first_cluster;
	u32 cluster_count;
	u32 flags;				// Flags, the id of the group in the feedback buffer << 8
	u32 padding;
};
static_assert(sizeof(MeshGroupAddressGPU) == 32);

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
	u32 unrefined;			// 1 if a cluster of the node (or under it) has children that are not loaded: the node can not be skipped because its clusters are too coarse for the distance, see MeshGroupAddressGPU::LOADED
	float padding1;
};

// Vertices of clusters are not shared between clusters, each cluster has its own (at most MeshClusters::MAX_CLUSTER_VERTICES), so that they can be
// packed relative to the cluster. The header of a cluster (48 bytes, the GPU layout, read by vertex_pulling.hlsli) says where they are.
// Vertices are interleaved in one bit stream, in a piece for each group (MeshGroupGPU, a piece starts at a 32 bit boundary), the headers are separate, a vertex is
//   - its position: integers of a lattice of the whole mesh (position = integer * pos_step), the same for every cluster, so that a vertex that is
//     in more clusters is at the same place in all of them. A cluster stores `integer - pos_start` with pos_bits bits per axis, bit packed,
//   - then its attributes, not aligned: the frame (normal and tangent, 32 bits), the UV (uv_bits per axis), AO (8 bits) and color (32 bits), if the mesh has them
// so the vertex `local` of a cluster starts at the bit `pos_bit_offset + local * (bits of x + y + z + attribute bits)`, where the number of the
// attribute bits is in the top byte of pos_bits.
// The indices of the clusters are local to the cluster: u16 while they are built, bytes in the model file and on the GPU.
struct MeshClusterVertexGPU {
	i32 pos_start[3];		// lattice position of the minimum of the cluster
	u32 pos_bits;			// bits of x | bits of y << 8 | bits of z << 16 | bits of attributes of a vertex << 24
	u32 uv_bits;			// bits of u | bits of v << 8
	u32 vertex_count;
	u32 pos_bit_offset;		// of the first vertex, from the start of the piece of the stream that the cluster's group has
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
		: clusters(allocator), groups(allocator), indices(allocator), index_positions(allocator), nodes(allocator), roots(allocator)
		, vertex_headers(allocator), local_indices(allocator), vertex_words(allocator)
	{}

	static constexpr u32 LEAF_FLAG = 0x80000000u;
	static constexpr u32 NODE_FANOUT = 8;
	static constexpr u32 MAX_CLUSTER_VERTICES = 256;	// indices local to a cluster are bytes, so not more than 256
	static constexpr u32 MAX_POSITION_BITS = 25;	// per axis of a cluster
	static constexpr u32 NO_GROUP = 0xffffFFFFu;

	Array<MeshClusterGPU> clusters;
	Array<MeshGroupGPU> groups;	// by MeshClusterGPU::group, filled by buildMeshClusters
	Array<u32> indices;			// indices of vertices of the mesh, used while the clusters are built. See local_indices
	Array<Vec3> index_positions;	// if the borders of the clusters are dilated (see buildMeshClusters): the position that the vertex of each entry of `indices` has in its cluster, otherwise empty
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

// position of vertex `local` of the cluster `header`, from the vertex stream in which the piece of the group of the cluster starts at the bit `group_bit_base`
inline Vec3 decodeClusterPosition(const MeshClusterVertexGPU& header, const u32* vertex_words, u32 group_bit_base, u32 local) {
	const u32 bx = header.pos_bits & 0xff, by = (header.pos_bits >> 8) & 0xff, bz = (header.pos_bits >> 16) & 0xff;
	const u64 bit = u64(group_bit_base) + header.pos_bit_offset + u64(local) * (bx + by + bz + (header.pos_bits >> 24));
	const i32 x = header.pos_start[0] + (i32)readClusterBits(vertex_words, bit, bx);
	const i32 y = header.pos_start[1] + (i32)readClusterBits(vertex_words, bit + bx, by);
	const i32 z = header.pos_start[2] + (i32)readClusterBits(vertex_words, bit + bx + by, bz);
	return Vec3((float)x, (float)y, (float)z) * header.pos_step;
}

struct Renderer;

// The residency of the groups of one mesh with clusters. The tables (clusters, nodes, headers, the table of group addresses) of a mesh are always in the buffer of cluster data, the
// payload of the groups (vertex stream and indices) is loaded and unloaded in pages (MeshGroupGPU::page) while the game runs. The GPU says which ones it needs (see nanite.hlsl and
// Renderer::getClusterFeedbackBuffer), the renderer loads them (RendererImpl::updateClusterStreaming).
//
// Invariant: a group is loaded only if every group that has a cluster refined from it (MeshClusterGPU::refined_group == the group) is loaded, the groups of its "parents". Without it
// both the clusters of the group and the coarser ones would be drawn, see processCluster in nanite.hlsl. So a page is loaded after the pages of its parents, and a page is unloaded only
// if the groups that it has are not parents of loaded groups of other pages. The groups that are RESIDENT never unload, they have no parents but themselves.
struct ClusterStream {
	static constexpr u32 NONE = 0xffFFffFFu;
	// The pages are made when the model is loaded (the pages of the model file, MeshGroupGPU::page, are not used), so these can be changed without importing again
	static constexpr u32 PAGE_BYTES = 32 * 1024;		// payload of a page, a group that is larger has a page of its own
	static constexpr u32 PERMANENT_BYTES = 16 * 1024;	// the coarsest groups up to this payload (at least one group) are never unloaded

	struct Page {
		u32 first_group = 0;
		u32 group_count = 0;
		u32 pool_offset = 0;	// of the range of the payload of the page in the buffer of cluster data, if it is loaded
		u32 pool_size = 0;
		u32 loaded_frame = 0;
		u32 last_used = 0;		// the frame stamp of the last time a cluster of a group of the page was drawn (zero: never)
		u32 wanted_frame = 0;
		bool loaded = false;
		bool permanent = false;
	};

	explicit ClusterStream(IAllocator& allocator);

	// Copies everything that is needed to load pages, nothing is loaded. `base_id` is the first of the ids of the pages in the feedback buffer, NONE: set it later with setFeedbackBase (the pages are made here).
	// Only the nodes and what makes the pages stay in the stream, the clusters (records and headers), vertices and indices are in the payloads of the groups.
	bool init(const MeshClusterGPU* cluster_array, u32 clusters, const MeshClusterVertexGPU* header_array, const MeshNodeGPU* node_array, u32 nodes_count, const MeshGroupGPU* group_array, u32 group_count
		, const u8* stream_bytes, u32 stream_size, const u8* index_bytes, u32 stride, u32 base_id);
	// bytes of the payload of a group
	u32 groupBytes(u32 group) const;
	// bytes of the range of a page in the buffer of cluster data
	u32 pageBytes(u32 page) const;
	// the first page that has a parent group of the page and is not loaded, or NONE
	u32 firstMissingParentPage(u32 page) const;
	bool canEvict(u32 page) const;
	// sets Page::wanted_frame of the pages that have parents of the page, so they are not unloaded before the page is loaded
	void protectParentPages(u32 page, u32 stamp);
	// the pages of the parents must be loaded, false if there is no room in the buffer
	// `can_reclaim`: if there is no room in the buffer the renderer may unload other pages (not when the streamer loads, it does it itself and holds its lock)
	bool loadPage(Renderer& renderer, u32 page, u32 stamp, bool can_reclaim);
	void evictPage(Renderer& renderer, u32 page);
	// frees everything that is loaded
	void release(Renderer& renderer);
	// recomputes MeshNodeGPU::unrefined and uploads the nodes that changed (if the nodes are uploaded: node_table_offset)
	void refreshNodes(Renderer& renderer);
	u32 addressFlags(u32 group) const;
	// the first id of the pages of this stream in the feedback buffer (one for each page), the flags of the groups have the id of their page
	void setFeedbackBase(u32 base_id);

	IAllocator& allocator;
	Array<MeshGroupGPU> groups;
	Array<u32> parent_first, parent_count, parents;	// of each group, the groups that have a cluster refined from it (lists, see buildAdjacency in model.cpp)
	Array<u32> child_first, child_count, children;	// of each group, the groups that its clusters are refined from
	Array<Page> pages;
	Array<u8> loaded;								// of each group
	Array<MeshGroupAddressGPU> addresses;			// the table of the GPU, uploaded to group_table_offset
	Array<u8> payload;								// the payloads of all groups (see MeshGroupAddressGPU), one after the other, 16 byte aligned: what the pages are copied from
	Array<u32> group_offset;						// of each group, its offset in payload
	Array<MeshNodeGPU> nodes;
	Array<u32> cluster_refined;						// MeshClusterGPU::refined_group of each cluster
	u32 cluster_count = 0;
	u32 node_count = 0;
	u32 index_stride = 0;
	u32 global_base = NONE;
	u32 group_table_offset = NONE;					// in the buffer of cluster data, NONE until the table is uploaded (0 is a valid offset)
	u32 node_table_offset = NONE;					// the same for the nodes
	u32 table_bytes = 0;							// in the buffer as long as the mesh is loaded: the nodes, the roots, the groups of the clusters and the table of group addresses
	u32 streamed_bytes = 0;							// of the loaded pages that are not permanent
	u32 root_bytes = 0;							// payload of the groups that have no parent, they are always loaded: the least that a mesh needs
};

} // namespace Lumix
