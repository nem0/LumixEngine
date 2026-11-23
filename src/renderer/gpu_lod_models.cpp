#include "gpu_lod_models.h"

#include "core/log.h"
#include "core/span.h"
#include "renderer/gpu_scene_registry.h"
#include "renderer/model.h"
#include "renderer/renderer.h"

namespace Lumix {

GPULodModels::GPULodModels(IAllocator& allocator)
	: entries(allocator)
	, free_entries(allocator)
	, map(allocator)
	, model_table(allocator, "gpu scene lod models")
	, pool_copies(allocator)
{}

u32 findLodModel(const GPULodModels& models, const Model& model) {
	const auto iter = models.map.find(&model);
	return iter.isValid() ? iter.value() : GPUSceneConst::INVALID_MODEL;
}

const GPUMeshPool* getLodPool(const GPULodModels& models, u32 index, u32 mesh_index) {
	const GPULodModelEntry& entry = models.entries[index];
	if (!entry.pools || mesh_index >= entry.pool_count) return nullptr;
	return &entry.pools[mesh_index];
}

static void freePools(GPUSceneRegistry& res, u32 index) {
	GPULodModelEntry& entry = res.lod.entries[index];
	if (!entry.pools) return;
	for (u32 i = 0; i < entry.pool_count; ++i) {
		res.renderer.freeClusterData(entry.pools[i].vertex_offset, entry.pools[i].vertex_size);
		res.renderer.freeClusterData(entry.pools[i].index_offset, entry.pools[i].index_size);
	}
	res.allocator.deallocate(entry.pools);
	entry.pools = nullptr;
	entry.pool_count = 0;
}

static bool allocPools(GPUSceneRegistry& res, u32 index, const Model& model) {
	GPULodModelEntry& entry = res.lod.entries[index];
	if (entry.pools) return true;
	const u32 count = (u32)model.getMeshCount();
	if (count == 0) return false;
	GPUMeshPool* pools = (GPUMeshPool*)res.allocator.allocate(sizeof(GPUMeshPool) * count, alignof(GPUMeshPool));
	u32 done = 0;
	bool ok = true;
	for (u32 i = 0; i < count && ok; ++i) {
		const Mesh& mesh = model.getMesh(i);
		GPUMeshPool& p = pools[i];
		memset(&p, 0, sizeof(p));
		p.vertex_size = mesh.vertices.size() * mesh.vb_stride;
		p.index_size = (u32)mesh.indices.size();
		// vertices are addressed by the base vertex of a command, so they start at a multiple of their size
		if (!res.renderer.allocClusterRange(p.vertex_size, mesh.vb_stride, p.vertex_offset)) {
			ok = false;
		}
		else if (!res.renderer.allocClusterRange(p.index_size, 4, p.index_offset)) {
			res.renderer.freeClusterData(p.vertex_offset, p.vertex_size);
			ok = false;
		}
		else {
			++done;
		}
	}
	if (!ok) {
		for (u32 i = 0; i < done; ++i) {
			res.renderer.freeClusterData(pools[i].vertex_offset, pools[i].vertex_size);
			res.renderer.freeClusterData(pools[i].index_offset, pools[i].index_size);
		}
		res.allocator.deallocate(pools);
		logError("GPU scene: no space for the meshes of ", model.getPath(), ", it is not drawn");
		return false;
	}
	entry.pools = pools;
	entry.pool_count = count;
	for (u32 i = 0; i < count; ++i) {
		const Mesh& mesh = model.getMesh(i);
		res.lod.pool_copies.push({ mesh.vertex_buffer_handle, pools[i].vertex_offset, pools[i].vertex_size });
		res.lod.pool_copies.push({ mesh.index_buffer_handle, pools[i].index_offset, pools[i].index_size });
	}
	return true;
}

// Writes the entry of the model. A model that can not be drawn (not complete, no room for its meshes) has no LODs.
static void buildLodModel(GPUSceneRegistry& res, u32 index) {
	GPULodModels& models = res.lod;
	GPULodModelEntry& entry = models.entries[index];
	const Model& model = *entry.model;
	GPULodModel& gm = *(GPULodModel*)mirrorWrite(models.model_table, index * sizeof(GPULodModel), sizeof(GPULodModel));
	memset(&gm, 0, sizeof(gm));

	if (model.getMeshCount() == 0 || !model.isReady()) return;
	if (!model.canUseGPUScene() || !allocPools(res, index, model)) return;

	// the set has the variants of the meshes, which need the pools
	entry.default_set = acquireDefaultSet(res, index | GPUSceneConst::LOD_MODEL_BIT, model);
	gm.default_first = res.sets[entry.default_set].first;

	// all the meshes of all the LODs
	const LODMeshIndices* lods = model.getLODIndices();
	auto pack = [&](u32 l) { return lods[l].to >= lods[l].from && lods[l].from >= 0 ? u32(lods[l].from) | (u32(lods[l].to + 1) << 16) : 0u; };
	for (u32 l = 0; l < 4; ++l) {
		gm.lod[l] = pack(l);
		gm.lod_distance[l] = model.getLODDistances()[l];
	}
	gm.lod4 = pack(4);
}

u32 addLodModel(GPUSceneRegistry& res, const Model& model) {
	GPULodModels& models = res.lod;
	u32 index;
	if (!models.free_entries.empty()) {
		index = models.free_entries.back();
		models.free_entries.pop();
	}
	else {
		index = models.entries.size();
		models.entries.emplace();
		mirrorResize(models.model_table, models.entries.size() * sizeof(GPULodModel));
	}
	GPULodModelEntry& entry = models.entries[index];
	memset(&entry, 0, sizeof(entry));
	entry.model = &model;
	entry.refs = 1;
	entry.default_set = GPUSceneConst::INVALID_SET;
	models.map.insert(&model, index);
	buildLodModel(res, index);
	return index;
}

void releaseLodModel(GPUSceneRegistry& res, u32 index) {
	GPULodModels& models = res.lod;
	GPULodModelEntry& entry = models.entries[index];
	ASSERT(entry.refs > 0);
	if (--entry.refs > 0) return;

	if (entry.default_set != GPUSceneConst::INVALID_SET) releaseGPUMaterialSet(res, entry.default_set);
	freePools(res, index);
	memset(mirrorWrite(models.model_table, index * sizeof(GPULodModel), sizeof(GPULodModel)), 0, sizeof(GPULodModel));
	models.map.erase(entry.model);
	memset(&entry, 0, sizeof(entry));
	entry.default_set = GPUSceneConst::INVALID_SET;
	models.free_entries.push(index);
}

void flushLodModels(GPUSceneRegistry& res) {
	// the copies of the vertices and indices of the meshes to the ranges of the buffer of cluster data
	const gpu::BufferHandle pool = res.renderer.getClusterDataBuffer();
	for (const GPUPoolCopy& copy : res.lod.pool_copies) queueCopy(res.queue, pool, copy.dst_offset, copy.src, 0, copy.size, false);
	res.lod.pool_copies.clear();
	mirrorUpload(res.lod.model_table, res.renderer, res.queue);
}

void destroyLodModels(GPUSceneRegistry& res) {
	for (u32 i = 0, c = res.lod.entries.size(); i < c; ++i) freePools(res, i);
	mirrorDestroy(res.lod.model_table, res.renderer);
}

} // namespace Lumix
