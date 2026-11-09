#pragma once

#include "engine/lumix.h"

#include "core/span.h"
#include "renderer/mesh_clusters.h"

namespace Lumix {

// Splits the triangles into clusters of up to 128 triangles, then repeatedly groups neighbouring clusters,
// simplifies each group to half the triangles and splits the result into new clusters, until one cluster is left
// (or the simplification gets stuck). Used by the model importer, the result is stored in the compiled model.
// `normals` and `uvs` (of the vertices, they can be empty) are used to measure how much the attributes of a simplified group differ from the original,
// that is a part of the error of the group.
bool buildMeshClusters(Span<const Vec3> positions, Span<const Vec3> normals, Span<const Vec2> uvs, Span<const u32> indices, MeshClusters& out, IAllocator& allocator);

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

// the packing of a tangent frame, see ClusterAttribute. Exposed for tests
u32 packClusterFrame(const Vec3& normal, const Vec3* tangent);
void unpackClusterFrame(u32 packed, Vec3& normal, Vec3& tangent);

} // namespace Lumix
