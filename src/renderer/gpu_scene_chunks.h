#pragma once

#include "engine/lumix.h"

#include "core/array.h"
#include "core/math.h"
#include "renderer/gpu/gpu.h"
#include "renderer/gpu_scene.h"
#include "renderer/gpu_table.h"

namespace Lumix {

struct Model;
struct Renderer;
struct GPUSceneRegistry;

// What a world draws from the GPU scene as instanced models (InstancedModel), separate from the model instances of GPUScene (gpu_scene.h). The instances of an instanced
// model are sorted along a curve and cut into chunks of GPUSceneConst::CHUNK_INSTANCES, a chunk is one entry of the table of chunks. A chunk is not drawn itself: the
// culling tests the sphere of the chunk (gpu_scene_cull.hlsl, CHUNK_CULL) and then each of its instances (CHUNK_INSTANCE_CULL), 32 bytes each in a buffer of their own (InstancedModel::InstanceData:
// float4(rotation xyz, lod), float4(position, uniform scale)), relative to the position of the chunk. The work of an instance goes to the same lists as the work of a model
// instance, with the index of the instance (+ 1) in the `instance` word: the index of the chunk then replaces the slot of a model instance (gpu_scene_cull.hlsl, nanite.hlsl,
// gpu_scene_variants.hlsl). Instanced models do not move, so a chunk has no previous transform, and they have the materials of their model.
// The tables are written on the CPU (GPUMirror) when a chunk is made or its position changes.

// Hot part of a chunk is GPUSceneBounds, the sphere around all of its instances, radius < 0 marks a free chunk. Same layout in gpu_scene_cull.hlsl, nanite.hlsl and
// gpu_scene_variants.hlsl.
struct GPUSceneChunk {
	i32 p[3];				// the position of the chunk, fixed point (the instances are relative to it)
	u32 model;				// id of the model (acquireGPUModel)
	u32 instance_buffer;	// bindless handle of the buffer of the instances
	u32 first_instance;		// the instances of the chunk are [first_instance, first_instance + instance_count) of the buffer
	u32 instance_count;
	float instance_radius;	// of the sphere around the origin of an instance, for scale 1
};
static_assert(sizeof(GPUSceneChunk) == 32);

// a buffer of instances that some chunks use, the draws need a barrier for it
struct GPUSceneInstanceBuffer {
	gpu::BufferHandle buffer;
	u32 refs;
};

struct GPUSceneChunks {
	GPUSceneChunks(Renderer& renderer, IAllocator& allocator);

	Renderer& renderer;
	IAllocator& allocator;
	GPUSceneRegistry& registry;
	GPUUploadQueue queue;
	u32 next_slot = 0;				// chunk slots that were used, the table has this many
	Array<u32> free_slots;
	Array<u32> slot_model;			// for each slot: its id in the table of models, INVALID_MODEL if the slot is free
	Array<gpu::BufferHandle> slot_buffers;	// for each slot: its buffer of instances
	Array<GPUSceneInstanceBuffer> instance_buffers;
	GPUMirror bounds;				// GPUSceneBounds[] by slot
	GPUMirror chunks;				// GPUSceneChunk[] by slot
};

// releases what the chunks that are not free have
void destroyGPUSceneChunks(GPUSceneChunks& scene);

// A chunk of an instanced model: `count` instances from `first` in `buffer`, which must exist while the chunk does. The chunk is not drawn until it is moved.
u32 allocSceneChunk(GPUSceneChunks& scene, const Model& model, gpu::BufferHandle buffer, u32 first, u32 count);
void freeSceneChunk(GPUSceneChunks& scene, u32 slot);
// the position of the instances of a chunk (no rotation, no scale) and the sphere around all of them
void moveSceneChunk(GPUSceneChunks& scene, u32 slot, const DVec3& origin, const DVec3& bounds_center, float bounds_radius);

// Writes what changed to the GPU buffers, must be called when the frame is set up. Safe to call many times in a frame.
void uploadGPUSceneChunks(GPUSceneChunks& scene);
// the buffers exist and everything that was changed is in them
bool isGPUSceneChunksUploaded(const GPUSceneChunks& scene);

} // namespace Lumix
