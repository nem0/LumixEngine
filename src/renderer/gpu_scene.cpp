#include "gpu_scene.h"

#include "core/path.h"
#include "core/profiler.h"
#include "engine/engine.h"
#include "engine/resource_manager.h"
#include "renderer/draw_stream.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"

namespace Lumix {

// the uniforms of gpu_scene.hlsl
struct GPUSceneScatterUniforms {
	gpu::BindlessHandle entries;
	u32 entries_offset;
	u32 entry_count;
	gpu::RWBindlessHandle bounds;
	gpu::RWBindlessHandle instances;
	u32 frame;
	u32 padding[2];
};
static_assert(sizeof(GPUSceneScatterUniforms) == 32);

GPUSceneView makeGPUSceneView(const DVec3& camera) {
	GPUSceneView r;
	r.cam_p[0] = toFixedPosition(camera.x);
	r.cam_p[1] = toFixedPosition(camera.y);
	r.cam_p[2] = toFixedPosition(camera.z);
	r.unit = (float)GPUSceneConst::POS_UNIT;
	// the remainder is in [-unit / 2, unit / 2], a double has the precision for it
	r.cam_frac[0] = (float)(camera.x - (double)r.cam_p[0] * GPUSceneConst::POS_UNIT);
	r.cam_frac[1] = (float)(camera.y - (double)r.cam_p[1] * GPUSceneConst::POS_UNIT);
	r.cam_frac[2] = (float)(camera.z - (double)r.cam_p[2] * GPUSceneConst::POS_UNIT);
	r.padding = 0;
	return r;
}

GPUScene::GPUScene(Renderer& renderer, IAllocator& allocator)
	: renderer(renderer)
	, allocator(allocator)
	, registry(renderer.getGPUSceneRegistry())
	, free_slots(allocator)
	, entries(allocator)
	, pending(allocator)
	, slot_model(allocator)
	, slot_set(allocator)
{
	capacity = GPUSceneConst::INITIAL_CAPACITY;
	shader = renderer.getEngine().getResourceManager().load<Shader>(Path("engine/shaders/gpu_scene.hlsl"));
}

void destroyGPUScene(GPUScene& scene) {
	for (u32 slot = 0; slot < scene.next_slot; ++slot) {
		if (scene.slot_model[slot] == GPUSceneConst::INVALID_MODEL) continue;
		releaseGPUMaterialSet(scene.registry, scene.slot_set[slot]);
		releaseGPUModel(scene.registry, scene.slot_model[slot]);
		scene.slot_model[slot] = GPUSceneConst::INVALID_MODEL;
		scene.slot_set[slot] = GPUSceneConst::INVALID_SET;
	}
	DrawStream& stream = scene.renderer.getEndFrameDrawStream();
	if (scene.bounds) stream.destroy(scene.bounds);
	if (scene.instances) stream.destroy(scene.instances);
	scene.shader->decRefCount();
}

static GPUSceneEntry& entryFor(GPUScene& scene, u32 slot) {
	ASSERT(slot < scene.next_slot);
	if (scene.pending[slot] != GPUSceneConst::INVALID_SLOT) return scene.entries[scene.pending[slot]];
	scene.pending[slot] = scene.entries.size();
	GPUSceneEntry& e = scene.entries.emplace();
	memset(&e, 0, sizeof(e));
	e.slot = slot;
	return e;
}

u32 allocSceneSlot(GPUScene& scene, const Model& model) {
	const u32 model_id = acquireGPUModel(scene.registry, model);

	u32 slot;
	if (!scene.free_slots.empty()) {
		slot = scene.free_slots.back();
		scene.free_slots.pop();
	}
	else {
		slot = scene.next_slot++;
		while (scene.next_slot > scene.capacity) scene.capacity *= 2;
		scene.pending.push(GPUSceneConst::INVALID_SLOT);
		scene.slot_model.push(GPUSceneConst::INVALID_MODEL);
		scene.slot_set.push(GPUSceneConst::INVALID_SET);
	}
	scene.slot_model[slot] = model_id;
	scene.slot_set[slot] = GPUSceneConst::INVALID_SET;

	// a slot that was freed and allocated again in the same frame gets a new entry, the old one is for its previous user
	GPUSceneEntry& e = entryFor(scene, slot);
	memset(&e, 0, sizeof(e));
	e.slot = slot;
	e.mask = GPUSceneEntry::DISABLE | GPUSceneEntry::FRESH | GPUSceneEntry::STATIC | GPUSceneEntry::MATERIAL_SET;
	e.bounds.radius = -1.f;
	e.instance.model = model_id;
	e.instance.material_set = GPUSceneInstance::NO_MATERIAL_SET;
	return slot;
}

void freeSceneSlot(GPUScene& scene, u32 slot) {
	ASSERT(slot < scene.next_slot && scene.slot_model[slot] != GPUSceneConst::INVALID_MODEL);
	releaseGPUMaterialSet(scene.registry, scene.slot_set[slot]);
	releaseGPUModel(scene.registry, scene.slot_model[slot]);
	scene.slot_model[slot] = GPUSceneConst::INVALID_MODEL;
	scene.slot_set[slot] = GPUSceneConst::INVALID_SET;

	GPUSceneEntry& e = entryFor(scene, slot);
	memset(&e.bounds, 0, sizeof(e.bounds));
	memset(&e.instance, 0, sizeof(e.instance));
	e.mask = GPUSceneEntry::DISABLE | GPUSceneEntry::STATIC | GPUSceneEntry::MATERIAL_SET;
	e.bounds.radius = -1.f;
	e.instance.model = GPUSceneConst::INVALID_MODEL;
	e.instance.material_set = GPUSceneInstance::NO_MATERIAL_SET;
	scene.free_slots.push(slot);
}

void setSceneTransform(GPUScene& scene, u32 slot, const Transform& tr, float radius, i32 forced_lod) {
	GPUSceneEntry& e = entryFor(scene, slot);
	e.mask = (e.mask & ~GPUSceneEntry::DISABLE) | GPUSceneEntry::TRANSFORM | GPUSceneEntry::STATIC;

	e.bounds.p[0] = toFixedPosition(tr.pos.x);
	e.bounds.p[1] = toFixedPosition(tr.pos.y);
	e.bounds.p[2] = toFixedPosition(tr.pos.z);
	e.bounds.radius = radius;

	GPUSceneInstance& p = e.instance;
	p.rot = tr.rot;
	memcpy(p.p, e.bounds.p, sizeof(p.p));
	p.scale = tr.scale;
	p.model = scene.slot_model[slot];
	p.flags = forced_lod >= 0 ? GPUSceneInstance::FORCED_LOD | (u32(minimum(forced_lod, 4)) << 1) : 0;
}

void setSceneOverrides(GPUScene& scene, u32 slot, Span<const GPUMaterialOverride> overrides) {
	ASSERT(slot < scene.next_slot);
	const u32 set = acquireGPUMaterialSet(scene.registry, scene.slot_model[slot], overrides);
	const u32 old = scene.slot_set[slot];
	releaseGPUMaterialSet(scene.registry, old);
	if (set == old) return;
	scene.slot_set[slot] = set;

	GPUSceneEntry& e = entryFor(scene, slot);
	e.mask |= GPUSceneEntry::MATERIAL_SET;
	e.instance.material_set = set == GPUSceneConst::INVALID_SET ? GPUSceneInstance::NO_MATERIAL_SET : scene.registry.sets[set].first;
}

bool isGPUSceneUploaded(const GPUScene& scene) {
	return scene.bounds && scene.instances && scene.entries.empty();
}

void uploadGPUScene(GPUScene& scene) {
	flushGPUSceneRegistry(scene.registry);
	if (!scene.shader->isReady()) return;

	const bool grown = scene.gpu_capacity != scene.capacity;
	if (scene.entries.empty() && !grown) return;
	PROFILE_FUNCTION();
	Renderer& renderer = scene.renderer;

	gpu::BufferHandle old_bounds = gpu::INVALID_BUFFER;
	gpu::BufferHandle old_instances = gpu::INVALID_BUFFER;
	const u32 old_capacity = scene.gpu_capacity;
	if (grown) {
		old_bounds = scene.bounds;
		old_instances = scene.instances;

		// free slots must have radius < 0, so the bounds are not left uninitialized
		Renderer::MemRef bounds_mem = renderer.allocate(scene.capacity * sizeof(GPUSceneBounds));
		memset(bounds_mem.data, 0, bounds_mem.size);
		GPUSceneBounds* b = (GPUSceneBounds*)bounds_mem.data;
		for (u32 i = 0; i < scene.capacity; ++i) b[i].radius = -1.f;
		scene.bounds = renderer.createBuffer(bounds_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene bounds");

		Renderer::MemRef instances_mem = renderer.allocate(scene.capacity * sizeof(GPUSceneInstance));
		memset(instances_mem.data, 0, instances_mem.size);
		scene.instances = renderer.createBuffer(instances_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene instances");
		scene.gpu_capacity = scene.capacity;
	}

	TransientSlice slice = {};
	const u32 count = scene.entries.size();
	if (count) {
		slice = alloc(renderer.getTransientPool(), count * sizeof(GPUSceneEntry));
		memcpy(slice.ptr, scene.entries.begin(), count * sizeof(GPUSceneEntry));
	}
	for (const GPUSceneEntry& e : scene.entries) scene.pending[e.slot] = GPUSceneConst::INVALID_SLOT;
	scene.entries.clear();

	const gpu::BufferHandle bounds = scene.bounds;
	const gpu::BufferHandle instances = scene.instances;
	const gpu::ProgramHandle program = scene.shader->getProgram(0);
	const u32 frame = renderer.frameNumber();
	renderer.pushJob("gpu scene", [=, &renderer](DrawStream& stream) {
		if (old_bounds) {
			stream.copy(bounds, old_bounds, 0, 0, old_capacity * sizeof(GPUSceneBounds));
			stream.copy(instances, old_instances, 0, 0, old_capacity * sizeof(GPUSceneInstance));
			stream.destroy(old_bounds);
			stream.destroy(old_instances);
		}
		if (count == 0) return;

		GPUSceneScatterUniforms ub;
		memset(&ub, 0, sizeof(ub));
		ub.entries = gpu::getBindlessHandle(slice.buffer);
		ub.entries_offset = slice.offset;
		ub.entry_count = count;
		ub.bounds = gpu::getRWBindlessHandle(bounds);
		ub.instances = gpu::getRWBindlessHandle(instances);
		ub.frame = frame;
		const TransientSlice ub_slice = alloc(renderer.getUniformPool(), &ub, sizeof(ub));

		stream.barrier(slice.buffer, gpu::BarrierType::READ);
		stream.barrier(bounds, gpu::BarrierType::WRITE);
		stream.barrier(instances, gpu::BarrierType::WRITE);
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub_slice.buffer, ub_slice.offset, sizeof(ub));
		stream.useProgram(program);
		const u32 groups = (count + 63) / 64;
		stream.dispatch(minimum(groups, 65535u), (groups + 65534) / 65535, 1);
		stream.memoryBarrier(bounds);
		stream.memoryBarrier(instances);
	});
}

} // namespace Lumix
