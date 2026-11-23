#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/math.h"
#include "renderer/gpu/gpu.h"
#include "renderer/gpu_scene_registry.h"

namespace Lumix {

struct DrawStream;
struct Renderer;
struct Model;
struct Shader;
struct Transform;

// GPU copy of what a world draws from the GPU scene, indexed by slot. A slot is a model instance. Instanced models are in gpu_scene_chunks.h.
// Positions are 32 bit fixed point: `POS_UNIT` (a power of two, so that the conversion to float is exact) per step, which is a range of +-2097 km around the world
// origin. A position relative to the camera is
//    float3(p - cam_p) * POS_UNIT - cam_frac
// where cam_p is the camera rounded to the grid and cam_frac the remainder, see GPUSceneView. The integer subtraction is exact, so the
// precision depends only on the distance to the camera, not on the distance to the origin.

// Hot part of a model instance: what a pass that tests every model instance needs, the sphere around all of it (all its instances). radius < 0 marks a free or disabled slot.
// Same layout in gpu_scene.hlsl and gpu_scene_cull.hlsl.
struct GPUSceneBounds {
	i32 p[3];
	float radius;
};
static_assert(sizeof(GPUSceneBounds) == 16);

// Cold part of a model instance, 96 bytes: read for visible instances only. The same layout in gpu_scene.hlsl, gpu_scene_cull.hlsl, nanite.hlsl and gpu_scene_variants.hlsl.
// The previous transform (prev_*) is valid only if moved_frame is the number of the current frame (the instance was moved in this frame), otherwise the instance did
// not move since the last frame and the previous transform is the current one.
struct GPUSceneInstance {
	enum Flags : u32 {
		FORCED_LOD = 1 << 0		// the LOD is in the bits 1..3
	};
	static constexpr u32 NO_MATERIAL_SET = 0xffFFffFF;
	static constexpr u32 NO_STATE = 0xffFFffFF;	// moved_frame of a slot that has no transform yet

	Quat rot;
	i32 p[3];
	u32 model;				// id of the model (acquireGPUModel), the bit LOD_MODEL_BIT: a model without clusters
	Vec3 scale;
	u32 flags;
	Quat prev_rot;
	i32 prev_p[3];
	u32 material_set;		// the first entry of the material set (GPUSceneRegistry::set_table), NO_MATERIAL_SET: it has the materials of its model
	Vec3 prev_scale;
	u32 moved_frame;		// the frame of the last change of the transform
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

// One change of a slot, 128 bytes, the layout of gpu_scene.hlsl. Only the fields of the parts in `mask` are written, so the parts change independently.
struct GPUSceneEntry {
	enum Mask : u32 {
		TRANSFORM = 1 << 0,		// bounds, rot, p, scale (the old transform becomes the previous one)
		FRESH = 1 << 1,			// the slot has no transform yet: the next transform is also the previous one
		MATERIAL_SET = 1 << 2,	// material_set
		STATIC = 1 << 3,		// model, flags
		DISABLE = 1 << 4		// the slot is not drawn (radius < 0)
	};
	u32 slot;
	u32 mask;
	u32 padding[2];
	GPUSceneBounds bounds;
	GPUSceneInstance instance;
};
static_assert(sizeof(GPUSceneEntry) == 128);

// The bones of skinned instances of the frame: the CPU converts the poses of the instances once per frame (Pipeline::prepareSkinnedPoses) into one buffer
// of dual quaternions (32 bytes each) and a table of the byte offset of the bones of each slot (u32, 0 for the slots without bones).
struct GPUSceneFrameBones {
	u32 frame = 0xffFFffFF;
	gpu::BufferHandle bones = gpu::INVALID_BUFFER;
	u32 bones_offset = 0;
	gpu::BufferHandle table = gpu::INVALID_BUFFER;
	u32 table_offset = 0;
};

struct GPUScene {
	GPUScene(Renderer& renderer, IAllocator& allocator);

	Renderer& renderer;
	IAllocator& allocator;
	GPUSceneRegistry& registry;
	Shader* shader = nullptr;
	gpu::BufferHandle bounds = gpu::INVALID_BUFFER;		// GPUSceneBounds[capacity]
	gpu::BufferHandle instances = gpu::INVALID_BUFFER;	// GPUSceneInstance[capacity]
	u32 capacity = 0;			// slots the buffers need to have
	u32 gpu_capacity = 0;		// slots they have
	u32 next_slot = 0;
	Array<u32> free_slots;
	Array<GPUSceneEntry> entries;	// pending, at most one for a slot
	Array<u32> pending;				// for each slot: its index in entries or INVALID_SLOT
	Array<u32> slot_model;			// for each slot: its id in the table of models, INVALID_MODEL if the slot is free
	Array<u32> slot_set;			// for each slot: its material set or INVALID_SET
	GPUSceneFrameBones frame_bones;
};

// releases what the slots that are not free have
void destroyGPUScene(GPUScene& scene);

// A slot stays valid until it is freed, it is disabled until its first transform. The slot has the model (its id in the table of models) until it is freed.
u32 allocSceneSlot(GPUScene& scene, const Model& model);
void freeSceneSlot(GPUScene& scene, u32 slot);
// `radius`: of the sphere around the position of the instance that contains the whole instance, in world units
// `forced_lod`: the LOD of the instance if it is not chosen by the distance (-1: by the distance), only for models without clusters
void setSceneTransform(GPUScene& scene, u32 slot, const Transform& transform, float radius, i32 forced_lod);
// replaces the material overrides of the slot (sorted by mesh), none: the materials of the model
void setSceneOverrides(GPUScene& scene, u32 slot, Span<const GPUMaterialOverride> overrides);

// Writes what changed since the last call to the GPU buffers (also of the resources). Safe to call many times in a frame (every pipeline does), must be called when the
// frame is set up.
void uploadGPUScene(GPUScene& scene);
// the buffers exist and everything that was changed is in them
bool isGPUSceneUploaded(const GPUScene& scene);

} // namespace Lumix
