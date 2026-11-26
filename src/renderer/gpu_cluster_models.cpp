#include "gpu_cluster_models.h"

#include "core/span.h"
#include "renderer/gpu_scene_registry.h"
#include "renderer/mesh_clusters.h"
#include "renderer/model.h"
#include "renderer/renderer.h"

namespace Lumix {

static_assert(CLUSTER_GPU_SIZE == sizeof(MeshClusterGPU));
static_assert(CLUSTER_NODE_GPU_SIZE == sizeof(MeshNodeGPU));
static_assert(CLUSTER_HEADER_GPU_SIZE == sizeof(MeshClusterVertexGPU));

GPUClusterModels::GPUClusterModels(IAllocator& allocator)
	: entries(allocator)
	, free_entries(allocator)
	, map(allocator)
	, model_table(allocator, "gpu scene cluster models")
	, mesh_ranges(allocator)
	, mesh_table(allocator, "gpu scene cluster meshes")
	, depth_counts(allocator)
{}

u32 getMaxTreeDepth(const GPUClusterModels& models) {
	for (i32 depth = models.depth_counts.size() - 1; depth > 0; --depth) {
		if (models.depth_counts[depth] > 0) return (u32)depth;
	}
	return 0;
}

static void addDepth(GPUClusterModels& models, u32 depth, i32 delta) {
	if (depth >= (u32)models.depth_counts.size()) {
		const u32 old = models.depth_counts.size();
		models.depth_counts.resize(depth + 1);
		for (u32 i = old; i <= depth; ++i) models.depth_counts[i] = 0;
	}
	ASSERT(delta > 0 || models.depth_counts[depth] > 0);
	models.depth_counts[depth] += delta;
}

bool isClusterMeshDrawn(const Model& model, u32 mesh_index) {
	return (i32)mesh_index <= model.getClusterMeshesEnd();
}

u32 findClusterModel(const GPUClusterModels& models, const Model& model) {
	const auto iter = models.map.find(&model);
	return iter.isValid() ? iter.value() : GPUSceneConst::INVALID_MODEL;
}

// Writes the entry of the model and the entries of its meshes. A model that can not be drawn (not complete) has no meshes.
static void buildClusterModel(GPUSceneRegistry& res, u32 id) {
	GPUClusterModels& models = res.clusters;
	GPUClusterModelEntry& entry = models.entries[id];
	const Model& model = *entry.model;
	GPUClusterModel& gm = *(GPUClusterModel*)mirrorWrite(models.model_table, id * sizeof(GPUClusterModel), sizeof(GPUClusterModel));
	memset(&gm, 0, sizeof(gm));

	if (model.getMeshCount() == 0 || !model.isReady()) return;
	// a model that is not complete has no meshes in the table rather than some
	const i32 end = model.getClusterMeshesEnd();
	if (end < 0) return;
	const Mesh::MeshClusterData& mc = model.getMesh(0).clusters;
	if (mc.table_size == 0 || mc.cluster_count == 0 || mc.group_size == 0) return;

	entry.default_set = acquireDefaultSet(res, id, model);
	const u32 default_first = res.sets[entry.default_set].first;

	entry.first_mesh = rangeAlloc(models.mesh_ranges, 1);
	entry.mesh_count = 1;
	if (models.mesh_ranges.size * sizeof(GPUClusterMesh) > models.mesh_table.data.size()) mirrorResize(models.mesh_table, models.mesh_ranges.size * sizeof(GPUClusterMesh));
	GPUClusterMesh* meshes = (GPUClusterMesh*)mirrorWrite(models.mesh_table, entry.first_mesh * sizeof(GPUClusterMesh), sizeof(GPUClusterMesh));
	memset(meshes, 0, sizeof(GPUClusterMesh));

	gm.first_mesh = entry.first_mesh;
	gm.mesh_count = 1;
	GPUClusterMesh& m = meshes[0];
	m.clusters = mc.table_offset;
	m.group_table = mc.group_offset;
	m.cluster_count = mc.cluster_count;
	m.node_count = mc.node_count;
	m.root_count = mc.root_count;
	m.material_entry = default_first;
	m.index_in_model = 0;
	// the shaders derive these
	ASSERT(clusterNodesOffset(m) == mc.node_table_offset);
	ASSERT(clusterHeadersOffset(m) == mc.header_offset);
	entry.tree_depth = mc.tree_depth;
	addDepth(models, entry.tree_depth, 1);
}

u32 addClusterModel(GPUSceneRegistry& res, const Model& model) {
	GPUClusterModels& models = res.clusters;
	u32 id;
	if (!models.free_entries.empty()) {
		id = models.free_entries.back();
		models.free_entries.pop();
	}
	else {
		id = models.entries.size();
		models.entries.emplace();
		mirrorResize(models.model_table, models.entries.size() * sizeof(GPUClusterModel));
	}
	GPUClusterModelEntry& entry = models.entries[id];
	memset(&entry, 0, sizeof(entry));
	entry.model = &model;
	entry.refs = 1;
	entry.default_set = GPUSceneConst::INVALID_SET;
	models.map.insert(&model, id);
	buildClusterModel(res, id);
	return id;
}

void releaseClusterModel(GPUSceneRegistry& res, u32 id) {
	GPUClusterModels& models = res.clusters;
	GPUClusterModelEntry& entry = models.entries[id];
	ASSERT(entry.refs > 0);
	if (--entry.refs > 0) return;

	if (entry.mesh_count > 0) {
		GPUClusterMesh* meshes = (GPUClusterMesh*)mirrorWrite(models.mesh_table, entry.first_mesh * sizeof(GPUClusterMesh), entry.mesh_count * sizeof(GPUClusterMesh));
		memset(meshes, 0, entry.mesh_count * sizeof(GPUClusterMesh));
		rangeFree(models.mesh_ranges, entry.first_mesh, entry.mesh_count);
		addDepth(models, entry.tree_depth, -1);
	}
	if (entry.default_set != GPUSceneConst::INVALID_SET) releaseGPUMaterialSet(res, entry.default_set);
	memset(mirrorWrite(models.model_table, id * sizeof(GPUClusterModel), sizeof(GPUClusterModel)), 0, sizeof(GPUClusterModel));
	models.map.erase(entry.model);
	memset(&entry, 0, sizeof(entry));
	entry.default_set = GPUSceneConst::INVALID_SET;
	models.free_entries.push(id);
}

void flushClusterModels(GPUSceneRegistry& res) {
	mirrorUpload(res.clusters.model_table, res.renderer, res.queue);
	mirrorUpload(res.clusters.mesh_table, res.renderer, res.queue);
}

void destroyClusterModels(GPUSceneRegistry& res) {
	mirrorDestroy(res.clusters.model_table, res.renderer);
	mirrorDestroy(res.clusters.mesh_table, res.renderer);
}

} // namespace Lumix
