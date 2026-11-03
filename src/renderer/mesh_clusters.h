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

struct MeshClusters {
	explicit MeshClusters(IAllocator& allocator) : clusters(allocator), indices(allocator), nodes(allocator), roots(allocator) {}

	static constexpr u32 LEAF_FLAG = 0x80000000u;
	static constexpr u32 NODE_FANOUT = 8;

	Array<MeshClusterGPU> clusters;
	Array<u32> indices;
	Array<MeshNodeGPU> nodes;
	Array<u32> roots;			// indices of the roots of the trees (one for each level)
	u32 leaf_index_count = 0; // the first leaf_index_count indices are the finest clusters, they contain every triangle once
	u32 leaf_cluster_count = 0; // the finest clusters are the first ones, a selection can not have more clusters than that
};

} // namespace Lumix
