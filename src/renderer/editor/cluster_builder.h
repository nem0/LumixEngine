#pragma once

#include "engine/lumix.h"

#include "core/span.h"
#include "renderer/mesh_clusters.h"

namespace Lumix {

// Splits the triangles into clusters of up to 124 triangles, then repeatedly groups neighbouring clusters,
// simplifies each group to half the triangles and splits the result into new clusters, until one cluster is left
// (or the simplification gets stuck). Used by the model importer, the result is stored in the compiled model.
// `normals` and `uvs` (of the vertices, they can be empty) are used to measure how much the attributes of a simplified group differ from the original,
// that is a part of the error of the group.
// `dilate_borders`: the open borders of simplified clusters grow to keep the area of the surface that simplification removes (for foliage, where the pieces are
// loose leaves). Then the vertices of a cluster are not the ones of the mesh, they have the positions of MeshClusters::index_positions.
// `voxels`: if it is not null, each group of clusters switches from triangles to voxels independently when topology-preserving simplification stalls
// (leaving more than `stall_threshold` of its triangles) or its error reaches `voxels->error * mesh_diagonal` (or half the group's average edge length when `voxels->error == 0`);
// voxelized groups are then merged and downsampled upward, see MeshVoxelCluster.
struct ClusterVoxelSource {
	float error = 0.01f;			// a fraction of the diagonal of the mesh (0 = automatic, half average triangle edge length)
	float min_coverage = 0.25f;		// minimum accumulated surface area / (size * size) for a voxel to survive filtering
	float downsample_scale = 0.25f;	// child coverage multiplier when downsampling 2x coarser (0.25 preserves 2D area, 0.5 preserves 1D twigs)
	float stall_threshold = 0.65f;	// switch a group to voxels if simplification leaves more than this fraction of its triangles
	u32 max_resolution = 4000;		// maximum voxels along the longest axis of the mesh (clamped to [64, 4000])
	const u32* colors = nullptr;	// optional, 4 bytes (u8 x4) of each vertex, the voxels have the average (without the alpha)
	const u32* ao = nullptr;		// optional, 4 bytes (u8 x4) of each vertex, only the first one is used
};
struct ClusterSubmesh {
	Span<const u32> indices;
	u32 material = 0;
};
bool buildMeshClusters(Span<const Vec3> positions, Span<const Vec3> normals, Span<const Vec2> uvs, Span<const ClusterSubmesh> submeshes, bool dilate_borders, MeshClusters& out, IAllocator& allocator, const ClusterVoxelSource* voxels = nullptr);
bool buildMeshClusters(Span<const Vec3> positions, Span<const Vec3> normals, Span<const Vec2> uvs, Span<const u32> indices, bool dilate_borders, MeshClusters& out, IAllocator& allocator, const ClusterVoxelSource* voxels = nullptr);

// The vertices of a mesh, as the importer has them.
struct ClusterVertexSource {
	u32 vertex_count = 0;
	const Vec3* positions = nullptr;
	const Vec3* normals = nullptr;		// required
	const Vec3* tangents = nullptr;		// optional
	const Vec2* uvs = nullptr;			// optional
	const u32* ao = nullptr;			// optional, 4 bytes (u8 x4) of each vertex
	const u32* colors = nullptr;		// optional, 4 bytes (u8 x4) of each vertex
};

// Gives each cluster of `out` (made by buildMeshClusters) its own vertices and packs them, see MeshClusterVertexGPU. Fills vertex_headers, local_indices,
// vertex_words and the numbers that go with them. Positions are rounded to a lattice, the same for the whole mesh.
bool packClusterVertices(const ClusterVertexSource& source, MeshClusters& out, IAllocator& allocator);

// Makes the pages (streaming units made of groups, see ClusterStream) of a mesh, they are stored in the compiled model (the payload of a group is its vertex stream and the u8 indices of its clusters).
// False if the groups and the clusters are not valid (a group is not refined from a group with a lower number).
bool buildMeshPages(const MeshClusterGPU* clusters, u32 cluster_count, const MeshGroupGPU* groups, u32 group_count, MeshPages& out, IAllocator& allocator);

// the packing of a tangent frame, see ClusterAttribute. Exposed for tests
u32 packClusterFrame(const Vec3& normal, const Vec3* tangent);
void unpackClusterFrame(u32 packed, Vec3& normal, Vec3& tangent);

} // namespace Lumix
