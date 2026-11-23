#include "gpu_scene_registry.h"

#include "core/log.h"
#include "core/profiler.h"
#include "core/string.h"
#include "renderer/draw_stream.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"

namespace Lumix {

static u64 hashBegin() { return 1469598103934665603ull; }
static u64 hashMix(u64 hash, u64 value) { return (hash ^ value) * 1099511628211ull; }

GPUSceneRegistry::GPUSceneRegistry(Renderer& renderer, IAllocator& allocator)
	: renderer(renderer)
	, allocator(allocator)
	, queue(allocator)
	, clusters(allocator)
	, lod(allocator)
	, sets(allocator)
	, free_sets(allocator)
	, set_map(allocator)
	, set_ranges(allocator)
	, set_entries(allocator)
	, set_table(allocator, "gpu scene material sets")
	, draw_groups(allocator)
	, free_groups(allocator)
	, draw_group_map(allocator)
	, variants(allocator)
	, variant_map(allocator)
	, variant_table(allocator, "gpu scene variants")
{
	skinned_define_idx = renderer.getShaderDefineIdx("SKINNED");
	reload_counter = renderer.getMaterialReloadCounter();
}

void destroyGPUSceneRegistry(GPUSceneRegistry& res) {
	destroyClusterModels(res);
	destroyLodModels(res);
	mirrorDestroy(res.set_table, res.renderer);
	mirrorDestroy(res.variant_table, res.renderer);
}

const Model* getGPUModel(const GPUSceneRegistry& res, u32 model_id) {
	if (model_id == GPUSceneConst::INVALID_MODEL) return nullptr;
	if (isLodModelId(model_id)) return res.lod.entries[modelIndex(model_id)].model;
	return res.clusters.entries[model_id].model;
}

// draw groups and variants

static u64 hashDrawGroup(const GPUDrawGroup& key) {
	u64 hash = hashBegin();
	hash = hashMix(hash, (u64)(uintptr)key.shader);
	hash = hashMix(hash, key.define_mask);
	hash = hashMix(hash, (u64)key.state);
	hash = hashMix(hash, (u64)key.primitive_type);
	hash = hashMix(hash, key.layer);
	hash = hashMix(hash, key.lod);
	hash = hashMix(hash, (u64)key.index_type);
	for (const char* c = key.semantics_defines; c && *c; ++c) hash = hashMix(hash, (u8)*c);
	return hash;
}

static bool sameDrawGroup(const GPUDrawGroup& a, const GPUDrawGroup& b) {
	return a.shader == b.shader && a.define_mask == b.define_mask && a.state == b.state && a.primitive_type == b.primitive_type && a.layer == b.layer
		&& a.lod == b.lod && a.index_type == b.index_type && equalStrings(a.semantics_defines ? a.semantics_defines : "", b.semantics_defines ? b.semantics_defines : "");
}

static void writeVariant(GPUSceneRegistry& res, u32 id) {
	const GPUVariant& v = res.variants[id];
	u32* out = (u32*)mirrorWrite(res.variant_table, id * 16, 16);
	if (v.group == GPUSceneConst::INVALID_GROUP) {
		out[0] = 0xffFFffFF;
		out[1] = out[2] = out[3] = 0;
		return;
	}
	out[0] = v.group | (v.index_in_group << 16);
	out[1] = v.first_index;
	out[2] = v.base_vertex;
	out[3] = v.index_count;
}

// A group without variants is free: its variants (all of them are in its list of free variants) become free ids, the commands of the group are gone.
static void freeDrawGroup(GPUSceneRegistry& res, u32 group_id) {
	GPUDrawGroup& group = res.draw_groups[group_id];
	ASSERT(group.live_variants == 0);
	u32 v = group.free_variants;
	while (v != GPUSceneConst::INVALID_VARIANT) {
		GPUVariant& variant = res.variants[v];
		const u32 next = variant.next_free;
		variant.group = GPUSceneConst::INVALID_GROUP;
		variant.mesh = nullptr;
		variant.next_free = res.free_variants;
		res.free_variants = v;
		writeVariant(res, v);
		v = next;
	}
	const auto iter = res.draw_group_map.find(group.hash);
	if (iter.isValid() && iter.value() == group_id) res.draw_group_map.erase(iter);
	memset(&group, 0, sizeof(group));
	group.free_variants = GPUSceneConst::INVALID_VARIANT;
	res.free_groups.push(group_id);
}

static u32 findOrAddDrawGroup(GPUSceneRegistry& res, const GPUDrawGroup& key) {
	const u64 hash = hashDrawGroup(key);
	const auto iter = res.draw_group_map.find(hash);
	if (iter.isValid()) {
		if (sameDrawGroup(res.draw_groups[iter.value()], key)) return iter.value();
		// two different groups with the same hash
		for (u32 i = 0, c = res.draw_groups.size(); i < c; ++i) {
			if (res.draw_groups[i].shader && sameDrawGroup(res.draw_groups[i], key)) return i;
		}
	}
	u32 id;
	if (!res.free_groups.empty()) {
		id = res.free_groups.back();
		res.free_groups.pop();
	}
	else {
		if (res.draw_groups.size() >= (i32)GPUSceneConst::MAX_DRAW_GROUPS) {
			static bool reported = false;
			if (!reported) logError("GPU scene: more than ", GPUSceneConst::MAX_DRAW_GROUPS, " different programs of meshes, the meshes of the other ones are not drawn");
			reported = true;
			return GPUSceneConst::INVALID_GROUP;
		}
		id = res.draw_groups.size();
		res.draw_groups.emplace();
	}
	GPUDrawGroup& group = res.draw_groups[id];
	group = key;
	group.variant_count = 0;
	group.live_variants = 0;
	group.free_variants = GPUSceneConst::INVALID_VARIANT;
	group.hash = hash;
	if (!iter.isValid()) res.draw_group_map.insert(hash, id);
	return id;
}

// The variant (what one command of an indirect draw draws) of a mesh of a model with a material, counted.
static u32 acquireVariant(GPUSceneRegistry& res, u32 model_id, const Model& model, u32 mesh_idx, const Material* material) {
	if (!material || !material->isReady() || !material->getShader()) return GPUSceneConst::INVALID_VARIANT;
	if (mesh_idx >= (u32)model.getMeshCount()) return GPUSceneConst::INVALID_VARIANT;
	const Mesh& mesh = model.getMesh(mesh_idx);
	const bool lod = isLodModelId(model_id);
	const GPUMeshPool* pool = nullptr;
	if (lod) {
		pool = getLodPool(res.lod, modelIndex(model_id), mesh_idx);
		if (!pool) return GPUSceneConst::INVALID_VARIANT;
	}
	else if (!isClusterMeshDrawn(model, mesh_idx)) {
		return GPUSceneConst::INVALID_VARIANT;
	}

	GPUDrawGroup key;
	memset(&key, 0, sizeof(key));
	key.shader = material->getShader();
	key.define_mask = material->getDefineMask();
	if (lod && mesh.type == Mesh::SKINNED) key.define_mask |= 1 << res.skinned_define_idx;
	key.state = material->m_render_states;
	key.primitive_type = mesh.primitive_type;
	key.semantics_defines = mesh.semantics_defines;
	key.layer = (u8)material->getLayer();
	key.lod = lod;
	key.index_type = lod ? mesh.index_type : gpu::DataType::U16;
	const u32 group_id = findOrAddDrawGroup(res, key);
	if (group_id == GPUSceneConst::INVALID_GROUP) return GPUSceneConst::INVALID_VARIANT;

	// meshes with clusters share the variant of their group, its draw is as long as the longest of them
	const Mesh* mesh_key = lod ? &mesh : nullptr;
	const u32 index_count = lod ? (u32)mesh.indices_count : (u32)mesh.cluster_index_stride;
	const u64 hash = hashMix(hashMix(hashBegin(), group_id), (u64)(uintptr)mesh_key);
	const auto iter = res.variant_map.find(hash);
	u32 id = GPUSceneConst::INVALID_VARIANT;
	if (iter.isValid()) {
		const GPUVariant& found = res.variants[iter.value()];
		if (found.group == group_id && found.mesh == mesh_key && found.refs > 0) {
			id = iter.value();
		}
		else {
			// two different variants with the same hash
			for (u32 i = 0, c = res.variants.size(); i < c; ++i) {
				if (res.variants[i].group == group_id && res.variants[i].mesh == mesh_key && res.variants[i].refs > 0) {
					id = i;
					break;
				}
			}
		}
	}
	if (id != GPUSceneConst::INVALID_VARIANT) {
		GPUVariant& variant = res.variants[id];
		++variant.refs;
		if (index_count > variant.index_count) {
			variant.index_count = index_count;
			writeVariant(res, id);
		}
		return id;
	}

	// a new one: a free variant of the group keeps its command, otherwise the group gets one more command
	GPUDrawGroup& group = res.draw_groups[group_id];
	if (group.free_variants != GPUSceneConst::INVALID_VARIANT) {
		id = group.free_variants;
		group.free_variants = res.variants[id].next_free;
	}
	else {
		if (res.free_variants != GPUSceneConst::INVALID_VARIANT) {
			id = res.free_variants;
			res.free_variants = res.variants[id].next_free;
		}
		else {
			if (res.variants.size() >= (i32)GPUSceneConst::MAX_VARIANTS) {
				static bool reported = false;
				if (!reported) logError("GPU scene: more than ", GPUSceneConst::MAX_VARIANTS, " different meshes with materials, the other ones are not drawn");
				reported = true;
				if (group.live_variants == 0) freeDrawGroup(res, group_id);
				return GPUSceneConst::INVALID_VARIANT;
			}
			id = res.variants.size();
			res.variants.emplace();
			mirrorResize(res.variant_table, res.variants.size() * 16);
		}
		res.variants[id].group = group_id;
		res.variants[id].index_in_group = group.variant_count++;
	}
	GPUVariant& variant = res.variants[id];
	variant.mesh = mesh_key;
	variant.first_index = lod ? pool->index_offset / (mesh.areIndices16() ? 2 : 4) : 0;
	variant.base_vertex = lod ? pool->vertex_offset / mesh.vb_stride : 0;
	variant.index_count = index_count;
	variant.refs = 1;
	variant.next_free = GPUSceneConst::INVALID_VARIANT;
	variant.hash = hash;
	++group.live_variants;
	if (!iter.isValid()) res.variant_map.insert(hash, id);
	writeVariant(res, id);
	return id;
}

static void releaseVariant(GPUSceneRegistry& res, u32 id) {
	if (id == GPUSceneConst::INVALID_VARIANT) return;
	GPUVariant& variant = res.variants[id];
	ASSERT(variant.refs > 0);
	if (--variant.refs > 0) return;
	const auto iter = res.variant_map.find(variant.hash);
	if (iter.isValid() && iter.value() == id) res.variant_map.erase(iter);
	// it keeps its command (nothing counts into it, it draws nothing) for the next variant of the group
	const u32 group_id = variant.group;
	GPUDrawGroup& group = res.draw_groups[group_id];
	variant.next_free = group.free_variants;
	group.free_variants = id;
	ASSERT(group.live_variants > 0);
	if (--group.live_variants == 0) freeDrawGroup(res, group_id);
}

// material sets

static u64 hashOverrides(u32 model_id, Span<const GPUMaterialOverride> overrides) {
	u64 hash = hashMix(hashBegin(), model_id);
	for (const GPUMaterialOverride& o : overrides) {
		hash = hashMix(hash, o.mesh);
		hash = hashMix(hash, o.material_index);
		hash = hashMix(hash, (u64)(uintptr)o.material);
	}
	return hash;
}

static bool sameSet(const GPUSceneRegistry& res, const GPUMaterialSet& set, u32 model_id, Span<const GPUMaterialOverride> overrides) {
	if (set.model_id != model_id) return false;
	u32 next = 0;
	for (u32 i = 0; i < set.count; ++i) {
		const GPUSetEntry& e = res.set_entries[set.first + i];
		if (!e.own) continue;
		if (next >= overrides.length()) return false;
		const GPUMaterialOverride& o = overrides[next++];
		if (o.mesh != i || o.material != e.material || o.material_index != e.material_index) return false;
	}
	return next == overrides.length();
}

// the variants of the entries of the set from their materials (the materials of the model for the entries that are not own)
static void resolveSet(GPUSceneRegistry& res, u32 set_id) {
	const GPUMaterialSet& set = res.sets[set_id];
	const Model* model = set.model;
	for (u32 i = 0; i < set.count; ++i) {
		const u32 index = set.first + i;
		GPUSetEntry& e = res.set_entries[index];
		if (!e.own) {
			const MeshMaterial& mesh_material = model->getMeshMaterial(i);
			e.material = mesh_material.material;
			e.material_index = (u32)mesh_material.material_index;
		}
		// the new one first, so that a group that both use is not freed in between
		const u32 variant = acquireVariant(res, set.model_id, *model, i, e.material);
		releaseVariant(res, e.variant);
		e.variant = variant;
		u32* out = (u32*)mirrorWrite(res.set_table, index * 8, 8);
		out[0] = variant;
		out[1] = e.material_index;
	}
}

static u32 findOrMakeSet(GPUSceneRegistry& res, u32 model_id, const Model& model, Span<const GPUMaterialOverride> overrides) {
	const u64 hash = hashOverrides(model_id, overrides);
	const auto iter = res.set_map.find(hash);
	if (iter.isValid()) {
		if (sameSet(res, res.sets[iter.value()], model_id, overrides)) return iter.value();
		// two different sets with the same hash
		for (u32 i = 0, c = res.sets.size(); i < c; ++i) {
			if (sameSet(res, res.sets[i], model_id, overrides)) return i;
		}
	}

	const u32 count = (u32)model.getMeshCount();
	u32 id;
	if (!res.free_sets.empty()) {
		id = res.free_sets.back();
		res.free_sets.pop();
	}
	else {
		id = res.sets.size();
		res.sets.emplace();
	}
	GPUMaterialSet& set = res.sets[id];
	set.model_id = model_id;
	set.model = &model;
	set.first = rangeAlloc(res.set_ranges, count);
	set.count = count;
	set.refs = 0;
	set.hash = hash;
	if (res.set_ranges.size > (u32)res.set_entries.size()) {
		res.set_entries.resize(res.set_ranges.size);
		mirrorResize(res.set_table, res.set_ranges.size * 8);
	}
	for (u32 i = 0; i < count; ++i) {
		GPUSetEntry& e = res.set_entries[set.first + i];
		e.material = nullptr;
		e.material_index = 0;
		e.variant = GPUSceneConst::INVALID_VARIANT;
		e.own = false;
	}
	for (const GPUMaterialOverride& o : overrides) {
		if (o.mesh >= count) continue;
		GPUSetEntry& e = res.set_entries[set.first + o.mesh];
		e.material = o.material;
		e.material_index = o.material_index;
		e.own = true;
	}
	if (!iter.isValid()) res.set_map.insert(hash, id);
	resolveSet(res, id);
	return id;
}

static void freeSet(GPUSceneRegistry& res, u32 set_id) {
	GPUMaterialSet& set = res.sets[set_id];
	for (u32 i = 0; i < set.count; ++i) {
		const u32 index = set.first + i;
		GPUSetEntry& e = res.set_entries[index];
		releaseVariant(res, e.variant);
		e.variant = GPUSceneConst::INVALID_VARIANT;
		e.material = nullptr;
		e.own = false;
		u32* out = (u32*)mirrorWrite(res.set_table, index * 8, 8);
		out[0] = GPUSceneConst::INVALID_VARIANT;
		out[1] = 0;
	}
	rangeFree(res.set_ranges, set.first, set.count);
	const auto iter = res.set_map.find(set.hash);
	if (iter.isValid() && iter.value() == set_id) res.set_map.erase(iter);
	set.model_id = GPUSceneConst::INVALID_MODEL;
	set.model = nullptr;
	set.count = 0;
	res.free_sets.push(set_id);
}

u32 acquireGPUMaterialSet(GPUSceneRegistry& res, u32 model_id, Span<const GPUMaterialOverride> overrides) {
	if (model_id == GPUSceneConst::INVALID_MODEL || overrides.length() == 0) return GPUSceneConst::INVALID_SET;
	const Model* model = getGPUModel(res, model_id);
	if (model->getMeshCount() == 0) return GPUSceneConst::INVALID_SET;
	const u32 set = findOrMakeSet(res, model_id, *model, overrides);
	++res.sets[set].refs;
	return set;
}

void releaseGPUMaterialSet(GPUSceneRegistry& res, u32 set) {
	if (set == GPUSceneConst::INVALID_SET) return;
	ASSERT(res.sets[set].refs > 0);
	if (--res.sets[set].refs == 0) freeSet(res, set);
}

u32 acquireDefaultSet(GPUSceneRegistry& res, u32 model_id, const Model& model) {
	const u32 set = findOrMakeSet(res, model_id, model, Span<const GPUMaterialOverride>());
	++res.sets[set].refs;
	return set;
}

// models

u32 acquireGPUModel(GPUSceneRegistry& res, const Model& model) {
	// the kind of a model is the one it had when it was acquired first
	u32 id = findClusterModel(res.clusters, model);
	if (id != GPUSceneConst::INVALID_MODEL) {
		++res.clusters.entries[id].refs;
		return id;
	}
	id = findLodModel(res.lod, model);
	if (id != GPUSceneConst::INVALID_MODEL) {
		++res.lod.entries[id].refs;
		return id | GPUSceneConst::LOD_MODEL_BIT;
	}
	if (model.hasClusters()) return addClusterModel(res, model);
	return addLodModel(res, model) | GPUSceneConst::LOD_MODEL_BIT;
}

void releaseGPUModel(GPUSceneRegistry& res, u32 model_id) {
	if (model_id == GPUSceneConst::INVALID_MODEL) return;
	if (isLodModelId(model_id)) releaseLodModel(res, modelIndex(model_id));
	else releaseClusterModel(res, model_id);
}

void flushGPUSceneRegistry(GPUSceneRegistry& res) {
	// a material that was unloaded (reloaded) can have another program now, the sets get the variants again (the models that depend on it are released and acquired
	// again by their instances, this is for the materials of overrides)
	const u32 reload_counter = res.renderer.getMaterialReloadCounter();
	if (reload_counter != res.reload_counter) {
		res.reload_counter = reload_counter;
		for (u32 i = 0, c = res.sets.size(); i < c; ++i) {
			if (res.sets[i].model_id != GPUSceneConst::INVALID_MODEL) resolveSet(res, i);
		}
	}

	const bool any = !res.lod.pool_copies.empty() || res.clusters.model_table.any_dirty || res.clusters.mesh_table.any_dirty || res.lod.model_table.any_dirty
		|| res.set_table.any_dirty || res.variant_table.any_dirty
		|| !res.clusters.model_table.buffer || !res.clusters.mesh_table.buffer || !res.lod.model_table.buffer || !res.set_table.buffer || !res.variant_table.buffer;
	if (!any) return;
	PROFILE_FUNCTION();

	flushClusterModels(res);
	flushLodModels(res);
	mirrorUpload(res.set_table, res.renderer, res.queue);
	mirrorUpload(res.variant_table, res.renderer, res.queue);
	submitCopies(res.queue, res.renderer, "gpu resources");
}

} // namespace Lumix
