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

} // namespace Lumix
