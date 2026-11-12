#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/hash_map.h"
#include "core/math.h"
#include "renderer/gpu/gpu.h"

namespace Lumix {

struct DrawStream;
struct Renderer;
struct Material;
struct Mesh;
struct Model;
struct Shader;
struct Transform;

// GPU copy of the model instances ("GPU scene"), indexed by slot. Positions are 32 bit fixed point: `POS_UNIT` (a power of two, so that the
// conversion to float is exact) per step, which is a range of +-2097 km around the world origin. A position relative to the camera is
//    float3(p - cam_p) * POS_UNIT - cam_frac
// where cam_p is the camera rounded to the grid and cam_frac the remainder, see GPUSceneView. The integer subtraction is exact, so the
// precision depends only on the distance to the camera, not on the distance to the origin.
namespace GPUSceneConst {
	constexpr double POS_UNIT = 1.0 / 1024.0;
	constexpr double POS_LIMIT = 2147483647.0 * POS_UNIT;
	constexpr u32 INVALID_SLOT = 0xffFFffFF;
	constexpr u32 INITIAL_CAPACITY = 1024;
	// models of the scene that have a table entry (mesh_id), instances of the other ones have INVALID_MODEL
	constexpr u32 MAX_MODELS = 4096;
	constexpr u32 INVALID_MODEL = 0xffFFffFF;
	// meshes with up to that many clusters are tested cluster by cluster (flat), groups of CLUSTER_THREADS clusters, see pipeline.cpp
	constexpr u32 FLAT_MAX_CLUSTERS = 512;
	// A variant is what one command of an indirect draw draws: a mesh (meshes with clusters share one for all the meshes with the same program) with a material
	// program. Variants of the same program (and layer) are a draw group, which is drawn by one indirect call of one command for each variant.
	constexpr u32 MAX_VARIANTS = 8192;
	constexpr u32 MAX_DRAW_GROUPS = 2048;
	constexpr u32 INVALID_VARIANT = 0xffFFffFF;
	constexpr u32 CLUSTER_THREADS = 64;
}

// Static data of a mesh with clusters, 64 bytes, read by the passes that select clusters. The meshes of a model are consecutive (GPUSceneModel).
// Same layout in gpu_scene_cull.hlsl.
struct GPUSceneMesh {
	// The mesh of a model without clusters (GPUSceneModel::PLAIN) has only the last three fields, its vertices and indices are in ranges of the buffer of cluster data
	// (drawn by one command of an indirect draw for each mesh and material), the other fields are 0
	enum Flags : u32 { FLAT = 1 << 0 };
	u32 cluster_groups;		// byte offset in the buffer of cluster data (Renderer::getClusterDataBuffer) of u32[cluster_count]: the group of each cluster. The clusters are in the payloads of the groups
	u32 nodes;				// byte offset in the same buffer of MeshNodeGPU[node_count], then the indices of root_count roots
	u32 cluster_count;
	u32 node_count;
	u32 root_count;
	u32 roots_offset;		// byte offset of the roots in nodes
	u32 tree_depth;
	u32 index_stride;
	u32 leaf_cluster_count;
	u32 flags;
	u32 flat_groups;		// groups of a flat mesh for an instance
	u32 padding0;
	u32 indices;			// byte offset in the same buffer of MeshGroupAddressGPU[], where the clusters, vertices and the u8 indices (local to a cluster, `index_stride` for each) of each group are
	u32 material_entry;		// the entry of the mesh in the default material set of its model (GPUScene::getMaterialSetBuffer): its variant and material index
	u32 index_in_model;		// the index of the mesh in its model
	u32 padding;
};
static_assert(sizeof(GPUSceneMesh) == 64);

// An instanced model with clusters (InstancedModel of render_module.h): many instances in a buffer of their own, local to an origin, 32 bytes. The culling
// of instances (gpu_scene_cull.hlsl) tests the instances of all the groups one by one.
struct GPUSceneGroup {
	i32 p[3];				// the origin, fixed point
	u32 instance_count;
	u32 instances;			// bindless, InstanceData: float4(rotation xyz, lod), float4(position, uniform scale)
	u32 model;				// id in the table of models
	u32 first_instance;		// the instances of the groups before this one: the instance number `i` of all the groups is of the group that has the last first_instance <= i
	float radius;			// of the sphere around the origin of the model, for scale 1
};
static_assert(sizeof(GPUSceneGroup) == 32);

// The meshes of a model of the table of models, 48 bytes, the same layout in gpu_scene_cull.hlsl. A model that can not be drawn from the scene has no meshes.
// A model with clusters has the meshes of its first LOD. A PLAIN model has all its meshes (the mesh `i` of the model is first_mesh + i), the
// meshes of the LOD `l` are lod[l]: the first mesh in the low 16 bits and the last + 1 in the high 16 bits (0: the LOD has no meshes). The LOD of an
// instance is the first one with squared distance < lod_distance[l] (the last LOD if there is none), see emitPlain in gpu_scene_cull.hlsl.
struct GPUSceneModel {
	enum Flags : u32 { PLAIN = 1 << 0 };
	u32 first_mesh;
	u32 mesh_count;
	u32 flags;
	u32 lod4;
	u32 lod[4];
	float lod_distance[4];
};
static_assert(sizeof(GPUSceneModel) == 48);

// Hot part: what a pass that tests every instance needs. radius < 0 marks a free or disabled slot.
// Same layout in gpu_scene.hlsl.
struct GPUSceneBounds {
	i32 p[3];
	float radius;
};
static_assert(sizeof(GPUSceneBounds) == 16);

// Cold part: read for visible instances only. The layout of the MESH_INSTANCE instance (see nanite.hlsl, 96 bytes) with positions in fixed point.
struct GPUSceneInstance {
	enum Flags : u32 {
		FORCED_LOD = 1 << 0		// the LOD is in the bits 1..3
	};
	static constexpr u32 NO_MATERIAL_SET = 0xffFFffFF;
	Quat rot;
	i32 p[3];
	u32 mesh_id;
	Vec3 scale;
	u32 flags;
	Quat prev_rot;
	i32 prev_p[3];
	u32 material_set;	// the first entry of the material set of the instance (GPUScene::setOverrides), NO_MATERIAL_SET: it has the materials of its model
	Vec3 prev_scale;
	u32 padding1;
};
static_assert(sizeof(GPUSceneInstance) == 96);

// Camera in the units of the scene. The same for the camera of the drawing and the camera of the culling, which may differ.
struct GPUSceneView {
	i32 cam_p[3];
	float unit;
	float cam_frac[3];
	float padding;
};

inline i32 toFixedPosition(double v) {
	ASSERT(v > -GPUSceneConst::POS_LIMIT && v < GPUSceneConst::POS_LIMIT);
	const double limit = GPUSceneConst::POS_LIMIT;
	v = v < -limit ? -limit : (v > limit ? limit : v);
	const double x = v / GPUSceneConst::POS_UNIT;
	return (i32)(x < 0 ? x - 0.5 : x + 0.5);	// round half away from zero
}

GPUSceneView makeGPUSceneView(const DVec3& camera);

struct GPUScene {
	GPUScene(Renderer& renderer, IAllocator& allocator);
	~GPUScene();

	// A slot stays valid until it is freed. The data of a new slot is written by the next update(), until then it is disabled.
	// The slot belongs to the model: its table index (the mesh_id of the instance) is the same until the slot is freed.
	u32 alloc(const Model* model);
	void free(u32 slot);
	// `radius`: of the sphere around the position of the instance that contains the whole instance, in world units
	// `forced_lod`: the LOD of the instance if it is not chosen by the distance (-1: by the distance), only for models without clusters
	void update(u32 slot, const Transform& transform, float radius, i32 forced_lod = -1);

	// Materials of instances that are not the ones of their model: the meshes of a slot that have their own material (`mesh` is the index of the mesh in its model).
	// The slot gets a material set: the variant and material index of every mesh of its model, the meshes without an override have the ones of the model. Slots of the
	// same model with the same overrides share a set. The same for all the views, the sets have their variants (see below). The set reaches the instance with its
	// next update() (material_set).
	struct Override {
		u32 mesh;
		u32 material_index;
		const Material* material;
	};
	// replaces the overrides of the slot (sorted by mesh, the model of the slot is ready), the slot is freed with no overrides, no overrides: the materials of the model
	void setOverrides(u32 slot, Span<const Override> overrides);
	// 8 bytes for each entry of the sets: variant (INVALID_VARIANT: not drawn), material index. All the materials of the meshes are here, the default set of
	// each model in the table (the materials of the model, GPUSceneMesh::material_entry) and the sets of overrides. Valid after flush()
	gpu::BufferHandle getMaterialSetBuffer() const { return m_set_buffer; }

	// Draw groups and variants, see GPUSceneConst::MAX_VARIANTS. They are made by flush() for the meshes of the mesh table and for the entries of the table of overrides,
	// and stay the same until the tables are rebuilt (a model enters or leaves, a material is unloaded), the pipelines only read them.
	struct DrawGroup {
		Shader* shader;
		u32 define_mask;				// of the material, with SKINNED
		gpu::StateFlags state;			// of the material
		gpu::PrimitiveType primitive_type;
		const char* semantics_defines;
		u8 layer;
		bool plain;						// meshes without clusters, otherwise the group has clusters
		gpu::DataType index_type;		// of the commands
		u32 variant_count;				// the commands of the group are consecutive
	};
	u32 getDrawGroupCount() const { return m_draw_groups.size(); }
	const DrawGroup& getDrawGroup(u32 i) const { return m_draw_groups[i]; }
	u32 getVariantCount() const { return m_variants.size(); }
	// 16 bytes each: x = group | index in the group << 16, first index (in indices, of the index buffer of the draw), base vertex, index count. Valid after flush().
	gpu::BufferHandle getVariantBuffer() const { return m_variant_buffer; }

	// Instanced models with clusters. A group is not a slot: its instances stay in the buffer of the instanced model, the origin is the position of the entity.
	// The model has an entry in the table of models while the group exists. Changes reach the GPU with the next flush().
	u32 allocGroup(const Model* model);
	void updateGroup(u32 group, const DVec3& origin, gpu::BufferHandle instances, u32 instance_count);
	void freeGroup(u32 group);
	// The bones of skinned instances of the frame: the CPU converts the poses of the instances once per frame (Pipeline::prepareSkinnedPoses) into one buffer
	// of dual quaternions (32 bytes each) and a table of the byte offset of the bones of each slot (u32, 0 for the slots without bones).
	struct FrameBones {
		u32 frame = 0xffFFffFF;
		gpu::BufferHandle bones = gpu::INVALID_BUFFER;
		u32 bones_offset = 0;
		gpu::BufferHandle table = gpu::INVALID_BUFFER;
		u32 table_offset = 0;
	};
	FrameBones& getFrameBones() { return m_frame_bones; }

	gpu::BufferHandle getGroupTableBuffer() const { return m_group_buffer; }	// GPUSceneGroup[] of the groups that have instances, valid after flush()
	u32 getGroupCount() const { return m_group_table.size(); }
	u32 getGroupInstanceCount() const { return m_group_instances; }		// of all the groups
	Span<const gpu::BufferHandle> getGroupInstanceBuffers() const { return Span<const gpu::BufferHandle>(m_group_buffers.begin(), m_group_buffers.size()); }

	// The table of models: an entry exists while at least one slot (or group) has the model. Entries of the same model have the same id as long as
	// it has an instance, ids of models without instances are reused.
	u32 getModelTableSize() const { return m_models.size(); }		// the highest id + 1
	u32 getMaxTreeDepth() const { return m_max_tree_depth; }	// of the meshes of the mesh table
	u32 getMeshTableVersion() const { return m_mesh_table_version; }	// changes whenever the mesh table is rebuilt

	// Writes what changed since the last call to the GPU buffers. Safe to call many times in a frame (every pipeline does).
	void flush();

	gpu::BufferHandle getMeshTableBuffer() const { return m_mesh_buffer; }		// GPUSceneMesh[], valid after flush()
	gpu::BufferHandle getModelTableBuffer() const { return m_model_buffer; }	// GPUSceneModel[] by id of the table of models
	gpu::BufferHandle getBoundsBuffer() const { return m_bounds; }
	gpu::BufferHandle getInstancesBuffer() const { return m_instances; }
	u32 getCapacity() const { return m_capacity; }
	// the buffers exist and everything that was changed is in them (after flush())
	bool isUploaded() const { return m_bounds != gpu::INVALID_BUFFER && m_entries.empty(); }

	// one entry of the upload, 128 bytes, the layout of gpu_scene.hlsl
	struct Entry {
		enum Flags : u32 {
			HAS_NEW = 1 << 0,	// otherwise the instance did not change, but it did in the last frame: the previous state becomes the current one
			TELEPORT = 1 << 1	// the previous state is the new one (the instance is new, or was disabled)
		};
		u32 slot;
		u32 flags;
		u32 padding[2];
		GPUSceneBounds bounds;
		GPUSceneInstance instance;
	};
	static_assert(sizeof(Entry) == 128);

private:
	void grow(u32 min_capacity);
	Entry& entryFor(u32 slot);

	FrameBones m_frame_bones;
	Renderer& m_renderer;
	IAllocator& m_allocator;
	Shader* m_shader = nullptr;
	gpu::BufferHandle m_bounds = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_instances = gpu::INVALID_BUFFER;
	u32 m_capacity = 0;				// slots the buffers need to have
	u32 m_gpu_capacity = 0;			// slots they have
	u32 m_next_slot = 0;
	u32 m_frame = 0xffFFffFF;		// of the last flush
	Array<u32> m_free_slots;
	Array<Entry> m_entries;			// pending, at most one for a slot
	Array<u32> m_pending;			// for each slot: its index in m_entries or INVALID_SLOT
	Array<u32> m_fresh;				// for each slot: 1 until its first update (teleport)
	Array<u32> m_slot_model;		// for each slot: its id in the table of models
	Array<u32> m_slot_set;			// for each slot: its material set or INVALID_SLOT
	// A material set is a range of m_set_entries, one for each mesh of the model. The entries of the meshes without an override are the materials of the model
	// (`own` is false), they are read again when the sets are made again.
	struct SetEntry {
		const Material* material;	// of an own entry, the instances that use the set have it (it has no reference of the set)
		u32 material_index;
		u32 variant;
		bool own;
	};
	struct MaterialSet {
		const Model* model = nullptr;	// null: the set is free, its range is reused for a model with as many meshes
		u32 model_id = GPUSceneConst::INVALID_MODEL;
		u32 first = 0;
		u32 count = 0;
		u32 refs = 0;
		u64 hash = 0;
	};
	Array<MaterialSet> m_sets;
	Array<SetEntry> m_set_entries;
	Array<u32> m_free_sets;
	HashMap<u64, u32> m_set_map;
	gpu::BufferHandle m_set_buffer = gpu::INVALID_BUFFER;
	u32 m_set_capacity = 0;
	bool m_sets_dirty = true;		// the first flush makes the buffer
	void releaseSet(u32 set);
	// the set of the model with the overrides (none: the default set of the model), it is made if there is none. Its refs are the ones of the caller.
	u32 acquireSet(u32 model_id, const Model* model, u32 count, Span<const Override> overrides);
	bool sameSet(const MaterialSet& set, u32 model_id, Span<const Override> overrides) const;
	// the vertices and indices of a mesh without clusters, a copy in the buffer of cluster data (the model keeps its own buffers for the other uses of it)
	struct MeshPool {
		u32 vertex_offset;
		u32 vertex_size;
		u32 index_offset;
		u32 index_size;
	};
	struct ModelEntry {
		const Model* model = nullptr;
		u32 instance_count = 0;
		MeshPool* pools = nullptr;		// a model without clusters while it is in the table, for each mesh
		u32 pool_count = 0;
		u32 default_set = GPUSceneConst::INVALID_SLOT;	// the set with the materials of the model, while the model is in the table and ready
	};
	Array<ModelEntry> m_models;		// by id, the first MAX_MODELS entries

	struct Variant {
		u32 group;
		u32 index_in_group;
		const Mesh* mesh;	// of a variant of meshes without clusters
		u32 first_index;
		u32 base_vertex;
		u32 index_count;
	};
	struct PoolCopy {
		gpu::BufferHandle src;
		u32 dst_offset;
		u32 size;
	};
	bool allocPools(u32 model_id, const Model& model);
	void freePools(u32 model_id);
	u32 findOrAddDrawGroup(const DrawGroup& key);
	u32 variantFor(u32 model_id, const Model& model, u32 mesh_idx, const Material* material);
	Array<DrawGroup> m_draw_groups;
	HashMap<u64, u32> m_draw_group_map;
	Array<Variant> m_variants;
	HashMap<u64, u32> m_variant_map;
	Array<PoolCopy> m_pool_copies;	// pending copies from the buffers of the meshes to the ranges of the pool
	gpu::BufferHandle m_variant_buffer = gpu::INVALID_BUFFER;
	u32 m_variant_capacity = 0;
	bool m_variants_dirty = true;
	u32 m_registry_reload_counter = 0;
	u32 m_skinned_define_idx = 0;

	u32 getModelId(const Model* model) const;
	u32 acquireModel(const Model* model);	// the id of the model in the table, it is made if there is none, INVALID_MODEL if the table is full
	void releaseModel(u32 model_id);
	void rebuildGroupTable();
	void ensureTableBuffer(gpu::BufferHandle& buffer, u32& capacity, u32 needed, u32 entry_size, const char* name);

	struct Group {
		const Model* model = nullptr;
		u32 model_id = GPUSceneConst::INVALID_MODEL;
		i32 p[3] = {};
		gpu::BufferHandle data = gpu::INVALID_BUFFER;
		u32 count = 0;
		bool used = false;
	};
	Array<Group> m_groups;			// by the id of the group
	Array<u32> m_free_groups;
	Array<GPUSceneGroup> m_group_table;	// the groups that have instances, as the GPU has them
	Array<gpu::BufferHandle> m_group_buffers;	// their buffers of instances
	gpu::BufferHandle m_group_buffer = gpu::INVALID_BUFFER;
	u32 m_group_capacity = 0;
	u32 m_group_instances = 0;
	bool m_groups_dirty = true;		// the first flush makes the buffer

	// tables of the static data of models, rebuilt as a whole when a model enters or leaves the table
	void rebuildModelTables();
	Array<GPUSceneMesh> m_mesh_table;
	Array<GPUSceneModel> m_model_table;
	gpu::BufferHandle m_mesh_buffer = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_model_buffer = gpu::INVALID_BUFFER;
	u32 m_mesh_capacity = 0;		// of the buffers, in entries
	u32 m_model_capacity = 0;
	bool m_tables_dirty = false;
	u32 m_mesh_table_version = 0;
	u32 m_max_tree_depth = 0;
	Array<u32> m_moved_now;			// slots written in the frame of the last flush, their previous state has to catch up in the next frame
};

} // namespace Lumix
