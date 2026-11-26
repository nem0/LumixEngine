#pragma once

#include "renderer/editor/cluster_builder.h"

namespace Lumix {

// Internal shared types between cluster_builder.cpp and cluster_voxels.cpp.

struct DagBounds {
	Vec3 center;
	float radius;
	float error;
};

struct DagCluster {
	u32 index_offset;
	u32 index_count;
	int refined;
	u32 material;
	DagBounds bounds;
};

struct ClusterBuildContext {
	ClusterBuildContext(MeshClusters& out, IAllocator& allocator, const Vec3* positions, bool dilate_borders)
		: out(out)
		, allocator(allocator)
		, positions(positions)
		, dilate_borders(dilate_borders)
		, level_ranges(allocator)
		, group_bounds(allocator)
	{}

	MeshClusters& out;
	IAllocator& allocator;
	const Vec3* positions;
	bool dilate_borders;

	Array<u32> level_ranges;		// [first cluster, number of clusters] of levels above the finest
	Array<DagBounds> group_bounds;
	int level = -1;
	u32 level_first = 0;
	u32 group_id_counter = 0;
	bool leaves_done = false;
};

DagBounds dagBoundsMerge(const DagBounds* bounds, u32 count, size_t stride);
void closeClusterLevel(ClusterBuildContext& ctx);
void beginClusterGroup(ClusterBuildContext& ctx, u32 count, int group_depth);

// Voxel level builder (implemented in cluster_voxels.cpp).
struct Voxel;
struct VoxelLevel;

struct ClusterVoxelBuilder {
	static constexpr float SIMPLIFY_RATIO = 0.5f;

	ClusterVoxelBuilder(IAllocator& allocator);
	~ClusterVoxelBuilder();

	bool init(const ClusterVoxelSource* source, const Vec3* positions, const Vec3* normals, u32 vertex_count,
		const Array<DagCluster>& leaf_clusters, const Array<u32>& cluster_indices, bool dilate_borders);

	float levelSize(u32 k) const { return base_size * float(1u << k); }
	float levelError(u32 k) const;
	u32 chooseLevel(float min_error, float group_avg_edge) const;
	bool shouldSwitchGroup(u32 merged_count, u32 simplified_count, float prev_error, float simplify_error, float group_avg_edge) const;
	float computeAvgEdge(const Vec3* positions, const u32* indices, u32 index_count) const;

	void voxelizeGroup(ClusterBuildContext& ctx, const u32* tri_indices, u32 tri_index_count, int refined, u32 k);
	void buildStagedLevels(ClusterBuildContext& ctx, int first_depth);

	IAllocator& allocator;
	const ClusterVoxelSource* source = nullptr;
	const Vec3* positions = nullptr;
	const Vec3* normals = nullptr;
	bool enabled = false;
	Vec3 origin = Vec3(0, 0, 0);
	float extent = 0.f;
	float base_size = 0.f;
	float error_threshold = FLT_MAX;
	float min_coverage = 0.25f;
	float downsample_scale = 0.25f;
	float stall_threshold = 0.65f;
	float max_resolution = 4000.f;

	Array<VoxelLevel*> staged_levels;
};

} // namespace Lumix
