#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/hash_map.h"
#include "renderer/gpu_table.h"

namespace Lumix {

struct GPUSceneRegistry;
struct Model;

// Models with clusters (Model::hasClusters) of the GPU scene: their meshes are DAGs of clusters, which gpu_scene_cull.hlsl and nanite.hlsl select from, see
// mesh_clusters.h. The ids of these models are the indices of GPUClusterModels::entries. Models without clusters are in gpu_lod_models.h.

// The meshes of a model with clusters, 8 bytes, read by gpu_scene_cull.hlsl. A model that can not be drawn has no meshes. The model has the meshes of its first LOD.
struct GPUClusterModel {
	u32 first_mesh;			// in the table of meshes
	u32 mesh_count;
};
static_assert(sizeof(GPUClusterModel) == 8);

// Static data of a mesh with clusters, 32 bytes, read by the passes that select clusters and by the draws. The meshes of a model are consecutive.
// Everything else that the shaders need is derived from these, with the functions below (the same formulas are in nanite.hlsl, gpu_scene_cull.hlsl and
// vertex_pulling.hlsli): the table of the mesh (Mesh::cluster_table_offset) is MeshClusterGPU[cluster_count], MeshNodeGPU[node_count], u32 roots[root_count], and then,
// 16 byte aligned, MeshClusterVertexGPU[cluster_count] (the headers of the clusters, vertex_pulling.hlsli).
struct GPUClusterMesh {
	u32 clusters;			// byte offset in the buffer of cluster data (Renderer::getClusterDataBuffer) of the table of the mesh
	u32 group_table;		// byte offset in the same buffer of MeshGroupAddressGPU[], where the vertices and the u16 indices (local to a cluster, tightly packed, see MeshClusterGPU::first_index) of the clusters of each group are
	u32 cluster_count;
	u32 node_count;
	u32 root_count;
	u32 index_stride;		// the largest index count of a cluster of the mesh (Mesh::cluster_index_stride), the indices are not padded to it
	u32 material_entry;		// the entry of the mesh in the default material set of its model (GPUSceneRegistry::set_table): its variant and material index
	u32 index_in_model;		// the index of the mesh in its model
};
static_assert(sizeof(GPUClusterMesh) == 32);

constexpr u32 CLUSTER_GPU_SIZE = 80;		// sizeof(MeshClusterGPU)
constexpr u32 CLUSTER_NODE_GPU_SIZE = 64;	// sizeof(MeshNodeGPU)
constexpr u32 CLUSTER_HEADER_GPU_SIZE = 48;	// sizeof(MeshClusterVertexGPU)

inline u32 clusterNodesOffset(const GPUClusterMesh& m) { return m.clusters + m.cluster_count * CLUSTER_GPU_SIZE; }
inline u32 clusterRootsOffset(const GPUClusterMesh& m) { return m.node_count * CLUSTER_NODE_GPU_SIZE; }	// in the nodes
inline u32 clusterHeadersOffset(const GPUClusterMesh& m) {
	return m.clusters + ((m.cluster_count * CLUSTER_GPU_SIZE + m.node_count * CLUSTER_NODE_GPU_SIZE + m.root_count * 4 + 15) & ~15u);
}

struct GPUClusterModelEntry {
	const Model* model;		// null: the id is free
	u32 refs;
	u32 first_mesh;			// the range of the table of meshes
	u32 mesh_count;
	u32 default_set;
	u32 tree_depth;			// the deepest tree of its meshes
};

struct GPUClusterModels {
	GPUClusterModels(IAllocator& allocator);

	Array<GPUClusterModelEntry> entries;	// by id
	Array<u32> free_entries;
	HashMap<const Model*, u32> map;
	GPUMirror model_table;				// GPUClusterModel[] by id
	RangeAllocator mesh_ranges;
	GPUMirror mesh_table;				// GPUClusterMesh[]
	Array<u32> depth_counts;			// models whose deepest tree has each depth
	u32 max_node_count = 0;				// the most nodes that a mesh has, of the meshes that were in the table (it does not get smaller when they are removed)
};

// The id of the model if it is there (it is not counted), INVALID_MODEL if it is not
u32 findClusterModel(const GPUClusterModels& models, const Model& model);
// The model gets an id, counted once, its meshes are written when it is complete (a model that is not complete has no meshes rather than some).
u32 addClusterModel(GPUSceneRegistry& res, const Model& model);
void releaseClusterModel(GPUSceneRegistry& res, u32 id);
// true if the mesh of the model is one of the meshes of its first LOD that have clusters in the table, see Model::getClusterMeshesEnd
bool isClusterMeshDrawn(const Model& model, u32 mesh_index);

void flushClusterModels(GPUSceneRegistry& res);
void destroyClusterModels(GPUSceneRegistry& res);
u32 getMaxTreeDepth(const GPUClusterModels& models);	// of the meshes of the table
u32 getMaxNodeCount(const GPUClusterModels& models);	// of the meshes of the table, the culling of trees (nanite.hlsl) has a slice of the scratch buffer for two lists of this size in each group

} // namespace Lumix
