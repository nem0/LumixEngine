#include "engine/shaders/common.hlsli"
#include "engine/shaders/gpu_scene_layout.hlsli"
#include "engine/shaders/hzb_common.hlsli"

// Selects clusters of meshes (see src/renderer/mesh_clusters.h) for the model instances of the GPU scene (src/renderer/gpu_scene.h) and the instances of its chunks of
// instanced models. The work comes from the culling of model instances (gpu_scene_cull.hlsl): an item is (mesh of the mesh table, slot, first cluster, instance). The
// instance is 0 for a model instance that is drawn itself, otherwise the index (+ 1) of an instance in the buffer of the instances of the model instance. The
// number of items of a dispatch is in a counter of the GPU (u_counts), the groups of the dispatch are computed from it.
//
// A cluster is selected if its error is small enough on the screen and the error of its parent is not.
// Clusters are organized in trees of nodes (MeshNodeGPU); a node stores the smallest error and the largest parent error of its clusters.
// Tree traversal is breadth-first over global (instance, node) queues in `u_scratch`:
//   TREE_INIT:     1 thread per (instance, mesh) item in u_tree_items; tests its roots and appends child (item, node) pairs to node queue 0
//                  and leaf clusters to the candidate cluster queue.
//   TREE_ARGS:     computes indirect dispatch groups for node queue 0/1 (u_node_pass = 0/1, clearing the other) or candidate clusters (u_node_pass = 2).
//   TREE_STEP:     1 thread per (item, node) in the current node queue; tests the node and appends internal children to the next node queue
//                  or leaf clusters to the candidate cluster queue.
//   CLUSTER_CULL:  1 thread per candidate (item, cluster); runs processCluster.
//   POST_ARGS:     prepares indirect dispatch args for POST_OCCLUDED and resets u_selected / u_voxel_clusters.
//   POST_OCCLUDED: 1 thread per pass-1 occluded cluster; re-tests against current-frame HZB.
//
// The clusters that are selected are appended to a list (u_selected), gpu_scene_variants.hlsl sorts them into draws.

cbuffer UniformData : register(b4) {
	float4 u_planes[6];
	float4 u_cull_offset;		// xyz: the camera of the culling - the camera of the drawing: positions in the tests of the culling are the frozen camera's
	float u_lod_k;				// cluster is accurate enough if error <= u_lod_k * distance
	uint u_is_ortho;
	uint u_items;				// bindless, 16 bytes items
	uint u_item_capacity;		// of the list
	uint u_scratch;				// rw bindless, global ping-pong (item, node) queues + candidate (item, cluster) queue
	uint u_scratch_size;		// u32 in u_scratch
	uint u_selected_capacity;	// entries of the list of selected clusters
	uint u_selected;			// rw bindless: [0] selected clusters, from 16 entries of 16 bytes (instance, mesh, cluster, group)
	int3 u_cam_p;				// the camera in the fixed point of the GPU scene, the positions of instances are relative to it
	uint u_node_pass;			// 0 or 1: which ping-pong node queue is read in TREE_ARGS / TREE_STEP
	float3 u_cam_frac;			// -camera_offset of the instances
	uint u_meshes;				// bindless, GPUClusterMesh[], 32 bytes
	uint u_instances;			// bindless, the model instances of the GPU scene, 96 bytes
	uint u_chunks;				// bindless, the chunks of instanced models (GPUSceneChunk, 32 bytes)
	uint u_cluster_data;		// bindless, the buffer with the clusters and nodes of all meshes, see MeshDesc
	uint u_node_queue_capacity;	// max uint2 entries in each of the two ping-pong node queues in u_scratch
	uint u_counts;				// rw bindless, counters of gpu_scene_cull.hlsl and tree/cluster queues
	uint u_feedback;			// rw bindless, one bit for each page of the meshes that stream, the CPU clears the bits after reading them
	uint u_occluded;			// rw bindless: [0] occluded clusters from pass 1, from 16 entries of 16 bytes (instance, mesh, cluster | flags, instance_ref)
	uint u_feedback_capacity;	// groups in u_feedback, 0 if there is none
	uint u_occluded_capacity;	// entries of u_occluded
	uint u_args;				// rw bindless, indirect dispatch args (POST_ARGS / TREE_ARGS)
	uint u_voxel_clusters;		// rw bindless, list of selected voxel clusters (index_count == 0)
	uint u_padding0;
	HZBUniforms u_hzb;
};

// threads of a group, GPUSceneConst::CLUSTER_THREADS
#define GROUP_SIZE 64

struct MeshDesc {
	uint clusters;				// byte offset in u_cluster_data of MeshClusterGPU[]
	uint nodes;					// byte offset in u_cluster_data of MeshNodeGPU[node_count], then the indices of root_count roots
	uint node_count;
	uint root_count;
	uint roots_offset;			// byte offset of the roots in nodes
	uint cluster_count;
	uint mesh_index;			// in the mesh table
	uint groups;				// byte offset in u_cluster_data of the table of groups (MeshGroupAddressGPU, 16 bytes), the flags are the last word
};

// the description of a mesh of the mesh table of the GPU scene (GPUClusterMesh), `mesh` is its index in the table. The nodes follow the clusters (72 bytes each),
// the roots follow the nodes (52 bytes each), see gpu_cluster_models.h
MeshDesc loadSceneDesc(uint mesh) {
	ByteAddressBuffer meshes = bindless_buffers[u_meshes];
	const ClusterMesh cm = meshes.Load<ClusterMesh>(mesh * CLUSTER_MESH_STRIDE);
	MeshDesc r;
	r.clusters = cm.clusters; r.nodes = cm.clusters + cm.cluster_count * CLUSTER_STRIDE; r.cluster_count = cm.cluster_count; r.node_count = cm.node_count;
	r.root_count = cm.root_count; r.roots_offset = cm.node_count * NODE_STRIDE;
	r.groups = cm.group_table;
	r.mesh_index = mesh;
	return r;
}

struct Instance {
	float4 rot;
	float3 pos;
	float3 scale;
};

// `slot` is the model instance, `instance` is 0 if the model instance is drawn itself, otherwise the instance + 1 in the buffer of the instances of the chunk, which is `slot`
Instance loadInstance(uint slot, uint instance) {
	Instance r;
	loadSceneTransform(u_instances, u_chunks, u_cam_p, u_cam_frac, slot, instance, r.rot, r.pos, r.scale);
	return r;
}

bool isErrorSmall(float error, float3 center, float radius) {
	float dist = u_is_ortho ? 1 : max(length(center) - radius, 1e-4);
	// clusters without a parent have FLT_MAX error
	return error < 1e30 && error <= u_lod_k * dist;
}

// Sets the bit of a group in the feedback. Most of the clusters of a frame find it set already, so it is read first and the atomic is only done if it is needed.
void markFeedback(RWByteAddressBuffer feedback, uint id) {
	const uint address = (id >> 5) * 4;
	const uint bit = 1u << (id & 31);
	if ((feedback.Load(address) & bit) == 0) feedback.InterlockedOr(address, bit);
}

// Coarse logarithmic front-to-back distance bin [0 .. 31] from camera-relative cluster center (packed into bits 27..31 of selected.z).
uint computeDepthBin(float3 center) {
	const float dist2 = dot(center, center);
	const float b = (log2(max(dist2, 0.25)) + 2.0) * (31.0 / 18.0);
	return (uint)clamp(b, 0.0, 31.0);
}

// The feedback of streaming: the group is used, and if the cluster is only drawn because its children are not loaded and it is not accurate enough, they are needed.
// The ids in the groups are the ones of their pages. The page is loaded in the first case and it is not in the second, so one bit is enough.
void reportStreamFeedback(uint own_flags, uint child_flags, bool error_small) {
	if (u_feedback_capacity == 0) return;
	RWByteAddressBuffer feedback = bindless_rw_buffers[u_feedback];
	const uint own_id = own_flags >> GROUP_PAGE_SHIFT;
	if (own_id < u_feedback_capacity) markFeedback(feedback, own_id);
	const bool refined = (child_flags & GROUP_FLAG_LOADED) != 0;
	const uint child_id = child_flags >> GROUP_PAGE_SHIFT;
	if (!refined && !error_small && child_id < u_feedback_capacity) markFeedback(feedback, child_id);
}

void appendSelectedCluster(bool is_voxel, uint instance_idx, uint mesh_index, uint cluster, float3 center, uint instance_ref) {
	const uint packed = cluster | (computeDepthBin(center) << SELECTED_DEPTH_BIN_SHIFT);
	if (is_voxel && u_voxel_clusters != 0) {
		RWByteAddressBuffer dst = bindless_rw_buffers[u_voxel_clusters];
		uint at;
		dst.InterlockedAdd(SELECTED_COUNT, 1, at);
		if (at < u_selected_capacity) dst.Store4(SELECTED_HEADER + at * SELECTED_STRIDE, uint4(instance_idx, mesh_index, packed, instance_ref));
	}
	else {
		RWByteAddressBuffer dst = bindless_rw_buffers[u_selected];
		uint at;
		dst.InterlockedAdd(SELECTED_COUNT, 1, at);
		if (at < u_selected_capacity) dst.Store4(SELECTED_HEADER + at * SELECTED_STRIDE, uint4(instance_idx, mesh_index, packed, instance_ref));
	}
}

// Tests a cluster and appends it to the list of selected clusters if it is selected.
void processCluster(MeshDesc desc, Instance instance, float max_scale, uint instance_idx, uint cluster, uint instance_ref) {
	ByteAddressBuffer clusters = bindless_buffers[u_cluster_data];
	uint offset = desc.clusters + cluster * CLUSTER_STRIDE;
	const MeshCluster cl = clusters.Load<MeshCluster>(offset);
	const float4 bounds = cl.bounds;
	const float4 lod_bounds = cl.lod_bounds;
	const float4 parent_bounds = cl.parent_bounds;
	// Streaming: the clusters of a group that is not loaded are not drawn. A cluster whose children (the group it was refined to) are not loaded is drawn as if it was
	// accurate enough, if its parent is not drawn instead. The parent decides the same for all clusters of the group, so there are no holes and no overlaps.
	const uint own_flags = clusters.Load(desc.groups + cl.group * GROUP_STRIDE + GROUP_FLAGS);
	if ((own_flags & GROUP_FLAG_LOADED) == 0) return;
	const uint child_flags = cl.refined_group == NO_GROUP ? GROUP_FLAG_LOADED : clusters.Load(desc.groups + cl.refined_group * GROUP_STRIDE + GROUP_FLAGS);
	const bool refined = (child_flags & GROUP_FLAG_LOADED) != 0;
	float error = cl.error * max_scale;
	// clusters without a parent have FLT_MAX error
	float parent_error = cl.parent_error < 1e30 ? cl.parent_error * max_scale : cl.parent_error;

	// the tests are done from the camera of the culling (the same as the one of the drawing, if it is not frozen), the instance is written as it is
	const float3 cull_pos = instance.pos + u_cull_offset.xyz;

	// the cluster is drawn if it is accurate enough and its parent is not
	float3 lod_center = cull_pos + rotateByQuat(instance.rot, lod_bounds.xyz * instance.scale);
	float3 parent_center = cull_pos + rotateByQuat(instance.rot, parent_bounds.xyz * instance.scale);
	const bool error_small = isErrorSmall(error, lod_center, lod_bounds.w * max_scale);
	if (refined && !error_small) return;
	if (isErrorSmall(parent_error, parent_center, parent_bounds.w * max_scale)) return;

	// frustum culling
	float3 center = cull_pos + rotateByQuat(instance.rot, bounds.xyz * instance.scale);
	float radius = bounds.w * max_scale;
	if (!insideFrustum(u_planes, center, radius)) return;

	const bool is_voxel = (cl.index_count & CLUSTER_INDEX_COUNT_MASK) == 0;

	// Occlusion culling in two passes. The first pass appends clusters hidden by the previous frame to u_occluded instead of drawing them;
	// the second pass (POST_OCCLUDED) re-tests only u_occluded against the depth pyramid of the first pass, avoiding a full DAG re-traversal.
	if (u_hzb.mode == 1) {
		if (isOccludedHZB(u_hzb, center, radius, true)) {
			RWByteAddressBuffer occluded = bindless_rw_buffers[u_occluded];
			uint at;
			occluded.InterlockedAdd(SELECTED_COUNT, 1, at);
			if (at < u_occluded_capacity) {
				occluded.Store4(SELECTED_HEADER + at * SELECTED_STRIDE, uint4(instance_idx, desc.mesh_index, cluster | (error_small ? OCCLUDED_ERROR_SMALL_BIT : 0u) | (is_voxel ? OCCLUDED_VOXEL_BIT : 0u), instance_ref));
			}
			return;
		}
	}

	reportStreamFeedback(own_flags, child_flags, error_small);
	appendSelectedCluster(is_voxel, instance_idx, desc.mesh_index, cluster, center, instance_ref);
}

// false if no cluster of the node can be selected, then the node and everything under it is skipped
bool isNodeNeeded(MeshNode node, Instance instance, float max_scale) {
	const float4 bounds = node.bounds;
	const float4 lod_bounds = node.lod_bounds;
	const bool unrefined = node.unrefined != 0;
	const float3 cull_pos = instance.pos + u_cull_offset.xyz;
	float3 center = cull_pos + rotateByQuat(instance.rot, bounds.xyz * instance.scale);

	// The distance of a cluster is measured from the sphere of its lod bounds (or the one of its parent), which is inside the lod sphere of the node:
	// it is not nearer than the nearest point of the node sphere and not farther than the farthest one.
	float3 lod_center = cull_pos + rotateByQuat(instance.rot, lod_bounds.xyz * instance.scale);
	float lod_radius = lod_bounds.w * max_scale;
	float to_center = length(lod_center);
	float near_dist = u_is_ortho ? 1 : max(to_center - lod_radius, 1e-4);
	float far_dist = u_is_ortho ? 1 : to_center + lod_radius;
	bool needed = insideFrustum(u_planes, center, bounds.w * max_scale);
	// none of the clusters is accurate enough (not if a cluster of the node has no loaded children: it is drawn however coarse it is)
	if (!unrefined && node.min_error * max_scale > u_lod_k * far_dist) needed = false;
	// the parents of all the clusters are accurate enough, they are drawn instead (clusters without a parent have FLT_MAX)
	if (node.max_parent_error < 1e30 && node.max_parent_error * max_scale <= u_lod_k * near_dist) needed = false;
	return needed;
}

uint waveAppend(RWByteAddressBuffer counts, uint counter_offset, uint count) {
	const uint total = WaveActiveSum(count);
	uint base = 0;
	if (WaveIsFirstLane() && total > 0) counts.InterlockedAdd(counter_offset, total, base);
	return WaveReadLaneFirst(base) + WavePrefixSum(count);
}

void emitNodeChildren(RWByteAddressBuffer counts, RWByteAddressBuffer scratch, MeshNode node, uint tree_item_idx, uint next_counter, uint next_base) {
	const uint child_count = node.count_and_flags & NODE_COUNT_MASK;
	if (node.count_and_flags & NODE_LEAF_BIT) {
		const uint cluster_cap = u_scratch_size / 2 - 2 * u_node_queue_capacity;
		const uint cluster_base = u_node_queue_capacity * 16;
		const uint at = waveAppend(counts, COUNTER_CANDIDATE_CLUSTERS, child_count);
		for (uint c = 0; c < child_count; ++c) {
			if (at + c < cluster_cap) scratch.Store2(cluster_base + (at + c) * 8, uint2(tree_item_idx, node.first + c));
		}
	}
	else {
		const uint at = waveAppend(counts, next_counter, child_count);
		for (uint c = 0; c < child_count; ++c) {
			if (at + c < u_node_queue_capacity) scratch.Store2(next_base + (at + c) * 8, uint2(tree_item_idx, node.first + c));
		}
	}
}

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID) {
	#if defined POST_ARGS
		if (group_thread_id.x == 0) {
			bindless_rw_buffers[u_selected].Store4(SELECTED_COUNT, uint4(0, 0, 0, 0));
			if (u_voxel_clusters != 0) bindless_rw_buffers[u_voxel_clusters].Store4(SELECTED_COUNT, uint4(0, 0, 0, 0));
			const uint count = min(bindless_rw_buffers[u_occluded].Load(SELECTED_COUNT), u_occluded_capacity);
			const uint groups = (count + GROUP_SIZE - 1) / GROUP_SIZE;
			bindless_rw_buffers[u_args].Store3(ARGS_POST, dispatchGroups(groups));
		}
	#elif defined POST_OCCLUDED
		const uint idx = (group_id.y * MAX_GROUPS_X + group_id.x) * GROUP_SIZE + group_thread_id.x;
		RWByteAddressBuffer occluded = bindless_rw_buffers[u_occluded];
		const uint count = min(occluded.Load(SELECTED_COUNT), u_occluded_capacity);
		if (idx >= count) return;
		const uint4 entry = occluded.Load4(SELECTED_HEADER + idx * SELECTED_STRIDE);
		const uint instance_idx = entry.x;
		const uint mesh_index = entry.y;
		const uint cluster = entry.z & OCCLUDED_CLUSTER_MASK;
		const bool is_voxel = (entry.z & OCCLUDED_VOXEL_BIT) != 0;
		const bool error_small = (entry.z & OCCLUDED_ERROR_SMALL_BIT) != 0;
		const uint instance_ref = entry.w;

		const MeshDesc desc = loadSceneDesc(mesh_index);
		const Instance instance = loadInstance(instance_idx, instance_ref);
		const float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));

		ByteAddressBuffer clusters = bindless_buffers[u_cluster_data];
		const uint offset = desc.clusters + cluster * CLUSTER_STRIDE;
		const float4 bounds = asfloat(clusters.Load4(offset + CLUSTER_BOUNDS));
		const float3 cull_pos = instance.pos + u_cull_offset.xyz;
		const float3 center = cull_pos + rotateByQuat(instance.rot, bounds.xyz * instance.scale);
		const float radius = bounds.w * max_scale;
		if (isOccludedHZB(u_hzb, center, radius, false)) return;

		if (u_feedback_capacity != 0) {
			const uint2 group_info = clusters.Load2(offset + CLUSTER_GROUPS);
			const uint own_flags = clusters.Load(desc.groups + group_info.x * GROUP_STRIDE + GROUP_FLAGS);
			const uint child_flags = group_info.y == NO_GROUP ? GROUP_FLAG_LOADED : clusters.Load(desc.groups + group_info.y * GROUP_STRIDE + GROUP_FLAGS);
			reportStreamFeedback(own_flags, child_flags, error_small);
		}
		appendSelectedCluster(is_voxel, instance_idx, mesh_index, cluster, center, instance_ref);
	#elif defined CLUSTER_CULL
		// Candidate cluster culling: 1 thread per (item, cluster)
		const uint idx = (group_id.y * MAX_GROUPS_X + group_id.x) * GROUP_SIZE + group_thread_id.x;
		RWByteAddressBuffer counts = bindless_rw_buffers[u_counts];
		const uint cluster_cap = u_scratch_size / 2 - 2 * u_node_queue_capacity;
		const uint count = min(counts.Load(COUNTER_CANDIDATE_CLUSTERS), cluster_cap);
		if (idx >= count) return;

		const uint cluster_base = u_node_queue_capacity * 16;
		const uint2 entry = bindless_rw_buffers[u_scratch].Load2(cluster_base + idx * 8); // tree_item_idx, cluster_id
		const uint4 item = bindless_buffers[u_items].Load4(entry.x * ITEM_STRIDE); // mesh, slot, 0, instance
		const MeshDesc desc = loadSceneDesc(item.x);
		const Instance instance = loadInstance(item.y, item.w);
		const float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));
		processCluster(desc, instance, max_scale, item.y, entry.y, item.w);
	#elif defined TREE_ARGS
		if (group_thread_id.x == 0) {
			RWByteAddressBuffer counts = bindless_rw_buffers[u_counts];
			if (u_node_pass == 2) {
				const uint cluster_cap = u_scratch_size / 2 - 2 * u_node_queue_capacity;
				const uint count = min(counts.Load(COUNTER_CANDIDATE_CLUSTERS), cluster_cap);
				const uint groups = (count + GROUP_SIZE - 1) / GROUP_SIZE;
				bindless_rw_buffers[u_args].Store3(ARGS_TREE, dispatchGroups(groups));
			}
			else {
				const uint cur_counter = u_node_pass == 0 ? COUNTER_NODE_0 : COUNTER_NODE_1;
				const uint next_counter = u_node_pass == 0 ? COUNTER_NODE_1 : COUNTER_NODE_0;
				const uint count = min(counts.Load(cur_counter), u_node_queue_capacity);
				const uint groups = (count + GROUP_SIZE - 1) / GROUP_SIZE;
				bindless_rw_buffers[u_args].Store3(ARGS_TREE, dispatchGroups(groups));
				counts.Store(next_counter, 0);
			}
		}
	#elif defined TREE_STEP
		// BFS node step: 1 thread per (item, node)
		const uint idx = (group_id.y * MAX_GROUPS_X + group_id.x) * GROUP_SIZE + group_thread_id.x;
		RWByteAddressBuffer counts = bindless_rw_buffers[u_counts];
		const uint cur_counter = u_node_pass == 0 ? COUNTER_NODE_0 : COUNTER_NODE_1;
		const uint count = min(counts.Load(cur_counter), u_node_queue_capacity);
		if (idx >= count) return;

		RWByteAddressBuffer scratch = bindless_rw_buffers[u_scratch];
		const uint cur_base = u_node_pass * u_node_queue_capacity * 8;
		const uint2 entry = scratch.Load2(cur_base + idx * 8); // tree_item_idx, node_id
		const uint tree_item_idx = entry.x;
		const uint node_id = entry.y;

		const uint4 item = bindless_buffers[u_items].Load4(tree_item_idx * ITEM_STRIDE); // mesh, slot, 0, instance
		const MeshDesc desc = loadSceneDesc(item.x);
		const Instance instance = loadInstance(item.y, item.w);
		const float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));

		ByteAddressBuffer nodes = bindless_buffers[u_cluster_data];
		const MeshNode node = nodes.Load<MeshNode>(desc.nodes + node_id * NODE_STRIDE);
		if (!isNodeNeeded(node, instance, max_scale)) return;

		const uint next_counter = u_node_pass == 0 ? COUNTER_NODE_1 : COUNTER_NODE_0;
		const uint next_base = (1 - u_node_pass) * u_node_queue_capacity * 8;
		emitNodeChildren(counts, scratch, node, tree_item_idx, next_counter, next_base);
	#else // TREE_INIT
		// TREE_INIT: 1 thread per (instance, mesh) item; tests roots and emits child (item, node) pairs to node queue 0 or leaf clusters to the cluster queue
		const uint tree_item_idx = (group_id.y * MAX_GROUPS_X + group_id.x) * GROUP_SIZE + group_thread_id.x;
		RWByteAddressBuffer counts = bindless_rw_buffers[u_counts];
		const uint item_count = min(counts.Load(COUNTER_TREE), u_item_capacity);
		if (tree_item_idx >= item_count) return;

		const uint4 item = bindless_buffers[u_items].Load4(tree_item_idx * ITEM_STRIDE); // mesh, slot, 0, instance
		const MeshDesc desc = loadSceneDesc(item.x);
		const Instance instance = loadInstance(item.y, item.w);
		const float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));

		ByteAddressBuffer nodes = bindless_buffers[u_cluster_data];
		RWByteAddressBuffer scratch = bindless_rw_buffers[u_scratch];

		for (uint r = 0; r < desc.root_count; ++r) {
			const uint node_id = nodes.Load(desc.nodes + desc.roots_offset + r * ROOT_STRIDE);
			const MeshNode node = nodes.Load<MeshNode>(desc.nodes + node_id * NODE_STRIDE);
			if (!isNodeNeeded(node, instance, max_scale)) continue;
			emitNodeChildren(counts, scratch, node, tree_item_idx, COUNTER_NODE_0, 0);
		}
	#endif
}
