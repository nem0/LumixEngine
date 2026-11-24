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
#define SELECTED_STRIDE 16				// slot, mesh, cluster, instance

// the lists of work between the passes, 16 bytes each: flat items, tree items (mesh, slot, first cluster, instance) and chunk items (slot, first instance, count, 0)
#define ITEM_STRIDE 16

// u_counter of gpu_scene_cull.hlsl, u32 each
#define COUNTER_TREE_QUEUE 0
#define COUNTER_FLAT 4
#define COUNTER_TREE 8
#define COUNTER_CHUNK 12
#define COUNTER_COUNT 4
// u_args of gpu_scene_cull.hlsl and gpu_scene_variants.hlsl, 16 bytes each
#define ARGS_MODEL_INSTANCES 0
#define ARGS_FLAT 16
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
#define CLUSTER_MESH_TABLE 0			// 4 words: byte offset of the clusters, of the table of groups, cluster count, node count
#define CLUSTER_MESH_CLUSTER_COUNT 8
#define CLUSTER_MESH_ROOT_COUNT 16
#define CLUSTER_MESH_INDEX_STRIDE 20
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
#define CLUSTER_STRIDE 80
#define CLUSTER_BOUNDS 0
#define CLUSTER_LOD_BOUNDS 16
#define CLUSTER_PARENT_BOUNDS 32
#define CLUSTER_MISC 48					// error, parent error, first_index (group-relative), index_count
#define CLUSTER_FIRST_INDEX 56
#define CLUSTER_INDEX_COUNT 60
#define CLUSTER_ATTRIBUTE_ERRORS 64		// of the cluster and of its parent
#define CLUSTER_GROUPS 72				// the group of the cluster, the group of its children
#define CLUSTER_GROUP 72

#define NODE_STRIDE 64
#define NODE_BOUNDS 0
#define NODE_LOD_BOUNDS 16
#define NODE_MISC 32					// min error, max parent error, first child, count and leaf bit
#define NODE_ATTRIBUTE_ERRORS 48		// the smallest and the largest of parents
#define NODE_UNREFINED 56
#define NODE_LEAF_BIT 0x80000000
#define NODE_COUNT_MASK 0x7fffffff

#define GROUP_STRIDE 16
#define GROUP_FLAGS 12
#define GROUP_FLAG_LOADED 1
#define GROUP_PAGE_SHIFT 8				// the page of a group is in the bits above the flags
#define ROOT_STRIDE 4

#define CLUSTER_HEADER_STRIDE 48		// the header of a cluster in u_vertex_buffer, see pullVertex
#define CLUSTER_ALIGNMENT_MASK 15

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
