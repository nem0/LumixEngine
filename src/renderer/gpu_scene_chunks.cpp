#include "gpu_scene_chunks.h"

#include "renderer/gpu_scene_registry.h"
#include "renderer/model.h"
#include "renderer/renderer.h"

namespace Lumix {

GPUSceneChunks::GPUSceneChunks(Renderer& renderer, IAllocator& allocator)
	: renderer(renderer)
	, allocator(allocator)
	, registry(renderer.getGPUSceneRegistry())
	, queue(allocator)
	, free_slots(allocator)
	, slot_model(allocator)
	, slot_buffers(allocator)
	, instance_buffers(allocator)
	, bounds(allocator, "gpu scene chunk bounds")
	, chunks(allocator, "gpu scene chunks")
{}

static void refInstanceBuffer(GPUSceneChunks& scene, gpu::BufferHandle buffer) {
	for (GPUSceneInstanceBuffer& b : scene.instance_buffers) {
		if (b.buffer == buffer) {
			++b.refs;
			return;
		}
	}
	scene.instance_buffers.push({ buffer, 1 });
}

static void unrefInstanceBuffer(GPUSceneChunks& scene, gpu::BufferHandle buffer) {
	if (!buffer) return;
	for (u32 i = 0, c = scene.instance_buffers.size(); i < c; ++i) {
		if (scene.instance_buffers[i].buffer != buffer) continue;
		if (--scene.instance_buffers[i].refs == 0) scene.instance_buffers.swapAndPop(i);
		return;
	}
	ASSERT(false);
}

static GPUSceneBounds& boundsOf(GPUSceneChunks& scene, u32 slot) {
	return *(GPUSceneBounds*)mirrorWrite(scene.bounds, slot * sizeof(GPUSceneBounds), sizeof(GPUSceneBounds));
}

static GPUSceneChunk& chunkOf(GPUSceneChunks& scene, u32 slot) {
	return *(GPUSceneChunk*)mirrorWrite(scene.chunks, slot * sizeof(GPUSceneChunk), sizeof(GPUSceneChunk));
}

void destroyGPUSceneChunks(GPUSceneChunks& scene) {
	for (u32 slot = 0; slot < scene.next_slot; ++slot) {
		if (scene.slot_model[slot] == GPUSceneConst::INVALID_MODEL) continue;
		releaseGPUModel(scene.registry, scene.slot_model[slot]);
	}
	mirrorDestroy(scene.bounds, scene.renderer);
	mirrorDestroy(scene.chunks, scene.renderer);
}

u32 allocSceneChunk(GPUSceneChunks& scene, const Model& model, gpu::BufferHandle buffer, u32 first, u32 count) {
	ASSERT(buffer);
	const u32 model_id = acquireGPUModel(scene.registry, model);
	refInstanceBuffer(scene, buffer);

	u32 slot;
	if (!scene.free_slots.empty()) {
		slot = scene.free_slots.back();
		scene.free_slots.pop();
	}
	else {
		slot = scene.next_slot++;
		scene.slot_model.push(GPUSceneConst::INVALID_MODEL);
		scene.slot_buffers.push(gpu::INVALID_BUFFER);
		mirrorResize(scene.bounds, scene.next_slot * sizeof(GPUSceneBounds));
		mirrorResize(scene.chunks, scene.next_slot * sizeof(GPUSceneChunk));
	}
	scene.slot_model[slot] = model_id;
	scene.slot_buffers[slot] = buffer;

	GPUSceneBounds& b = boundsOf(scene, slot);
	memset(&b, 0, sizeof(b));
	b.radius = -1.f;
	GPUSceneChunk& c = chunkOf(scene, slot);
	memset(&c, 0, sizeof(c));
	c.model = model_id;
	c.instance_buffer = gpu::getBindlessHandle(buffer).value;
	c.first_instance = first;
	c.instance_count = count;
	c.instance_radius = model.getOriginBoundingRadius();
	return slot;
}

void freeSceneChunk(GPUSceneChunks& scene, u32 slot) {
	ASSERT(slot < scene.next_slot && scene.slot_model[slot] != GPUSceneConst::INVALID_MODEL);
	unrefInstanceBuffer(scene, scene.slot_buffers[slot]);
	releaseGPUModel(scene.registry, scene.slot_model[slot]);
	scene.slot_model[slot] = GPUSceneConst::INVALID_MODEL;
	scene.slot_buffers[slot] = gpu::INVALID_BUFFER;

	GPUSceneBounds& b = boundsOf(scene, slot);
	memset(&b, 0, sizeof(b));
	b.radius = -1.f;
	memset(&chunkOf(scene, slot), 0, sizeof(GPUSceneChunk));
	scene.free_slots.push(slot);
}

void moveSceneChunk(GPUSceneChunks& scene, u32 slot, const DVec3& origin, const DVec3& bounds_center, float bounds_radius) {
	ASSERT(slot < scene.next_slot);
	GPUSceneBounds& b = boundsOf(scene, slot);
	b.p[0] = toFixedPosition(bounds_center.x);
	b.p[1] = toFixedPosition(bounds_center.y);
	b.p[2] = toFixedPosition(bounds_center.z);
	b.radius = bounds_radius;
	GPUSceneChunk& c = chunkOf(scene, slot);
	c.p[0] = toFixedPosition(origin.x);
	c.p[1] = toFixedPosition(origin.y);
	c.p[2] = toFixedPosition(origin.z);
}

bool isGPUSceneChunksUploaded(const GPUSceneChunks& scene) {
	return scene.bounds.buffer && scene.chunks.buffer && !scene.bounds.any_dirty && !scene.chunks.any_dirty;
}

void uploadGPUSceneChunks(GPUSceneChunks& scene) {
	mirrorUpload(scene.bounds, scene.renderer, scene.queue);
	mirrorUpload(scene.chunks, scene.renderer, scene.queue);
	submitCopies(scene.queue, scene.renderer, "gpu scene chunks");
}

} // namespace Lumix
