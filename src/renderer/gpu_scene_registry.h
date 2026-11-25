#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/hash_map.h"
#include "core/span.h"
#include "renderer/gpu/gpu.h"
#include "renderer/gpu_cluster_models.h"
#include "renderer/gpu_lod_models.h"
#include "renderer/gpu_scene_const.h"
#include "renderer/gpu_table.h"

namespace Lumix {

struct Material;
struct Mesh;
struct Model;
struct Renderer;
struct Shader;

// Materials of an instance that are not the ones of its model: `mesh` is the index of the mesh in its model.
struct GPUMaterialOverride {
	u32 mesh;
	u32 material_index;
	const Material* material;
};

// What one indirect call draws: the variants of the same program, layer and kind of mesh. A free group has variant_count 0.
struct GPUDrawGroup {
	Shader* shader;
	u64 define_mask;				// of the material, with SKINNED
	gpu::StateFlags state;			// of the material
	gpu::PrimitiveType primitive_type;
	const char* semantics_defines;
	u8 layer;
	bool lod;						// meshes without clusters, otherwise the group has clusters
	gpu::DataType index_type;		// of the commands
	u32 variant_count;				// the commands of the group are consecutive, the command of a variant that is not used draws nothing
	u32 live_variants;				// variants that are used
	u32 free_variants;				// the first variant of the group that is not used (reused first, it keeps its command), linked by Variant::next_free
	u64 hash;
};

struct GPUSetEntry {
	const Material* material;	// of an own entry, the instances that use the set have it (the set has no reference of it)
	u32 material_index;
	u32 variant;
	bool own;
};

struct GPUMaterialSet {
	u32 model_id;	// INVALID_MODEL: the id is free
	const Model* model;
	u32 first;		// in set_entries
	u32 count;		// the meshes of the model
	u32 refs;
	u64 hash;
};

struct GPUVariant {
	u32 group;				// INVALID_GROUP: the id is free
	u32 index_in_group;
	const Mesh* mesh;		// of a variant of a mesh without clusters
	u32 first_index;
	u32 base_vertex;
	u32 index_count;
	u32 refs;				// entries of sets
	u32 next_free;			// in the list of the free variants of its group, or in the list of free ids
	u64 hash;
};

// What the GPU scenes of all the worlds share, owned by the renderer: the models that have instances (the ones with clusters and the ones without), the material sets and
// the draw groups and variants of the draws. The models of each kind are in gpu_cluster_models.h and gpu_lod_models.h, the sets, groups and variants (what is common to
// both) are here. Every entry has a stable id while it is used and is written in place: a model that comes or goes writes its own entries, nothing is rebuilt, and the
// flush uploads only the pages that changed. Main thread only.
struct GPUSceneRegistry {
	GPUSceneRegistry(Renderer& renderer, IAllocator& allocator);

	Renderer& renderer;
	IAllocator& allocator;
	u32 skinned_define_idx = 0;
	u32 reload_counter = 0;
	GPUUploadQueue queue;

	GPUClusterModels clusters;
	GPULodModels lod;

	Array<GPUMaterialSet> sets;
	Array<u32> free_sets;
	HashMap<u64, u32> set_map;
	RangeAllocator set_ranges;
	Array<GPUSetEntry> set_entries;
	// 8 bytes for each entry of the sets: variant (INVALID_VARIANT: not drawn), material index. All the materials of the meshes are here: the default set of
	// each model (GPUClusterMesh::material_entry, GPULodModel::default_first) and the sets of overrides.
	GPUMirror set_table;

	Array<GPUDrawGroup> draw_groups;
	Array<u32> free_groups;
	HashMap<u64, u32> draw_group_map;
	Array<GPUVariant> variants;
	u32 free_variants = GPUSceneConst::INVALID_VARIANT;	// ids without a group
	HashMap<u64, u32> variant_map;
	// 16 bytes for each variant: x = group | index in the group << 16 (0xffffffff: the id is free), first index (in indices, of the index buffer of the draw),
	// base vertex, index count
	GPUMirror variant_table;
};

void destroyGPUSceneRegistry(GPUSceneRegistry& res);

// The id of a model, counted: it exists while it is acquired. The model must be ready, its materials are the ones it has now (a model that is reloaded, or whose
// materials are, is released by all its users first, see RenderModule::modelUnloaded). The model of an id stays the same until it is released. The kind of the model is
// chosen when it is first acquired: an id of a model without clusters has GPUSceneConst::LOD_MODEL_BIT set (isLodModelId).
u32 acquireGPUModel(GPUSceneRegistry& res, const Model& model);
void releaseGPUModel(GPUSceneRegistry& res, u32 model_id);
const Model* getGPUModel(const GPUSceneRegistry& res, u32 model_id);

// The set of the overrides (sorted by mesh) of an instance of the model, counted, INVALID_SET if there are none. Instances of the same model with the same overrides share it.
u32 acquireGPUMaterialSet(GPUSceneRegistry& res, u32 model_id, Span<const GPUMaterialOverride> overrides);
void releaseGPUMaterialSet(GPUSceneRegistry& res, u32 set);
// The set of the model with its own materials, counted: for the models, which have it as the materials of their meshes. Released with releaseGPUMaterialSet.
u32 acquireDefaultSet(GPUSceneRegistry& res, u32 model_id, const Model& model);

// Writes what changed to the GPU. Must be called when the frame is set up, every GPU scene calls it from its flush, it does nothing if nothing changed.
void flushGPUSceneRegistry(GPUSceneRegistry& res);

} // namespace Lumix
