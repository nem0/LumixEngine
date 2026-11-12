#include "engine/shaders/common.hlsli"

// Selects clusters of meshes (see src/renderer/mesh_clusters.h) for the instances of the GPU scene (src/renderer/gpu_scene.h): its slots and the
// instances of instanced models. The work comes from the culling of instances (gpu_scene_cull.hlsl): an item is (mesh of the mesh table of the scene,
// instance, first cluster, group). The instance is a slot, or, if the group is not 0, the number of an instance in the group (+ 1) of an instanced model. The
// number of items of a dispatch is in a counter of the GPU (u_counts), the groups of the dispatch are computed from it.
//
// A cluster is selected if its error is small enough on the screen and the error of its parent is not. Meshes with few clusters are done by the
// variant FLAT: a group tests 64 consecutive clusters of an instance, one thread each, no synchronizations.
// The clusters of the other meshes are in trees of nodes (MeshNodeGPU), a node has the smallest error and the largest parent error of its
// clusters. One thread group takes one instance and walks the trees from their roots, level by level: threads take nodes of the current level,
// skip a node (and everything under it) if it is outside of the frustum or no cluster of it can be selected at its distance, and add its
// children to the next level. The nodes of a level are in a list in `u_scratch`, there are two lists per group, which are swapped, and there are
// never more nodes in a list than nodes in the mesh. The lists of a group are taken from the scratch buffer with a counter.
//
// The clusters that are selected are appended to a list (u_selected), gpu_scene_variants.hlsl sorts them into draws.

cbuffer UniformData : register(b4) {
	float4 u_planes[6];
	float4 u_cull_offset;	// xyz: the camera of the culling - the camera of the drawing: positions in the tests of the culling are the frozen camera's
	float u_lod_k;			// cluster is accurate enough if error <= u_lod_k * distance
	uint u_is_ortho;
	float u_attribute_weight;	// the error of a cluster is its error of positions + this * its error of attributes
	uint u_tree_depth;		// levels of the deepest tree of the meshes
	uint u_items;			// bindless, 16 bytes items, from u_items_offset
	uint u_items_offset;
	uint u_item_capacity;	// of the list
	uint u_scratch;			// rw bindless, lists of nodes
	uint u_scratch_size;	// u32 in it
	uint u_selected_capacity;	// entries of the list of selected clusters
	uint u_selected;		// rw bindless: [0] selected clusters, [4] scratch u32 allocated, from 16 entries of 16 bytes (instance, mesh, cluster, group)
	uint u_counts;			// bindless, counters of the culling of instances: [4] flat items, [8] tree items
	int3 u_cam_p;			// the camera in the fixed point of the GPU scene, the positions of instances are relative to it
	uint u_padding;
	float3 u_cam_frac;		// -camera_offset of the instances
	uint u_meshes;			// bindless, GPUSceneMesh[], 64 bytes
	uint u_scene_instances;	// bindless, the instances of the GPU scene, 96 bytes
	uint u_groups;			// bindless, GPUSceneGroup[] of the instanced models
	uint u_cluster_data;	// bindless, the buffer with the clusters and nodes of all meshes, see MeshDesc
	uint u_padding1;
	uint u_feedback;		// rw bindless, the feedback of streaming, see FEEDBACK_* (the CPU reads back the lists, they are reset every frame)
	uint u_frame;			// the stamp of this frame
	uint u_feedback_capacity;	// groups in u_feedback, 0 if there is none
	uint u_padding2;
};

// The layout of the feedback buffer, the same as in renderer.cpp (FEEDBACK_*):
//   [0] number of requests (may be more than the capacity, the extra ones are dropped), [4] unused,
//   [8] number of selected clusters in all views of the frame, [12] how many of them did not fit in the list of selected clusters, [16] triangles of the clusters that fit (these are drawn), the counters are only for the profiler,
//   FEEDBACK_REQUESTS: requests, 8 bytes: the id of the group that is needed (its children are not loaded and the cluster is too coarse), the priority (float, larger is more needed),
//   FEEDBACK_BITS: a bit for each id, set if a cluster of the group was selected in the frame (the CPU reads back the ones of the ids that exist),
//   FEEDBACK_STAMPS: 4 bytes for each id, the stamp of the frame in which the id was appended to the requests, so it is not appended twice in a frame (GPU only).
// The CPU resets the counters and the bits every frame.
#define FEEDBACK_REQUEST_CAPACITY 4096
#define FEEDBACK_REQUESTS 32
#define FEEDBACK_BITS (FEEDBACK_REQUESTS + FEEDBACK_REQUEST_CAPACITY * 8)
#define FEEDBACK_MAX_IDS (1 << 20)
#define FEEDBACK_STAMPS (FEEDBACK_BITS + FEEDBACK_MAX_IDS / 8)

// threads of a group, GPUSceneConst::CLUSTER_THREADS
#define GROUP_SIZE 64

// units of positions in the GPU scene, GPUSceneConst::POS_UNIT
#define SCENE_POS_UNIT (1.0 / 1024.0)

struct MeshDesc {
	float4 camera_offset;		// -cam_frac
	uint cluster_groups;		// byte offset in u_cluster_data of the group of each cluster (u32), the clusters are in the payloads of the groups
	uint nodes;					// byte offset in u_cluster_data of MeshNodeGPU[node_count], then the indices of root_count roots
	uint node_count;
	uint root_count;
	uint roots_offset;			// byte offset of the roots in nodes
	uint cluster_count;
	uint mesh_index;			// in the mesh table
	uint groups;				// byte offset in u_cluster_data of the table of groups (MeshGroupAddressGPU, 32 bytes): record address, header address, stream, indices, first cluster, count, flags
};

// the description of a mesh of the mesh table of the GPU scene (GPUSceneMesh), `mesh` is its index in the table
MeshDesc loadSceneDesc(uint mesh) {
	ByteAddressBuffer meshes = bindless_buffers[u_meshes];
	const uint4 a = meshes.Load4(mesh * 64);			// clusters, nodes, cluster count, node count
	const uint2 b = meshes.Load2(mesh * 64 + 16);		// root count, roots offset
	MeshDesc r;
	r.camera_offset = float4(-u_cam_frac, 1);
	r.cluster_groups = a.x; r.nodes = a.y; r.cluster_count = a.z; r.node_count = a.w;
	r.root_count = b.x; r.roots_offset = b.y;
	r.groups = meshes.Load(mesh * 64 + 48);
	r.mesh_index = mesh;
	return r;
}

struct Instance {
	float4 rot;
	float3 pos;
	float3 scale;
};

// `group_ref` is 0 if the instance is a slot of the GPU scene, otherwise the group of an instanced model (+ 1) and `index` is the number of the instance in it
Instance loadInstance(MeshDesc desc, uint index, uint group_ref) {
	Instance r;
	if (group_ref == 0) {
		// GPU scene: 96 bytes per slot (rot, int3 p, mesh id, scale, ...), positions in fixed point. The difference to the camera is exact,
		// camera_offset (added below) is -cam_frac.
		ByteAddressBuffer instances = bindless_buffers[u_scene_instances];
		const uint offset = index * 96;
		r.rot = asfloat(instances.Load4(offset));
		r.pos = float3(asint(instances.Load3(offset + 16)) - u_cam_p) * SCENE_POS_UNIT;
		r.scale = asfloat(instances.Load3(offset + 32));
	}
	else {
		// an instance of an instanced model: 32 bytes in the buffer of its group, relative to the origin of the group, which is in fixed point
		ByteAddressBuffer groups = bindless_buffers[u_groups];
		const uint4 origin = groups.Load4((group_ref - 1) * 32);
		ByteAddressBuffer data = bindless_buffers[groups.Load((group_ref - 1) * 32 + 16)];
		const float4 rot_lod = asfloat(data.Load4(index * 32));
		const float4 pos_scale = asfloat(data.Load4(index * 32 + 16));
		r.rot = float4(rot_lod.xyz, sqrt(saturate(1 - dot(rot_lod.xyz, rot_lod.xyz))));
		r.pos = float3(asint(origin.xyz) - u_cam_p) * SCENE_POS_UNIT + pos_scale.xyz;
		r.scale = pos_scale.www;
	}
	r.pos += desc.camera_offset.xyz;
	return r;
}

bool isErrorSmall(float error, float3 center, float radius) {
	float dist = u_is_ortho ? 1 : max(length(center) - radius, 1e-4);
	// clusters without a parent have FLT_MAX error
	return error < 1e30 && error <= u_lod_k * dist;
}

bool insideFrustum(float3 center, float radius) {
	for (int i = 0; i < 6; ++i) {
		if (dot(u_planes[i], float4(center, 1)) < -radius) return false;
	}
	return true;
}

// Tests a cluster and appends it to the list of selected clusters if it is selected.
void processCluster(MeshDesc desc, Instance instance, float max_scale, uint instance_idx, uint cluster, uint group_ref) {
	ByteAddressBuffer clusters = bindless_buffers[u_cluster_data];
	// Streaming: the record of a cluster is in the payload of its group, which is not loaded if the flags say so (then the clusters of the group are not drawn)
	const uint group = clusters.Load(desc.cluster_groups + cluster * 4);
	const uint4 group_address = clusters.Load4(desc.groups + group * 32 + 16);	// first cluster, cluster count, flags
	const uint own_flags = group_address.z;
	if ((own_flags & 1) == 0) return;
	uint offset = clusters.Load(desc.groups + group * 32) + (cluster - group_address.x) * 80;
	float4 bounds = asfloat(clusters.Load4(offset));
	float4 lod_bounds = asfloat(clusters.Load4(offset + 16));
	float4 parent_bounds = asfloat(clusters.Load4(offset + 32));
	uint4 misc = clusters.Load4(offset + 48);
	const float2 attribute_errors = asfloat(clusters.Load2(offset + 64)); // of the cluster and of its parent
	// A cluster whose children (the group it was refined to) are not loaded is drawn as if it was accurate enough, if its parent is not drawn instead. The parent decides
	// the same for all clusters of the group, so there are no holes and no overlaps.
	const uint refined_group = clusters.Load(offset + 76);	// the group of its children
	const uint child_flags = refined_group == 0xffffffffu ? 1u : clusters.Load(desc.groups + refined_group * 32 + 24);
	const bool refined = (child_flags & 1) != 0;
	// the error of a cluster is the one of its positions and the one of its attributes, which counts as much as the weight says
	float error = (asfloat(misc.x) + u_attribute_weight * attribute_errors.x) * max_scale;
	// clusters without a parent have FLT_MAX error
	float parent_error = asfloat(misc.y) < 1e30 ? (asfloat(misc.y) + u_attribute_weight * attribute_errors.y) * max_scale : asfloat(misc.y);

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
	if (!insideFrustum(center, radius)) return;

	// the feedback of streaming: the group is used, and if the cluster is only drawn because its children are not loaded and it is not accurate enough, they are needed
	if (u_feedback_capacity != 0) {
		RWByteAddressBuffer feedback = bindless_rw_buffers[u_feedback];
		// a bit for the used group, a request is appended once per frame (the stamps after FEEDBACK_STAMPS tell if it was), the CPU reads them back
		const uint own_id = own_flags >> 8;
		if (own_id < u_feedback_capacity) {
			uint unused;
			feedback.InterlockedOr(FEEDBACK_BITS + (own_id >> 5) * 4, 1u << (own_id & 31), unused);
		}
		const uint child_id = child_flags >> 8;
		if (!refined && !error_small && child_id < u_feedback_capacity) {
			uint old;
			feedback.InterlockedExchange(FEEDBACK_STAMPS + child_id * 4, u_frame, old);
			if (old != u_frame) {
				uint at;
				feedback.InterlockedAdd(0, 1, at);
				// how many times too coarse the cluster is, the CPU loads the most needed pages first
				const float dist = u_is_ortho ? 1 : max(length(lod_center) - lod_bounds.w * max_scale, 1e-4);
				const float priority = min(error / (u_lod_k * dist), 1e30);
				if (at < FEEDBACK_REQUEST_CAPACITY) feedback.Store2(FEEDBACK_REQUESTS + at * 8, uint2(child_id, asuint(priority)));
			}
		}
	}

	RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
	uint at;
	selected.InterlockedAdd(0, 1, at);
	if (at < u_selected_capacity) selected.Store4(16 + at * 16, uint4(instance_idx, desc.mesh_index, cluster, group_ref));
	// counted for the profiler, in all views of the frame
	if (u_feedback_capacity != 0) {
		RWByteAddressBuffer feedback = bindless_rw_buffers[u_feedback];
		uint unused;
		feedback.InterlockedAdd(8, 1, unused);
		if (at >= u_selected_capacity) feedback.InterlockedAdd(12, 1, unused);
		else feedback.InterlockedAdd(16, misc.w / 3, unused);
	}
}

// false if no cluster of the node can be selected, then the node and everything under it is skipped
bool isNodeNeeded(float4 bounds, float4 lod_bounds, float min_error, float max_parent_error, float min_attribute_error, float max_parent_attribute_error, bool unrefined, Instance instance, float max_scale) {
	const float3 cull_pos = instance.pos + u_cull_offset.xyz;
	float3 center = cull_pos + rotateByQuat(instance.rot, bounds.xyz * instance.scale);

	// The distance of a cluster is measured from the sphere of its lod bounds (or the one of its parent), which is inside the lod sphere of the node:
	// it is not nearer than the nearest point of the node sphere and not farther than the farthest one.
	float3 lod_center = cull_pos + rotateByQuat(instance.rot, lod_bounds.xyz * instance.scale);
	float lod_radius = lod_bounds.w * max_scale;
	float to_center = length(lod_center);
	float near_dist = u_is_ortho ? 1 : max(to_center - lod_radius, 1e-4);
	float far_dist = u_is_ortho ? 1 : to_center + lod_radius;
	bool needed = insideFrustum(center, bounds.w * max_scale);
	// none of the clusters is accurate enough (the smallest error of positions and the smallest of attributes are not of the same cluster, the sum is a lower bound)
	// (not if a cluster of the node has no loaded children: it is drawn however coarse it is)
	if (!unrefined && (min_error + u_attribute_weight * min_attribute_error) * max_scale > u_lod_k * far_dist) needed = false;
	// the parents of all the clusters are accurate enough, they are drawn instead (clusters without a parent have FLT_MAX); the sum of the largest ones is an upper bound
	if (max_parent_error < 1e30 && (max_parent_error + u_attribute_weight * max_parent_attribute_error) * max_scale <= u_lod_k * near_dist) needed = false;
	return needed;
}

groupshared uint g_current_count;
groupshared uint g_next_count;
groupshared uint g_scratch_base;

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID) {
	const uint group_index = group_id.y * 65535 + group_id.x;
	// the number of items is in a counter of the GPU, u_item_capacity is the capacity of the list
	#ifdef FLAT
		const uint item_count = min(bindless_buffers[u_counts].Load(4), u_item_capacity);
	#else
		const uint item_count = min(bindless_buffers[u_counts].Load(8), u_item_capacity);
	#endif
	if (group_index >= item_count) return; // the same for all threads of the group
	const uint4 item = bindless_buffers[u_items].Load4(u_items_offset + group_index * 16); // mesh, instance, first cluster, group
	const MeshDesc desc = loadSceneDesc(item.x);
	const uint instance_idx = item.y;
	Instance instance = loadInstance(desc, instance_idx, item.w);
	// bounds are spheres, so non-uniform scale is handled by the largest axis
	float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));

	#ifdef FLAT
		// 64 consecutive clusters of an instance
		const uint cluster = item.z + group_thread_id.x;
		if (cluster < desc.cluster_count) processCluster(desc, instance, max_scale, instance_idx, cluster, item.w);
	#else
		ByteAddressBuffer nodes = bindless_buffers[u_cluster_data];
		RWByteAddressBuffer scratch = bindless_rw_buffers[u_scratch];

		// the lists of nodes of the group are allocated from the scratch buffer
		if (group_thread_id.x == 0) {
			uint allocated;
			bindless_rw_buffers[u_selected].InterlockedAdd(4, desc.node_count * 2, allocated);
			g_scratch_base = allocated;
		}
		GroupMemoryBarrierWithGroupSync();
		if (g_scratch_base + desc.node_count * 2 > u_scratch_size) return; // does not fit, the same for all threads of the group
		const uint scratch_base = g_scratch_base * 4;
		const uint list_size = desc.node_count * 4;

		// the roots are the first nodes to visit
		for (uint r = group_thread_id.x; r < desc.root_count; r += GROUP_SIZE) {
			scratch.Store(scratch_base + r * 4, nodes.Load(desc.nodes + desc.roots_offset + r * 4));
		}
		if (group_thread_id.x == 0) {
			g_current_count = desc.root_count;
			g_next_count = 0;
		}
		DeviceMemoryBarrierWithGroupSync();

		// A loop with the depth of the deepest tree of the meshes of the dispatch, a uniform value, so that the synchronizations are in flow
		// control that is the same for all threads (the shader compiler can not tell that a loop that ends when a value in groupshared memory is
		// 0 ends for all threads at the same time). Levels after the last one of a shallower tree have no nodes, they cost two synchronizations.
		uint list = 0;
		[loop] for (uint depth = 0; depth < u_tree_depth; ++depth) {
			const uint count = g_current_count; // all threads read it between the same two synchronizations
			const uint from = scratch_base + list * list_size;
			const uint to = scratch_base + (1 - list) * list_size;

			for (uint i = group_thread_id.x; i < count; i += GROUP_SIZE) {
				const uint node_id = scratch.Load(from + i * 4);
				const uint node_offset = desc.nodes + node_id * 64;
				const float4 bounds = asfloat(nodes.Load4(node_offset));
				const float4 lod_bounds = asfloat(nodes.Load4(node_offset + 16));
				const uint4 misc = nodes.Load4(node_offset + 32); // min error, max parent error, first, count and flags
				const float2 attribute_errors = asfloat(nodes.Load2(node_offset + 48)); // the smallest and the largest of parents
				if (!isNodeNeeded(bounds, lod_bounds, asfloat(misc.x), asfloat(misc.y), attribute_errors.x, attribute_errors.y, nodes.Load(node_offset + 56) != 0, instance, max_scale)) continue;

				const uint child_count = misc.w & 0x7fffffffu;
				if (misc.w & 0x80000000u) {
					// a leaf: clusters
					for (uint c = 0; c < child_count; ++c) {
						processCluster(desc, instance, max_scale, instance_idx, misc.z + c, item.w);
					}
				}
				else {
					uint at;
					InterlockedAdd(g_next_count, child_count, at);
					for (uint c = 0; c < child_count; ++c) scratch.Store(to + (at + c) * 4, misc.z + c);
				}
			}

			DeviceMemoryBarrierWithGroupSync(); // the nodes of the next level are written
			if (group_thread_id.x == 0) {
				g_current_count = g_next_count;
				g_next_count = 0;
			}
			GroupMemoryBarrierWithGroupSync();
			list = 1 - list;
		}
	#endif
}
