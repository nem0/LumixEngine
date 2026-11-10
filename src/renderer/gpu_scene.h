#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/math.h"
#include "renderer/gpu/gpu.h"

namespace Lumix {

struct DrawStream;
struct Renderer;
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
	constexpr u32 CLUSTER_THREADS = 64;
}

// Static data of a mesh with clusters, 64 bytes, read by the passes that select clusters. The meshes of a model are consecutive (GPUSceneModel).
// Same layout in gpu_scene_cull.hlsl.
struct GPUSceneMesh {
	enum Flags : u32 { FLAT = 1 << 0 };
	u32 clusters;			// bindless, MeshClusterGPU[]
	u32 nodes;				// bindless, MeshNodeGPU[node_count], then the indices of root_count roots
	u32 cluster_count;
	u32 node_count;
	u32 root_count;
	u32 roots_offset;		// byte offset of the roots in nodes
	u32 tree_depth;
	u32 index_stride;
	u32 leaf_cluster_count;
	u32 flags;
	u32 flat_groups;		// groups of a flat mesh for an instance
	u32 vertices;			// bindless, the headers of the clusters followed by their packed vertices (vertex_pulling.hlsli)
	u32 indices;			// bindless, u16 indices local to a cluster, cluster `c` starts at c * index_stride
	u32 padding[3];
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

// The meshes of a model of the table of models, 8 bytes. A model without clusters has no meshes.
struct GPUSceneModel {
	u32 first_mesh;
	u32 mesh_count;
};

// Hot part: what a pass that tests every instance needs. radius < 0 marks a free or disabled slot.
// Same layout in gpu_scene.hlsl.
struct GPUSceneBounds {
	i32 p[3];
	float radius;
};
static_assert(sizeof(GPUSceneBounds) == 16);

// Cold part: read for visible instances only. The layout of the DYNAMIC instance (see nanite.hlsl, 96 bytes) with positions in fixed point.
struct GPUSceneInstance {
	Quat rot;
	i32 p[3];
	u32 mesh_id;
	Vec3 scale;
	u32 flags;
	Quat prev_rot;
	i32 prev_p[3];
	u32 padding0;
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
	void update(u32 slot, const Transform& transform, float radius);

	// Instanced models with clusters. A group is not a slot: its instances stay in the buffer of the instanced model, the origin is the position of the entity.
	// The model has an entry in the table of models while the group exists. Changes reach the GPU with the next flush().
	u32 allocGroup(const Model* model);
	void updateGroup(u32 group, const DVec3& origin, gpu::BufferHandle instances, u32 instance_count);
	void freeGroup(u32 group);
	gpu::BufferHandle getGroupTableBuffer() const { return m_group_buffer; }	// GPUSceneGroup[] of the groups that have instances, valid after flush()
	u32 getGroupCount() const { return m_group_table.size(); }
	u32 getGroupInstanceCount() const { return m_group_instances; }		// of all the groups
	Span<const gpu::BufferHandle> getGroupInstanceBuffers() const { return Span<const gpu::BufferHandle>(m_group_buffers.begin(), m_group_buffers.size()); }

	// The table of models: an entry exists while at least one slot (or group) has the model. Entries of the same model have the same id as long as
	// it has an instance, ids of models without instances are reused.
	u32 getModelId(const Model* model) const;
	u32 getSlotModelId(u32 slot) const { return m_slot_model[slot]; }
	u32 getModelTableSize() const { return m_models.size(); }		// the highest id + 1
	// the meshes of the model in the mesh table (consecutive, the mesh `i` of the model is first + i), valid after flush()
	u32 getModelFirstMesh(u32 id) const { return m_model_table[id].first_mesh; }
	u32 getModelMeshCount(u32 id) const { return m_model_table[id].mesh_count; }
	u32 getMaxTreeDepth() const { return m_max_tree_depth; }	// of the meshes of the mesh table
	// the entries of the mesh table: the model and the index of the mesh in it
	u32 getMeshCount() const { return m_mesh_table.size(); }
	const Model* getMeshModel(u32 mesh) const { return m_mesh_models[mesh]; }
	u32 getMeshIndexInModel(u32 mesh) const { return m_mesh_indices[mesh]; }

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
	struct ModelEntry {
		const Model* model = nullptr;
		u32 instance_count = 0;
	};
	Array<ModelEntry> m_models;		// by id, the first MAX_MODELS entries

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
	Array<const Model*> m_mesh_models;	// for each entry of m_mesh_table
	Array<u32> m_mesh_indices;
	gpu::BufferHandle m_mesh_buffer = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_model_buffer = gpu::INVALID_BUFFER;
	u32 m_mesh_capacity = 0;		// of the buffers, in entries
	u32 m_model_capacity = 0;
	bool m_tables_dirty = false;
	u32 m_max_tree_depth = 0;
	Array<u32> m_moved_now;			// slots written in the frame of the last flush, their previous state has to catch up in the next frame
};

} // namespace Lumix
