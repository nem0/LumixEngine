#ifndef GPU_SCENE_LAYOUT_HLSLI
#define GPU_SCENE_LAYOUT_HLSLI

// Names of the byte offsets, strides, flags and sentinels of the buffers of the GPU scene, shared by gpu_scene.hlsl, gpu_scene_cull.hlsl, nanite.hlsl,
// gpu_scene_variants.hlsl and vertex_pulling.hlsli. Each group is the layout of a struct in src/renderer, change both together.

// units of positions in the GPU scene (fixed point), GPUSceneConst::POS_UNIT
#define SCENE_POS_UNIT (1.0 / 1024.0)

// a dispatch with more groups than a row has is split into rows of this many groups
#define MAX_GROUPS_X 65535

#define NO_MODEL 0xffffffff			// GPUSceneInstance::model of a slot with nothing to draw
#define NO_CLUSTER 0xffffffff		// the cluster of an entry of the selected clusters that is a mesh without clusters
#define NO_MATERIAL_SET 0xffffffff	// GPUSceneInstance::material_set: the materials of its model
#define NO_STATE 0xffffffff			// GPUSceneInstance::moved_frame of a slot that has no transform yet
#define NO_VARIANT 0xffffffff		// the variant of a mesh that is not drawn, or the id of a free variant
#define NO_COMMAND_GROUP 0xffffffff	// the first command of a draw group that is not drawn in a view
#define NO_GROUP 0xffffffff			// the group of the children of a cluster without children
#define MODEL_LOD_BIT 0x80000000	// of the id of a model: it is a model without clusters (GPULodModel), otherwise GPUClusterModel

// GPUSceneBounds, 16 bytes: int3 p, float radius (< 0: the slot is free or disabled)
#define BOUNDS_STRIDE 16

// GPUSceneInstance, 96 bytes
#define INSTANCE_STRIDE 96
#define INSTANCE_ROT 0
#define INSTANCE_POS 16				// int3, the word after it is the model
#define INSTANCE_MODEL 28
#define INSTANCE_SCALE 32			// float3, the word after it is the flags
#define INSTANCE_FLAGS 44
#define INSTANCE_PREV_ROT 48
#define INSTANCE_PREV_POS 64		// int3, the word after it is the material set
#define INSTANCE_MATERIAL_SET 76
#define INSTANCE_PREV_SCALE 80		// float3, the word after it is moved_frame
#define INSTANCE_MOVED_FRAME 92
#define INSTANCE_FORCED_LOD 1		// GPUSceneInstance::FORCED_LOD, the LOD is in the bits 1..3 of the flags
#define INSTANCE_FORCED_LOD_SHIFT 1
#define INSTANCE_FORCED_LOD_MASK 7

// GPUSceneEntry, 128 bytes: slot, mask, ..., the bounds at 16 and a GPUSceneInstance at 32
#define ENTRY_STRIDE 128
#define ENTRY_SLOT 0
#define ENTRY_MASK 4
#define ENTRY_BOUNDS 16
#define ENTRY_INSTANCE 32

// GPUSceneChunk, 32 bytes
#define CHUNK_STRIDE 32
#define CHUNK_P 0					// int3
#define CHUNK_MODEL 12
#define CHUNK_INSTANCE_BUFFER 16	// bindless handle
#define CHUNK_INSTANCE_RANGE 20		// first instance, instance count
#define CHUNK_INSTANCE_RADIUS 28

// an instance of a chunk, 32 bytes: rotation xyz and lod, position (relative to the chunk) and uniform scale
#define CHUNK_INSTANCE_STRIDE 32
#define CHUNK_INSTANCE_ROT_LOD 0
#define CHUNK_INSTANCE_POS_SCALE 16

// the instance data of a GPU-scene indirect draw (32 bytes), written by gpu_scene_variants.hlsl and read by vertex_pulling.hlsli / surface shaders (INDIRECT_INSTANCE)
#define DRAW_INSTANCE_STRIDE 32
#define DRAW_INSTANCE_SLOT 0			// uint4(slot, ref, material, base_vertex / cluster)
#define DRAW_INSTANCE_REF 4				// 0 for a model instance, otherwise instance index + 1 in the chunk's instance buffer
#define DRAW_INSTANCE_MATERIAL 8		// material index
#define DRAW_INSTANCE_BASE_VERTEX 12	// base vertex of a mesh without clusters (POOLED_VERTICES), or cluster index (CLUSTER_MESH, for CLUSTER_DEBUG)
#define DRAW_INSTANCE_CLUSTER 12
#define DRAW_INSTANCE_BONES 16			// byte offset of bones (SKINNED), or resolved cluster addresses (CLUSTER_MESH): uint4(stream_base, index_base, header_addr, index_count)
#define DRAW_INSTANCE_CLUSTER_DATA 16

// the list of selected clusters, from the clusters selected by nanite.hlsl or gpu_scene_cull.hlsl to gpu_scene_variants.hlsl
#define SELECTED_COUNT 0				// u32
#define SELECTED_HEADER 16
#define SELECTED_STRIDE 16				// slot, mesh, cluster | (depth_bin << 27), instance
#define SELECTED_CLUSTER_MASK 0x07ffffffu
#define SELECTED_DEPTH_BIN_SHIFT 27

// occluded cluster list entry.z flags (nanite.hlsl pass 1 -> POST_OCCLUDED)
#define OCCLUDED_CLUSTER_MASK 0x3fffffffu
#define OCCLUDED_VOXEL_BIT 0x40000000u
#define OCCLUDED_ERROR_SMALL_BIT 0x80000000u

// voxel brick pool / counters layout (voxel_bricks.hlsl, voxels.hlsl)
#define VOXEL_BRICK_STRIDE 16
#define VOXEL_BRICK_SLOT_MASK 0x03ffffffu
#define VOXEL_BRICK_INDEX_SHIFT 26
#define VOXEL_VIEWS_AT 64
#define VOXEL_VIEW_STRIDE 64
#define VOXEL_VIEW_COMMAND 16
#define VOXEL_VIEW_STATS 40
#define VOXEL_OCCLUDED_COUNT 16

// the lists of work between the passes, 16 bytes each: flat items, tree items (mesh, slot, first cluster, instance) and chunk items (slot, first instance, count, 0)
#define ITEM_STRIDE 16

// u_counter of gpu_scene_cull.hlsl, u32 each
#define COUNTER_NODE_0 0
#define COUNTER_TREE 4
#define COUNTER_CHUNK 8
#define COUNTER_NODE_1 12
#define COUNTER_CANDIDATE_CLUSTERS 16
#define COUNTER_COUNT 5
// u_args of gpu_scene_cull.hlsl and gpu_scene_variants.hlsl, 16 bytes each
#define ARGS_MODEL_INSTANCES 0
#define ARGS_POST 16
#define ARGS_TREE 32
#define ARGS_CHUNK_INSTANCES 64
#define ARGS_BIN 48

// u_group_base entries (u32 per draw group): NO_COMMAND_GROUP if not drawn in the view, otherwise:
//   bits 0..15:  first command in this view
//   bits 16..30: sequential cluster draw group index (when bit 31 is set)
//   bit 31:      1 if cluster draw group (!group.lod), 0 if LOD draw group
#define GROUP_BASE_COMMAND_MASK 0xffff
#define GROUP_BASE_CLUSTER_IDX_SHIFT 16
#define GROUP_BASE_CLUSTER_IDX_MASK 0x7fff
#define GROUP_BASE_CLUSTER_BIT 0x80000000

// u_counters layout of gpu_scene_variants.hlsl
#define MAX_VARIANTS 8192
#define DEPTH_BINS 32
#define MAX_DEPTH_BIN_GROUPS 256
#define DEPTH_BIN_TOTAL (MAX_DEPTH_BIN_GROUPS * DEPTH_BINS)
#define COUNTERS_OUT_CURSOR_AT 8
#define COUNTERS_COUNTS_AT 64
#define COUNTERS_CURSORS_AT (COUNTERS_COUNTS_AT + MAX_VARIANTS * 4)
#define COUNTERS_BASES_AT (COUNTERS_COUNTS_AT + MAX_VARIANTS * 8)
#define COUNTERS_BIN_COUNTS_AT (COUNTERS_COUNTS_AT + MAX_VARIANTS * 12)
#define COUNTERS_BIN_CURSORS_AT (COUNTERS_BIN_COUNTS_AT + DEPTH_BIN_TOTAL * 4)
#define COUNTERS_BIN_OFFSETS_AT (COUNTERS_BIN_COUNTS_AT + DEPTH_BIN_TOTAL * 8)

// GPUClusterModel, 8 bytes: first mesh, mesh count
#define CLUSTER_MODEL_STRIDE 8

// GPUClusterMesh, 32 bytes
#define CLUSTER_MESH_STRIDE 32
#define CLUSTER_MESH_SET 24				// 2 words: its entry in the default set of its model, its index in the model

// GPULodModel, 40 bytes
#define LOD_MODEL_STRIDE 40
#define LOD_MODEL_FIRST_SET 0			// the first entry of the default set of the model
#define LOD_MODEL_FAR_RANGE 4			// the meshes of the LOD used beyond the last distance
#define LOD_MODEL_LODS 8				// the meshes of each of the 4 LODs
#define LOD_MODEL_DISTANCES 24			// the squared distances up to which each LOD is used

// an entry of a material set, 8 bytes: variant, material index
#define SET_ENTRY_STRIDE 8

// a variant, 16 bytes: group | index in the group << 16, first index, base vertex, index count
#define VARIANT_STRIDE 16
#define VARIANT_BASE_VERTEX 8
#define VARIANT_GROUP_MASK 0xffff
#define VARIANT_INDEX_SHIFT 16

// the clusters and nodes of meshes, MeshClusterGPU, MeshNodeGPU, MeshGroupAddressGPU in mesh_clusters.h
#define CLUSTER_STRIDE 72
#define CLUSTER_BOUNDS 0
#define CLUSTER_FIRST_INDEX 56
#define CLUSTER_INDEX_COUNT 60
#define CLUSTER_INDEX_COUNT_MASK 0xffff
#define CLUSTER_MATERIAL_SHIFT 16
#define CLUSTER_GROUPS 64				// the group of the cluster, the group of its children
#define CLUSTER_GROUP 64

#define NODE_STRIDE 52
#define NODE_LEAF_BIT 0x80000000
#define NODE_COUNT_MASK 0x7fffffff

#define GROUP_STRIDE 16
#define GROUP_FLAGS 12
#define GROUP_FLAG_LOADED 1
#define GROUP_PAGE_SHIFT 8				// the page of a group is in the bits above the flags
#define ROOT_STRIDE 4

// the records at INSTANCE_*, CHUNK_*, LOD_MODEL_* and CLUSTER_MESH_* offsets as structs, for Load<>. GPUSceneInstance is read in two halves: the previous
// transform is only valid (and loaded) if the instance moved in this frame, see moved_frame.
struct SceneInstance {		// GPUSceneInstance, the first 48 bytes (INSTANCE_ROT)
	float4 rot;
	int3 p;
	uint model;
	float3 scale;
	uint flags;
};

struct SceneInstancePrevious {	// GPUSceneInstance, the last 48 bytes (INSTANCE_PREV_ROT)
	float4 rot;
	int3 p;
	uint material_set;
	float3 scale;
	uint moved_frame;
};

struct SceneChunk {			// GPUSceneChunk
	int3 p;
	uint model;
	uint instance_buffer;
	uint first_instance;
	uint instance_count;
	float instance_radius;
};

struct SceneChunkInstance {	// an instance of a chunk, CHUNK_INSTANCE_STRIDE bytes
	float4 rot_lod;			// rotation xyz and lod
	float4 pos_scale;		// position relative to the chunk and uniform scale
};

struct LodModel {			// GPULodModel
	uint first_set;
	uint far_range;
	uint4 lods;
	float4 distances;
};

struct ClusterMesh {		// GPUClusterMesh
	uint clusters;
	uint group_table;
	uint cluster_count;
	uint node_count;
	uint root_count;
	uint padding;
	uint material_entry;
	uint index_in_model;
};

// the records at CLUSTER_* and NODE_* offsets as structs, for Load<>: MeshClusterGPU and MeshNodeGPU (members are read one after another without padding, as in C++)
struct MeshCluster {
	float4 bounds;				// center, radius
	float4 lod_bounds;
	float4 parent_bounds;
	float error;
	float parent_error;
	uint first_index;
	uint index_count;			// 0 for a voxel cluster
	uint group;
	uint refined_group;			// NO_GROUP for the finest clusters
};

struct MeshNode {
	float4 bounds;
	float4 lod_bounds;
	float min_error;
	float max_parent_error;
	uint first;
	uint count_and_flags;		// NODE_LEAF_BIT, NODE_COUNT_MASK
	uint unrefined;
};

struct VoxelClusterHeader {		// MeshVoxelClusterHeaderGPU, 48 bytes (same slot size as MeshClusterVertexGPU)
	int3 base_voxel;
	uint padding0;
	uint padding1;
	uint brick_count;
	uint payload_bit_offset;
	float voxel_size;
	float3 grid_origin;
	float padding2;
};

#define CLUSTER_HEADER_STRIDE 48		// the header of a cluster in u_vertex_buffer, see pullVertex
#define CLUSTER_ALIGNMENT_MASK 15

uint clusterHeadersOffset(ClusterMesh cm) {
	return cm.clusters + ((cm.cluster_count * CLUSTER_STRIDE + cm.node_count * NODE_STRIDE + cm.root_count * ROOT_STRIDE + CLUSTER_ALIGNMENT_MASK) & ~(uint)CLUSTER_ALIGNMENT_MASK);
}

// The groups of an indirect dispatch of `count` thread groups (split into rows of MAX_GROUPS_X).
uint3 dispatchGroups(uint count) {
	return uint3(min(count, MAX_GROUPS_X), (count + MAX_GROUPS_X - 1) / MAX_GROUPS_X, 1);
}

bool insideFrustum(float4 planes[6], float3 center, float radius) {
	for (int i = 0; i < 6; ++i) {
		if (dot(planes[i], float4(center, 1)) < -radius) return false;
	}
	return true;
}

// Loads the current transform of a model instance (instance_ref == 0) or chunk instance (instance_ref > 0) relative to (cam_p, cam_frac).
void loadSceneTransform(uint instances_buf, uint chunks_buf, int3 cam_p, float3 cam_frac, uint slot, uint instance_ref, out float4 rot, out float3 position, out float3 scale) {
	if (instance_ref == 0) {
		ByteAddressBuffer instances = bindless_buffers[instances_buf];
		const SceneInstance cur = instances.Load<SceneInstance>(slot * INSTANCE_STRIDE);
		rot = cur.rot;
		position = float3(cur.p - cam_p) * SCENE_POS_UNIT - cam_frac;
		scale = cur.scale;
	}
	else {
		ByteAddressBuffer chunks = bindless_buffers[chunks_buf];
		const uint at = slot * CHUNK_STRIDE;
		ByteAddressBuffer data = bindless_buffers[NonUniformResourceIndex(chunks.Load(at + CHUNK_INSTANCE_BUFFER))];
		const SceneChunkInstance ci = data.Load<SceneChunkInstance>((instance_ref - 1) * CHUNK_INSTANCE_STRIDE);
		const float4 rot_lod = ci.rot_lod;
		const float4 pos_scale = ci.pos_scale;
		rot = float4(rot_lod.xyz, sqrt(saturate(1 - dot(rot_lod.xyz, rot_lod.xyz))));
		position = float3(asint(chunks.Load3(at + CHUNK_P)) - cam_p) * SCENE_POS_UNIT - cam_frac + pos_scale.xyz;
		scale = pos_scale.www;
	}
}

// Unpacks a tangent frame packed by packClusterFrame (smallest-three quaternion rotating (1,0,0) -> tangent, (0,0,1) -> normal).
void unpackClusterFrame(uint packed, out float3 normal, out float3 tangent) {
	const uint largest = packed >> 30;
	const float3 abc = float3((int)(packed << 22) >> 22, (int)(packed << 12) >> 22, (int)(packed << 2) >> 22) * (0.70710678 / 511.0);
	const float d = sqrt(max(0, 1 - dot(abc, abc)));
	float4 q;
	if (largest == 0) q = float4(d, abc);
	else if (largest == 1) q = float4(abc.x, d, abc.y, abc.z);
	else if (largest == 2) q = float4(abc.x, abc.y, d, abc.z);
	else q = float4(abc, d);
	normal = float3(2 * (q.x * q.z + q.w * q.y), 2 * (q.y * q.z - q.w * q.x), 1 - 2 * (q.x * q.x + q.y * q.y));
	tangent = float3(1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y + q.w * q.z), 2 * (q.x * q.z - q.w * q.y));
}

// Counters shared by the threads of a wave. Only the threads that call it take part (the active lanes), so it can be called after divergent returns.
// Single-address appends (one counter for all threads) are not done this way: NVIDIA merges them by itself, wave code for them was measured slower or the same.

// +1 on the counter at `address`, which can be different in each thread. Returns the previous value of the counter for this thread, as InterlockedAdd.
// If all threads have the same address (the usual case, the entries of a wave are mostly of one instance) they share one atomic, otherwise each does its own.
// Not a loop over the distinct addresses (WaveReadLaneFirst(address) == address until all are done): that hung the GPU (TDR) on NVIDIA, though its DXIL was right.
uint waveIncrement(RWByteAddressBuffer buffer, uint address) {
	uint result = 0;
	if (WaveActiveAllEqual(address)) {
		const uint total = WaveActiveCountBits(true);
		uint base = 0;
		if (WaveIsFirstLane()) buffer.InterlockedAdd(address, total, base);
		result = WaveReadLaneFirst(base) + WavePrefixCountBits(true);
	}
	else {
		buffer.InterlockedAdd(address, 1, result);
	}
	return result;
}

#endif
