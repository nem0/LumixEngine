#include "gpu_scene.h"

#include "core/log.h"
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

namespace {

struct ScatterUniforms {
	gpu::BindlessHandle entries;
	u32 entries_offset;
	u32 entry_count;
	gpu::RWBindlessHandle bounds;
	gpu::RWBindlessHandle instances;
	u32 padding[3];
};
static_assert(sizeof(ScatterUniforms) == 32);

} // anonymous namespace

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
	: m_renderer(renderer)
	, m_allocator(allocator)
	, m_free_slots(allocator)
	, m_entries(allocator)
	, m_pending(allocator)
	, m_fresh(allocator)
	, m_slot_model(allocator)
	, m_models(allocator)
	, m_mesh_table(allocator)
	, m_model_table(allocator)
	, m_mesh_models(allocator)
	, m_mesh_indices(allocator)
	, m_groups(allocator)
	, m_free_groups(allocator)
	, m_group_table(allocator)
	, m_group_buffers(allocator)
	, m_moved_now(allocator)
{
	m_capacity = GPUSceneConst::INITIAL_CAPACITY;
	ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
	m_shader = rm.load<Shader>(Path("engine/shaders/gpu_scene.hlsl"));
}

GPUScene::~GPUScene() {
	if (m_bounds) m_renderer.getEndFrameDrawStream().destroy(m_bounds);
	if (m_instances) m_renderer.getEndFrameDrawStream().destroy(m_instances);
	if (m_mesh_buffer) m_renderer.getEndFrameDrawStream().destroy(m_mesh_buffer);
	if (m_model_buffer) m_renderer.getEndFrameDrawStream().destroy(m_model_buffer);
	if (m_group_buffer) m_renderer.getEndFrameDrawStream().destroy(m_group_buffer);
	m_shader->decRefCount();
}

u32 GPUScene::getModelId(const Model* model) const {
	for (u32 i = 0, c = m_models.size(); i < c; ++i) {
		if (m_models[i].model == model) return i;
	}
	return GPUSceneConst::INVALID_MODEL;
}

u32 GPUScene::acquireModel(const Model* model) {
	u32 model_id = getModelId(model);
	if (model_id == GPUSceneConst::INVALID_MODEL) {
		for (u32 i = 0, c = m_models.size(); i < c; ++i) {
			if (!m_models[i].model) {
				model_id = i;
				break;
			}
		}
		if (model_id == GPUSceneConst::INVALID_MODEL && m_models.size() < GPUSceneConst::MAX_MODELS) {
			model_id = m_models.size();
			m_models.emplace();
		}
		if (model_id == GPUSceneConst::INVALID_MODEL) {
			static bool reported = false;
			if (!reported) logError("GPU scene: more than ", GPUSceneConst::MAX_MODELS, " models, the instances of the other ones are not in the table of models");
			reported = true;
		}
		else {
			m_models[model_id].model = model;
			m_tables_dirty = true;
		}
	}
	if (model_id != GPUSceneConst::INVALID_MODEL) ++m_models[model_id].instance_count;
	return model_id;
}

void GPUScene::releaseModel(u32 model_id) {
	if (model_id == GPUSceneConst::INVALID_MODEL) return;
	ASSERT(m_models[model_id].instance_count > 0);
	if (--m_models[model_id].instance_count == 0) {
		m_models[model_id].model = nullptr;
		m_tables_dirty = true;
	}
}

u32 GPUScene::alloc(const Model* model) {
	const u32 model_id = acquireModel(model);

	u32 slot;
	if (!m_free_slots.empty()) {
		slot = m_free_slots.back();
		m_free_slots.pop();
	}
	else {
		slot = m_next_slot++;
		while (m_next_slot > m_capacity) m_capacity *= 2;
		m_pending.resize(m_next_slot);
		m_fresh.resize(m_next_slot);
		m_slot_model.resize(m_next_slot);
		m_pending[slot] = GPUSceneConst::INVALID_SLOT;
	}
	m_slot_model[slot] = model_id;
	m_fresh[slot] = 1;
	return slot;
}

GPUScene::Entry& GPUScene::entryFor(u32 slot) {
	ASSERT(slot < m_next_slot);
	if (m_pending[slot] != GPUSceneConst::INVALID_SLOT) return m_entries[m_pending[slot]];
	m_pending[slot] = m_entries.size();
	Entry& e = m_entries.emplace();
	memset(&e, 0, sizeof(e));
	e.slot = slot;
	return e;
}

void GPUScene::free(u32 slot) {
	ASSERT(slot < m_next_slot);
	Entry& e = entryFor(slot);
	memset(&e.bounds, 0, sizeof(e.bounds));
	memset(&e.instance, 0, sizeof(e.instance));
	e.bounds.radius = -1.f;
	e.flags = Entry::HAS_NEW | Entry::TELEPORT;
	m_fresh[slot] = 1;
	m_free_slots.push(slot);
	releaseModel(m_slot_model[slot]);
	m_slot_model[slot] = GPUSceneConst::INVALID_MODEL;
}

u32 GPUScene::allocGroup(const Model* model) {
	u32 id;
	if (!m_free_groups.empty()) {
		id = m_free_groups.back();
		m_free_groups.pop();
	}
	else {
		id = m_groups.size();
		m_groups.emplace();
	}
	Group& g = m_groups[id];
	g = Group();
	g.used = true;
	g.model = model;
	g.model_id = acquireModel(model);
	m_groups_dirty = true;
	return id;
}

void GPUScene::updateGroup(u32 id, const DVec3& origin, gpu::BufferHandle instances, u32 instance_count) {
	Group& g = m_groups[id];
	ASSERT(g.used);
	const i32 p[3] = { toFixedPosition(origin.x), toFixedPosition(origin.y), toFixedPosition(origin.z) };
	if (memcmp(g.p, p, sizeof(p)) == 0 && g.data == instances && g.count == instance_count) return;
	memcpy(g.p, p, sizeof(p));
	g.data = instances;
	g.count = instance_count;
	m_groups_dirty = true;
}

void GPUScene::freeGroup(u32 id) {
	Group& g = m_groups[id];
	ASSERT(g.used);
	releaseModel(g.model_id);
	g = Group();
	m_free_groups.push(id);
	m_groups_dirty = true;
}

void GPUScene::rebuildGroupTable() {
	m_group_table.clear();
	m_group_buffers.clear();
	m_group_instances = 0;
	for (const Group& g : m_groups) {
		if (!g.used || g.count == 0 || !g.data || g.model_id == GPUSceneConst::INVALID_MODEL) continue;
		GPUSceneGroup& e = m_group_table.emplace();
		memcpy(e.p, g.p, sizeof(e.p));
		e.instance_count = g.count;
		e.instances = gpu::getBindlessHandle(g.data).value;
		e.model = g.model_id;
		e.first_instance = m_group_instances;
		e.radius = g.model->getOriginBoundingRadius();
		m_group_instances += g.count;
		m_group_buffers.push(g.data);
	}
}

void GPUScene::ensureTableBuffer(gpu::BufferHandle& buffer, u32& capacity, u32 needed, u32 entry_size, const char* name) {
	if (buffer && needed <= capacity) return;
	if (buffer) m_renderer.getEndFrameDrawStream().destroy(buffer);
	capacity = maximum(capacity ? capacity : 64u, 64u);
	while (capacity < needed) capacity *= 2;
	buffer = m_renderer.createBuffer(Renderer::MemRef{ capacity * entry_size, nullptr, false }, gpu::BufferFlags::SHADER_BUFFER, name);
}

void GPUScene::update(u32 slot, const Transform& tr, float radius) {
	Entry& e = entryFor(slot);
	// a slot that was freed and allocated again has no previous state either, it is fresh
	e.flags = Entry::HAS_NEW | (m_fresh[slot] ? Entry::TELEPORT : 0);
	m_fresh[slot] = 0;

	e.bounds.p[0] = toFixedPosition(tr.pos.x);
	e.bounds.p[1] = toFixedPosition(tr.pos.y);
	e.bounds.p[2] = toFixedPosition(tr.pos.z);
	e.bounds.radius = radius;

	GPUSceneInstance& i = e.instance;
	memset(&i, 0, sizeof(i));
	i.rot = tr.rot;
	i.p[0] = e.bounds.p[0];
	i.p[1] = e.bounds.p[1];
	i.p[2] = e.bounds.p[2];
	i.mesh_id = m_slot_model[slot];
	i.scale = tr.scale;
}

void GPUScene::rebuildModelTables() {
	m_mesh_table.clear();
	m_mesh_models.clear();
	m_mesh_indices.clear();
	m_max_tree_depth = 0;
	m_model_table.resize(m_models.size());
	for (u32 id = 0, c = m_models.size(); id < c; ++id) {
		GPUSceneModel& entry = m_model_table[id];
		entry.first_mesh = m_mesh_table.size();
		entry.mesh_count = 0;
		const Model* model = m_models[id].model;
		if (!model || !model->isReady() || !model->hasClusters()) continue;

		const i32 end = model->getClusterMeshesEnd();
		for (i32 i = 0; i <= end; ++i) {
			const Mesh& mesh = model->getMesh(i);
			if (!mesh.cluster_buffer || !mesh.node_buffer || mesh.cluster_count == 0) {
				// the model is not complete, it has no meshes in the table rather than some
				m_mesh_table.resize(entry.first_mesh);
				m_mesh_models.resize(entry.first_mesh);
				m_mesh_indices.resize(entry.first_mesh);
				entry.mesh_count = 0;
				break;
			}
			GPUSceneMesh& m = m_mesh_table.emplace();
			memset(&m, 0, sizeof(m));
			m.clusters = gpu::getBindlessHandle(mesh.cluster_buffer).value;
			m.nodes = gpu::getBindlessHandle(mesh.node_buffer).value;
			m.cluster_count = mesh.cluster_count;
			m.node_count = mesh.node_count;
			m.root_count = mesh.root_count;
			m.roots_offset = mesh.node_count * 64;	// sizeof(MeshNodeGPU)
			m.tree_depth = mesh.tree_depth;
			m.index_stride = mesh.cluster_index_stride;
			m.leaf_cluster_count = mesh.leaf_cluster_count;
			m.flat_groups = (mesh.cluster_count + GPUSceneConst::CLUSTER_THREADS - 1) / GPUSceneConst::CLUSTER_THREADS;
			m.vertices = gpu::getBindlessHandle(mesh.vertex_buffer_handle).value;
			m.indices = gpu::getBindlessHandle(mesh.cluster_index_buffer).value;
			m_mesh_models.push(model);
			m_mesh_indices.push((u32)i);
			if (mesh.cluster_count <= GPUSceneConst::FLAT_MAX_CLUSTERS) m.flags |= GPUSceneMesh::FLAT;
			m_max_tree_depth = maximum(m_max_tree_depth, mesh.tree_depth);
			++entry.mesh_count;
		}
	}
}

void GPUScene::flush() {
	if (!m_shader->isReady()) return;

	const u32 frame = m_renderer.frameNumber();
	if (frame != m_frame) {
		// what was written in the previous frame has to stop moving: its previous state becomes the current state
		for (u32 slot : m_moved_now) {
			if (m_pending[slot] != GPUSceneConst::INVALID_SLOT) continue;	// written again, its entry says what happens
			Entry& e = entryFor(slot);
			e.flags = 0;
		}
		m_moved_now.clear();
		m_frame = frame;
	}

	const bool grown = m_gpu_capacity != m_capacity;
	if (m_entries.empty() && !grown && !m_tables_dirty && !m_groups_dirty) return;
	PROFILE_FUNCTION();

	// the tables of the static data of models
	TransientSlice mesh_slice = {}, model_slice = {};
	u32 mesh_bytes = 0, model_bytes = 0;
	if (m_tables_dirty) {
		m_tables_dirty = false;
		rebuildModelTables();
		auto ensure = [&](gpu::BufferHandle& buffer, u32& capacity, u32 needed, u32 entry_size, const char* name) {
			if (buffer && needed <= capacity) return;
			if (buffer) m_renderer.getEndFrameDrawStream().destroy(buffer);
			capacity = maximum(capacity ? capacity : 64u, 64u);
			while (capacity < needed) capacity *= 2;
			buffer = m_renderer.createBuffer(Renderer::MemRef{ capacity * entry_size, nullptr, false }, gpu::BufferFlags::SHADER_BUFFER, name);
		};
		ensure(m_mesh_buffer, m_mesh_capacity, maximum(m_mesh_table.size(), 1u), sizeof(GPUSceneMesh), "gpu scene meshes");
		ensure(m_model_buffer, m_model_capacity, maximum(m_model_table.size(), 1u), sizeof(GPUSceneModel), "gpu scene models");
		mesh_bytes = m_mesh_table.size() * sizeof(GPUSceneMesh);
		model_bytes = m_model_table.size() * sizeof(GPUSceneModel);
		if (mesh_bytes) {
			mesh_slice = Lumix::alloc(m_renderer.getTransientPool(), mesh_bytes);
			memcpy(mesh_slice.ptr, m_mesh_table.begin(), mesh_bytes);
		}
		if (model_bytes) {
			model_slice = Lumix::alloc(m_renderer.getTransientPool(), model_bytes);
			memcpy(model_slice.ptr, m_model_table.begin(), model_bytes);
		}
	}
	const gpu::BufferHandle mesh_buffer = m_mesh_buffer;
	const gpu::BufferHandle model_buffer = m_model_buffer;

	// the table of the groups of instanced models
	TransientSlice group_slice = {};
	u32 group_bytes = 0;
	if (m_groups_dirty) {
		m_groups_dirty = false;
		rebuildGroupTable();
		ensureTableBuffer(m_group_buffer, m_group_capacity, maximum(m_group_table.size(), 1u), sizeof(GPUSceneGroup), "gpu scene groups");
		group_bytes = m_group_table.size() * sizeof(GPUSceneGroup);
		if (group_bytes) {
			group_slice = Lumix::alloc(m_renderer.getTransientPool(), group_bytes);
			memcpy(group_slice.ptr, m_group_table.begin(), group_bytes);
		}
	}
	const gpu::BufferHandle group_buffer = m_group_buffer;

	for (const Entry& e : m_entries) {
		if ((e.flags & Entry::HAS_NEW) && !(e.flags & Entry::TELEPORT)) m_moved_now.push(e.slot);
	}

	gpu::BufferHandle old_bounds = gpu::INVALID_BUFFER;
	gpu::BufferHandle old_instances = gpu::INVALID_BUFFER;
	const u32 old_capacity = m_gpu_capacity;
	if (grown) {
		old_bounds = m_bounds;
		old_instances = m_instances;

		// free slots must have radius < 0, so the bounds are not left uninitialized
		Renderer::MemRef bounds_mem = m_renderer.allocate(m_capacity * sizeof(GPUSceneBounds));
		memset(bounds_mem.data, 0, bounds_mem.size);
		GPUSceneBounds* b = (GPUSceneBounds*)bounds_mem.data;
		for (u32 i = 0; i < m_capacity; ++i) b[i].radius = -1.f;
		m_bounds = m_renderer.createBuffer(bounds_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene bounds");

		Renderer::MemRef instances_mem = m_renderer.allocate(m_capacity * sizeof(GPUSceneInstance));
		memset(instances_mem.data, 0, instances_mem.size);
		m_instances = m_renderer.createBuffer(instances_mem, gpu::BufferFlags::SHADER_BUFFER, "gpu scene instances");
		m_gpu_capacity = m_capacity;
	}

	TransientSlice slice = {};
	const u32 count = m_entries.size();
	if (count) {
		slice = Lumix::alloc(m_renderer.getTransientPool(), count * sizeof(Entry));
		memcpy(slice.ptr, m_entries.begin(), count * sizeof(Entry));
	}
	for (const Entry& e : m_entries) m_pending[e.slot] = GPUSceneConst::INVALID_SLOT;
	m_entries.clear();

	const gpu::BufferHandle bounds = m_bounds;
	const gpu::BufferHandle instances = m_instances;
	const gpu::ProgramHandle program = m_shader->getProgram(0);
	Renderer& renderer = m_renderer;
	m_renderer.pushJob("gpu scene", [=, &renderer](DrawStream& stream) {
		if (old_bounds) {
			stream.copy(bounds, old_bounds, 0, 0, old_capacity * sizeof(GPUSceneBounds));
			stream.copy(instances, old_instances, 0, 0, old_capacity * sizeof(GPUSceneInstance));
			stream.destroy(old_bounds);
			stream.destroy(old_instances);
		}
		if (mesh_bytes) {
			stream.barrier(mesh_slice.buffer, gpu::BarrierType::READ);
			stream.copy(mesh_buffer, mesh_slice.buffer, 0, mesh_slice.offset, mesh_bytes);
		}
		if (model_bytes) {
			stream.barrier(model_slice.buffer, gpu::BarrierType::READ);
			stream.copy(model_buffer, model_slice.buffer, 0, model_slice.offset, model_bytes);
		}
		if (group_bytes) {
			stream.barrier(group_slice.buffer, gpu::BarrierType::READ);
			stream.copy(group_buffer, group_slice.buffer, 0, group_slice.offset, group_bytes);
		}
		if (count == 0) return;

		ScatterUniforms ub;
		memset(&ub, 0, sizeof(ub));
		ub.entries = gpu::getBindlessHandle(slice.buffer);
		ub.entries_offset = slice.offset;
		ub.entry_count = count;
		ub.bounds = gpu::getRWBindlessHandle(bounds);
		ub.instances = gpu::getRWBindlessHandle(instances);
		const TransientSlice ub_slice = Lumix::alloc(renderer.getUniformPool(), &ub, sizeof(ub));

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
