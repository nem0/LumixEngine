#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/hash_map.h"
#include "renderer/gpu_table.h"

namespace Lumix {

struct GPUSceneRegistry;
struct Model;

// Models without clusters (not Model::hasClusters) of the GPU scene, drawn mesh by mesh: the LOD of an instance is chosen by gpu_scene_cull.hlsl by the distance, and each
// mesh of the LOD is drawn by a command of its own (one for each mesh and material). They have no table of meshes: the vertices and indices of a mesh are in a range of the
// buffer of cluster data (GPUMeshPool), the material of a mesh is the entry `default_first + index of the mesh` of the default material set of the model, and the selected meshes
// are (slot, index of the mesh in its model). The ids of these models are the indices of GPULodModels::entries with GPUSceneConst::LOD_MODEL_BIT set.

// 40 bytes, the layout of gpu_scene_cull.hlsl and gpu_scene_variants.hlsl. A model that can not be drawn has no LODs and no default set.
// The meshes of the LOD `l` are lod[l]: the first mesh in the low 16 bits and the last + 1 in the high 16 bits (0: the LOD has no meshes). The LOD of an instance is the
// first one with squared distance < lod_distance[l] (the last one, lod4, if there is none), see emitLod in gpu_scene_cull.hlsl.
struct GPULodModel {
	u32 default_first;		// the first entry of the default material set (GPUSceneRegistry::set_table)
	u32 lod4;
	u32 lod[4];
	float lod_distance[4];
};
static_assert(sizeof(GPULodModel) == 40);

// the vertices and indices of a mesh, a copy in the buffer of cluster data (the model keeps its own buffers for the other uses of it)
struct GPUMeshPool {
	u32 vertex_offset;
	u32 vertex_size;
	u32 index_offset;
	u32 index_size;
};

struct GPUPoolCopy {
	gpu::BufferHandle src;
	u32 dst_offset;
	u32 size;
};

struct GPULodModelEntry {
	const Model* model;		// null: the id is free
	u32 refs;
	u32 default_set;
	GPUMeshPool* pools;		// for each mesh
	u32 pool_count;
};

struct GPULodModels {
	GPULodModels(IAllocator& allocator);

	Array<GPULodModelEntry> entries;	// by index
	Array<u32> free_entries;
	HashMap<const Model*, u32> map;
	GPUMirror model_table;				// GPULodModel[] by index
	Array<GPUPoolCopy> pool_copies;		// pending copies from the buffers of the meshes to the ranges of the pool
};

// The id (without the LOD_MODEL_BIT) of the model if it is there (it is not counted), INVALID_MODEL if it is not
u32 findLodModel(const GPULodModels& models, const Model& model);
// The model gets an id (without the LOD_MODEL_BIT), counted once, its pools are allocated and its entry is written when it can be drawn
u32 addLodModel(GPUSceneRegistry& res, const Model& model);
void releaseLodModel(GPUSceneRegistry& res, u32 index);
// the pool of the mesh of the model (an id with LOD_MODEL_BIT), null if there is none
const GPUMeshPool* getLodPool(const GPULodModels& models, u32 index, u32 mesh_index);

// queues the copies of the meshes that were added to their pools and uploads the table
void flushLodModels(GPUSceneRegistry& res);
void destroyLodModels(GPUSceneRegistry& res);

} // namespace Lumix
