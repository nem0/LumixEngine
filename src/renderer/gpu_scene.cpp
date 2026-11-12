#include "gpu_scene.h"

#include "core/log.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "core/stack_array.h"
#include "engine/engine.h"
#include "engine/resource_manager.h"
#include "renderer/draw_stream.h"
#include "renderer/material.h"
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
	, m_slot_set(allocator)
	, m_sets(allocator)
	, m_set_entries(allocator)
	, m_free_sets(allocator)
	, m_set_map(allocator)
	, m_draw_groups(allocator)
	, m_draw_group_map(allocator)
	, m_variants(allocator)
	, m_variant_map(allocator)
	, m_pool_copies(allocator)
	, m_models(allocator)
	, m_mesh_table(allocator)
	, m_model_table(allocator)
	, m_groups(allocator)
	, m_free_groups(allocator)
	, m_group_table(allocator)
	, m_group_buffers(allocator)
	, m_moved_now(allocator)
{
	m_capacity = GPUSceneConst::INITIAL_CAPACITY;
	ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
	m_shader = rm.load<Shader>(Path("engine/shaders/gpu_scene.hlsl"));
	m_skinned_define_idx = (u32)renderer.getShaderDefineIdx("SKINNED");
}

GPUScene::~GPUScene() {
	if (m_bounds) m_renderer.getEndFrameDrawStream().destroy(m_bounds);
	if (m_instances) m_renderer.getEndFrameDrawStream().destroy(m_instances);
	if (m_mesh_buffer) m_renderer.getEndFrameDrawStream().destroy(m_mesh_buffer);
	if (m_model_buffer) m_renderer.getEndFrameDrawStream().destroy(m_model_buffer);
	if (m_group_buffer) m_renderer.getEndFrameDrawStream().destroy(m_group_buffer);
	if (m_set_buffer) m_renderer.getEndFrameDrawStream().destroy(m_set_buffer);
	if (m_variant_buffer) m_renderer.getEndFrameDrawStream().destroy(m_variant_buffer);
	for (u32 i = 0; i < (u32)m_models.size(); ++i) freePools(i);
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
		freePools(model_id);
		if (m_models[model_id].default_set != GPUSceneConst::INVALID_SLOT) releaseSet(m_models[model_id].default_set);
		m_models[model_id].default_set = GPUSceneConst::INVALID_SLOT;
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
		m_slot_set.resize(m_next_slot);
		m_slot_set[slot] = GPUSceneConst::INVALID_SLOT;
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

static u64 hashOverrides(const Model* model, Span<const GPUScene::Override> overrides) {
	u64 h = 1469598103934665603ull;
	auto mix = [&](u64 v) { h ^= v; h *= 1099511628211ull; };
	mix((u64)(uintptr)model);
	for (const GPUScene::Override& o : overrides) {
		mix(o.mesh);
		mix(o.material_index);
		mix((u64)(uintptr)o.material);
	}
	return h;
}

bool GPUScene::sameSet(const MaterialSet& set, u32 model_id, Span<const Override> overrides) const {
	if (!set.model || set.model_id != model_id) return false;
	u32 next = 0;
	for (u32 i = 0; i < set.count; ++i) {
		const SetEntry& e = m_set_entries[set.first + i];
		if (!e.own) continue;
		if (next >= overrides.length()) return false;
		const Override& o = overrides[next++];
		if (o.mesh != i || o.material != e.material || o.material_index != e.material_index) return false;
	}
	return next == overrides.length();
}

void GPUScene::releaseSet(u32 id) {
	MaterialSet& set = m_sets[id];
	ASSERT(set.refs > 0);
	if (--set.refs > 0) return;
	const auto found = m_set_map.find(set.hash);
	if (found.isValid() && found.value() == id) m_set_map.erase(set.hash);
	for (u32 i = 0; i < set.count; ++i) m_set_entries[set.first + i] = SetEntry{};
	set.model = nullptr;
	set.model_id = GPUSceneConst::INVALID_MODEL;
	m_free_sets.push(id);
}

// overrides: normalized by setOverrides (meshes < count, unique, sorted by mesh)
u32 GPUScene::acquireSet(u32 model_id, const Model* model, u32 count, Span<const Override> overrides) {
	u32 set = GPUSceneConst::INVALID_SLOT;
	const u64 hash = hashOverrides(model, overrides);
	const auto found = m_set_map.find(hash);
	if (found.isValid()) {
		if (sameSet(m_sets[found.value()], model_id, overrides)) {
			set = found.value();
		}
		else {
			// two different sets with the same hash
			for (u32 i = 0; i < (u32)m_sets.size() && set == GPUSceneConst::INVALID_SLOT; ++i) {
				if (sameSet(m_sets[i], model_id, overrides)) set = i;
			}
		}
	}
	if (set != GPUSceneConst::INVALID_SLOT) return set;

	// the range of a free set with as many entries, or a new one
	for (u32 i = 0; i < (u32)m_free_sets.size(); ++i) {
		if (m_sets[m_free_sets[i]].count != count) continue;
		set = m_free_sets[i];
		m_free_sets.swapAndPop(i);
		break;
	}
	if (set == GPUSceneConst::INVALID_SLOT) {
		set = m_sets.size();
		MaterialSet& added = m_sets.emplace();
		added.first = m_set_entries.size();
		added.count = count;
		m_set_entries.resize(added.first + count);
	}
	MaterialSet& s = m_sets[set];
	s.model = model;
	s.model_id = model_id;
	s.refs = 0;
	s.hash = hash;
	for (u32 i = 0; i < count; ++i) m_set_entries[s.first + i] = SetEntry{};
	for (const Override& o : overrides) {
		if (o.mesh >= count) continue;
		SetEntry& e = m_set_entries[s.first + o.mesh];
		e.material = o.material;
		e.material_index = o.material_index;
		e.own = true;
	}
	if (!found.isValid()) m_set_map.insert(hash, set);
	m_sets_dirty = true;
	return set;
}

void GPUScene::setOverrides(u32 slot, Span<const Override> overrides) {
	ASSERT(slot < m_next_slot);
	const u32 model_id = m_slot_model[slot];
	const Model* model = model_id == GPUSceneConst::INVALID_MODEL ? nullptr : m_models[model_id].model;
	const u32 count = model ? (u32)model->getMeshCount() : 0;
	// normalize the overrides (valid meshes, one per mesh - the last one wins, sorted by mesh), so the hash and sameSet see the same overrides for the same set
	StackArray<Override, 16> normalized(m_allocator);
	for (const Override& o : overrides) {
		if (o.mesh >= count) continue;
		bool replaced = false;
		for (Override& n : normalized) {
			if (n.mesh != o.mesh) continue;
			n = o;
			replaced = true;
			break;
		}
		if (!replaced) normalized.push(o);
	}
	sort(normalized.begin(), normalized.end(), [](const Override& a, const Override& b) { return a.mesh < b.mesh; });

	u32 set = GPUSceneConst::INVALID_SLOT;
	if (count > 0 && !normalized.empty()) set = acquireSet(model_id, model, count, Span<const Override>(normalized.begin(), normalized.size()));

	const u32 old = m_slot_set[slot];
	if (old == set) return;
	if (set != GPUSceneConst::INVALID_SLOT) ++m_sets[set].refs;
	if (old != GPUSceneConst::INVALID_SLOT) releaseSet(old);
	m_slot_set[slot] = set;
}

void GPUScene::free(u32 slot) {
	ASSERT(slot < m_next_slot);
	setOverrides(slot, Span<const Override>());
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

void GPUScene::update(u32 slot, const Transform& tr, float radius, i32 forced_lod) {
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
	if (forced_lod >= 0) i.flags = GPUSceneInstance::FORCED_LOD | (u32(minimum(forced_lod, 4)) << 1);
	i.material_set = m_slot_set[slot] == GPUSceneConst::INVALID_SLOT ? GPUSceneInstance::NO_MATERIAL_SET : m_sets[m_slot_set[slot]].first;
}

static u64 hashDrawGroup(const GPUScene::DrawGroup& k) {
	u64 h = 1469598103934665603ull;
	auto mix = [&](u64 v) { h ^= v; h *= 1099511628211ull; };
	mix((u64)(uintptr)k.shader);
	mix(k.define_mask);
	mix((u64)k.state);
	mix((u64)k.primitive_type);
	mix(k.layer);
	mix(k.plain);
	mix((u64)k.index_type);
	for (const char* c = k.semantics_defines; c && *c; ++c) mix((u8)*c);
	return h;
}

static bool isSameDrawGroup(const GPUScene::DrawGroup& a, const GPUScene::DrawGroup& b) {
	return a.shader == b.shader && a.define_mask == b.define_mask && a.state == b.state && a.primitive_type == b.primitive_type && a.layer == b.layer && a.plain == b.plain
		&& a.index_type == b.index_type && equalStrings(a.semantics_defines, b.semantics_defines);
}

u32 GPUScene::findOrAddDrawGroup(const DrawGroup& key) {
	const u64 hash = hashDrawGroup(key);
	const auto found = m_draw_group_map.find(hash);
	if (found.isValid()) {
		if (isSameDrawGroup(m_draw_groups[found.value()], key)) return found.value();
		// two different groups with the same hash
		for (u32 i = 0; i < (u32)m_draw_groups.size(); ++i) {
			if (isSameDrawGroup(m_draw_groups[i], key)) return i;
		}
	}
	if (m_draw_groups.size() >= (i32)GPUSceneConst::MAX_DRAW_GROUPS) {
		static bool reported = false;
		if (!reported) logError("GPU scene: more than ", GPUSceneConst::MAX_DRAW_GROUPS, " different programs of meshes, the meshes of the other ones are not drawn");
		reported = true;
		return GPUSceneConst::INVALID_VARIANT;
	}
	const u32 index = m_draw_groups.size();
	m_draw_groups.push(key);
	m_draw_groups.back().variant_count = 0;
	if (!found.isValid()) m_draw_group_map.insert(hash, index);
	return index;
}

// The variant (what one command of an indirect draw draws) of a mesh of a model with a material, it is made if it does not exist.
u32 GPUScene::variantFor(u32 model_id, const Model& model, u32 mesh_idx, const Material* material) {
	if (!material || !material->isReady() || !material->getShader()) return GPUSceneConst::INVALID_VARIANT;
	if (mesh_idx >= (u32)model.getMeshCount()) return GPUSceneConst::INVALID_VARIANT;
	const Mesh& mesh = model.getMesh(mesh_idx);
	const bool plain = !model.hasClusters();
	const MeshPool* pool = nullptr;
	if (plain) {
		const ModelEntry& entry = m_models[model_id];
		if (!entry.pools || mesh_idx >= entry.pool_count) return GPUSceneConst::INVALID_VARIANT;
		pool = &entry.pools[mesh_idx];
	}
	else if ((i32)mesh_idx > model.getClusterMeshesEnd()) {
		return GPUSceneConst::INVALID_VARIANT;
	}

	DrawGroup key;
	key.shader = material->getShader();
	key.define_mask = material->getDefineMask();
	if (plain && mesh.type == Mesh::SKINNED) key.define_mask |= 1 << m_skinned_define_idx;
	key.state = material->m_render_states;
	key.primitive_type = mesh.primitive_type;
	key.semantics_defines = mesh.semantics_defines;
	key.layer = (u8)material->getLayer();
	key.plain = plain;
	key.index_type = plain ? mesh.index_type : gpu::DataType::U16;
	key.variant_count = 0;
	const u32 group = findOrAddDrawGroup(key);
	if (group == GPUSceneConst::INVALID_VARIANT) return GPUSceneConst::INVALID_VARIANT;

	const Mesh* mesh_key = plain ? &mesh : nullptr;
	u64 hash = 1469598103934665603ull;
	hash = (hash ^ group) * 1099511628211ull;
	hash = (hash ^ (u64)(uintptr)mesh_key) * 1099511628211ull;
	const u32 index_count = plain ? (u32)mesh.indices_count : (u32)mesh.cluster_index_stride;
	const auto found = m_variant_map.find(hash);
	if (found.isValid()) {
		Variant& v = m_variants[found.value()];
		if (v.group == group && v.mesh == mesh_key) {
			// meshes with clusters share a variant, its draw is as long as the longest of them
			if (index_count > v.index_count) {
				v.index_count = index_count;
				m_variants_dirty = true;
			}
			return found.value();
		}
		// two different variants with the same hash
		for (u32 i = 0; i < (u32)m_variants.size(); ++i) {
			if (m_variants[i].group == group && m_variants[i].mesh == mesh_key) return i;
		}
	}
	if (m_variants.size() >= (i32)GPUSceneConst::MAX_VARIANTS) {
		static bool reported = false;
		if (!reported) logError("GPU scene: more than ", GPUSceneConst::MAX_VARIANTS, " different meshes with materials, the other ones are not drawn");
		reported = true;
		return GPUSceneConst::INVALID_VARIANT;
	}
	Variant v;
	v.group = group;
	v.index_in_group = m_draw_groups[group].variant_count++;
	v.mesh = mesh_key;
	v.first_index = plain ? pool->index_offset / (mesh.areIndices16() ? 2 : 4) : 0;
	v.base_vertex = plain ? pool->vertex_offset / mesh.vb_stride : 0;
	v.index_count = index_count;
	const u32 index = m_variants.size();
	m_variants.push(v);
	if (!found.isValid()) m_variant_map.insert(hash, index);
	m_variants_dirty = true;
	return index;
}

bool GPUScene::allocPools(u32 model_id, const Model& model) {
	ModelEntry& entry = m_models[model_id];
	if (entry.pools) return true;
	const u32 count = (u32)model.getMeshCount();
	if (count == 0) return false;
	MeshPool* pools = (MeshPool*)m_allocator.allocate(sizeof(MeshPool) * count, alignof(MeshPool));
	u32 done = 0;
	bool ok = true;
	for (u32 i = 0; i < count && ok; ++i) {
		const Mesh& mesh = model.getMesh(i);
		MeshPool& p = pools[i];
		memset(&p, 0, sizeof(p));
		p.vertex_size = mesh.vertices.size() * mesh.vb_stride;
		p.index_size = (u32)mesh.indices.size();
		// vertices are addressed by the base vertex of a command, so they start at a multiple of their size
		if (!m_renderer.allocClusterRange(p.vertex_size, mesh.vb_stride, p.vertex_offset)) {
			ok = false;
		}
		else if (!m_renderer.allocClusterRange(p.index_size, 4, p.index_offset)) {
			m_renderer.freeClusterData(p.vertex_offset, p.vertex_size);
			ok = false;
		}
		else {
			++done;
		}
	}
	if (!ok) {
		for (u32 i = 0; i < done; ++i) {
			m_renderer.freeClusterData(pools[i].vertex_offset, pools[i].vertex_size);
			m_renderer.freeClusterData(pools[i].index_offset, pools[i].index_size);
		}
		m_allocator.deallocate(pools);
		logError("GPU scene: no space for the meshes of ", model.getPath(), ", it is not drawn");
		return false;
	}
	entry.pools = pools;
	entry.pool_count = count;
	for (u32 i = 0; i < count; ++i) {
		const Mesh& mesh = model.getMesh(i);
		PoolCopy vertex_copy = { mesh.vertex_buffer_handle, pools[i].vertex_offset, pools[i].vertex_size };
		PoolCopy index_copy = { mesh.index_buffer_handle, pools[i].index_offset, pools[i].index_size };
		m_pool_copies.push(vertex_copy);
		m_pool_copies.push(index_copy);
	}
	return true;
}

void GPUScene::freePools(u32 model_id) {
	ModelEntry& entry = m_models[model_id];
	if (!entry.pools) return;
	for (u32 i = 0; i < entry.pool_count; ++i) {
		m_renderer.freeClusterData(entry.pools[i].vertex_offset, entry.pools[i].vertex_size);
		m_renderer.freeClusterData(entry.pools[i].index_offset, entry.pools[i].index_size);
	}
	m_allocator.deallocate(entry.pools);
	entry.pools = nullptr;
	entry.pool_count = 0;
}

void GPUScene::rebuildModelTables() {
	++m_mesh_table_version;
	// the draw groups and variants are made again for the new tables, so is what refers to them
	m_draw_groups.clear();
	m_draw_group_map.clear();
	m_variants.clear();
	m_variant_map.clear();
	m_variants_dirty = true;
	m_sets_dirty = true;
	m_mesh_table.clear();
	m_max_tree_depth = 0;
	m_model_table.resize(m_models.size());
	for (u32 id = 0, c = m_models.size(); id < c; ++id) {
		GPUSceneModel& entry = m_model_table[id];
		memset(&entry, 0, sizeof(entry));
		entry.first_mesh = m_mesh_table.size();
		const Model* model = m_models[id].model;
		ModelEntry& model_entry = m_models[id];
		// the default set has the entry of every mesh of the model, it is made again if the model has another number of meshes
		if (model_entry.default_set != GPUSceneConst::INVALID_SLOT && (!model || (u32)model->getMeshCount() != m_sets[model_entry.default_set].count)) {
			releaseSet(model_entry.default_set);
			model_entry.default_set = GPUSceneConst::INVALID_SLOT;
		}
		if (!model || !model->isReady()) continue;
		const u32 mesh_count = (u32)model->getMeshCount();
		if (mesh_count == 0) continue;
		if (model_entry.default_set == GPUSceneConst::INVALID_SLOT) {
			model_entry.default_set = acquireSet(id, model, mesh_count, Span<const Override>());
			++m_sets[model_entry.default_set].refs;
		}
		const u32 default_first = m_sets[model_entry.default_set].first;
		if (!model->hasClusters()) {
			if (!model->canUseGPUScene() || !allocPools(id, *model)) continue;
			// all the meshes of all the LODs, drawn one by one
			entry.flags = GPUSceneModel::PLAIN;
			const LODMeshIndices* lods = model->getLODIndices();
			auto pack = [&](u32 l) { return lods[l].to >= lods[l].from && lods[l].from >= 0 ? u32(lods[l].from) | (u32(lods[l].to + 1) << 16) : 0u; };
			for (u32 l = 0; l < 4; ++l) {
				entry.lod[l] = pack(l);
				entry.lod_distance[l] = model->getLODDistances()[l];
			}
			entry.lod4 = pack(4);
			for (i32 i = 0, count = model->getMeshCount(); i < count; ++i) {
				GPUSceneMesh& m = m_mesh_table.emplace();
				memset(&m, 0, sizeof(m));
				m.index_in_model = (u32)i;
				m.material_entry = default_first + (u32)i;
				++entry.mesh_count;
			}
			continue;
		}

		// a model that is not complete has no meshes in the table rather than some
		const i32 end = model->getClusterMeshesEnd();
		bool complete = true;
		for (i32 i = 0; i <= end; ++i) {
			const Mesh& mesh = model->getMesh(i);
			complete = complete && mesh.cluster_table_size != 0 && mesh.cluster_count != 0 && mesh.cluster_group_size != 0;
		}
		if (!complete) continue;

		for (i32 i = 0; i <= end; ++i) {
			const Mesh& mesh = model->getMesh(i);
			GPUSceneMesh& m = m_mesh_table.emplace();
			memset(&m, 0, sizeof(m));
			m.cluster_groups = mesh.cluster_group_ids_offset;
			m.nodes = mesh.node_table_offset;
			m.cluster_count = mesh.cluster_count;
			m.node_count = mesh.node_count;
			m.root_count = mesh.root_count;
			m.roots_offset = mesh.node_count * 64;	// sizeof(MeshNodeGPU)
			m.tree_depth = mesh.tree_depth;
			m.index_stride = mesh.cluster_index_stride;
			m.leaf_cluster_count = mesh.leaf_cluster_count;
			m.flat_groups = (mesh.cluster_count + GPUSceneConst::CLUSTER_THREADS - 1) / GPUSceneConst::CLUSTER_THREADS;
			m.indices = mesh.cluster_group_offset;
			m.index_in_model = (u32)i;
			m.material_entry = default_first + (u32)i;
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

	// a material that was unloaded (reloaded) can have a different program now
	const u32 reload_counter = m_renderer.getMaterialReloadCounter();
	if (reload_counter != m_registry_reload_counter) {
		m_registry_reload_counter = reload_counter;
		m_tables_dirty = true;
	}

	const bool grown = m_gpu_capacity != m_capacity;
	if (m_entries.empty() && !grown && !m_tables_dirty && !m_groups_dirty && !m_sets_dirty && !m_variants_dirty && m_pool_copies.empty()) return;
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

	// the material sets: the variants of their meshes (the model of a set has its pools and the mesh table was made), the materials of the meshes without an override
	// are the ones of the model
	TransientSlice set_slice = {};
	u32 set_bytes = 0;
	if (m_sets_dirty) {
		m_sets_dirty = false;
		for (const MaterialSet& set : m_sets) {
			if (!set.model) continue;
			const Model* model = m_models[set.model_id].model;
			const bool ready = model && model->isReady();
			for (u32 i = 0; i < set.count; ++i) {
				SetEntry& e = m_set_entries[set.first + i];
				e.variant = GPUSceneConst::INVALID_VARIANT;
				if (!ready || i >= (u32)model->getMeshCount()) continue;
				if (!e.own) {
					const MeshMaterial& mesh_mat = model->getMeshMaterial(i);
					e.material = mesh_mat.material;
					e.material_index = (u32)mesh_mat.material_index;
				}
				e.variant = variantFor(set.model_id, *model, i, e.material);
			}
		}
		ensureTableBuffer(m_set_buffer, m_set_capacity, maximum(m_set_entries.size(), 1u), 8, "gpu scene material sets");
		set_bytes = m_set_entries.size() * 8;
		if (set_bytes) {
			set_slice = Lumix::alloc(m_renderer.getTransientPool(), set_bytes);
			u32* out = (u32*)set_slice.ptr;
			for (const SetEntry& e : m_set_entries) {
				out[0] = e.variant;
				out[1] = e.material_index;
				out += 2;
			}
		}
	}
	const gpu::BufferHandle set_buffer = m_set_buffer;

	// the table of variants, after everything that makes variants
	TransientSlice variant_slice = {};
	u32 variant_bytes = 0;
	if (m_variants_dirty) {
		m_variants_dirty = false;
		ensureTableBuffer(m_variant_buffer, m_variant_capacity, maximum(m_variants.size(), 1u), 16, "gpu scene variants");
		variant_bytes = m_variants.size() * 16;
		if (variant_bytes) {
			variant_slice = Lumix::alloc(m_renderer.getTransientPool(), variant_bytes);
			u32* out = (u32*)variant_slice.ptr;
			for (const Variant& v : m_variants) {
				out[0] = v.group | (v.index_in_group << 16);
				out[1] = v.first_index;
				out[2] = v.base_vertex;
				out[3] = v.index_count;
				out += 4;
			}
		}
	}
	const gpu::BufferHandle variant_buffer = m_variant_buffer;

	// the copies of the vertices and indices of the meshes to the ranges of the buffer of cluster data
	TransientSlice pool_copy_slice = {};
	const u32 pool_copy_count = m_pool_copies.size();
	if (pool_copy_count) {
		pool_copy_slice = Lumix::alloc(m_renderer.getTransientPool(), pool_copy_count * sizeof(PoolCopy));
		memcpy(pool_copy_slice.ptr, m_pool_copies.begin(), pool_copy_count * sizeof(PoolCopy));
		m_pool_copies.clear();
	}
	const gpu::BufferHandle pool_buffer = m_renderer.getClusterDataBuffer();
	const PoolCopy* pool_copies = (const PoolCopy*)pool_copy_slice.ptr;

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
		if (set_bytes) {
			stream.barrier(set_slice.buffer, gpu::BarrierType::READ);
			stream.copy(set_buffer, set_slice.buffer, 0, set_slice.offset, set_bytes);
		}
		if (variant_bytes) {
			stream.barrier(variant_slice.buffer, gpu::BarrierType::READ);
			stream.copy(variant_buffer, variant_slice.buffer, 0, variant_slice.offset, variant_bytes);
		}
		for (u32 i = 0; i < pool_copy_count; ++i) {
			stream.copy(pool_buffer, pool_copies[i].src, pool_copies[i].dst_offset, 0, pool_copies[i].size);
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
