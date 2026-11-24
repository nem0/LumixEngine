#include "engine/shaders/common.hlsli"
#include "engine/shaders/gpu_scene_layout.hlsli"

// Selects clusters of meshes (see src/renderer/mesh_clusters.h) for the model instances of the GPU scene (src/renderer/gpu_scene.h) and the instances of its chunks of
// instanced models. The work comes from the culling of model instances (gpu_scene_cull.hlsl): an item is (mesh of the mesh table, slot, first cluster, instance). The
// instance is 0 for a model instance that is drawn itself, otherwise the index (+ 1) of an instance in the buffer of the instances of the model instance. The
// number of items of a dispatch is in a counter of the GPU (u_counts), the groups of the dispatch are computed from it.
//
// A cluster is selected if its error is small enough on the screen and the error of its parent is not. Meshes with few clusters are done by the
// variant FLAT: a group tests 64 consecutive clusters of an instance, one thread each, no synchronizations.
// The clusters of the other meshes are in trees of nodes (MeshNodeGPU), a node has the smallest error and the largest parent error of its
// clusters. One thread group takes one instance and walks the trees from their roots, level by level: threads take nodes of the current level,
// skip a node (and everything under it) if it is outside of the frustum or no cluster of it can be selected at its distance, and add its
// children to the next level. The nodes of a level are in a list in `u_scratch`, there are two lists per group, which are swapped, and there are
// never more nodes in a list than nodes in the mesh. A dispatch has a fixed number of groups (u_tree_groups), each takes many items one after another and has its own
// slice of the scratch buffer for the lists.
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
	uint u_scratch_size;	// u32 in it, the slices of the groups (u_tree_groups of them, u_scratch_slice each) are inside it
	uint u_selected_capacity;	// entries of the list of selected clusters
	uint u_selected;		// rw bindless: [0] selected clusters, from 16 entries of 16 bytes (instance, mesh, cluster, group)
	uint u_counts;			// bindless, counters of the culling of instances: [4] flat items, [8] tree items
	int3 u_cam_p;			// the camera in the fixed point of the GPU scene, the positions of instances are relative to it
	uint u_tree_groups;		// groups of the dispatch of the trees at most (gpu_scene_cull.hlsl, ARGS dispatches min(items, this)), each has a slice of the scratch buffer
	float3 u_cam_frac;		// -camera_offset of the instances
	uint u_meshes;			// bindless, GPUClusterMesh[], 32 bytes
	uint u_instances;		// bindless, the model instances of the GPU scene, 96 bytes
	uint u_chunks;			// bindless, the chunks of instanced models (GPUSceneChunk, 32 bytes)
	uint u_cluster_data;	// bindless, the buffer with the clusters and nodes of all meshes, see MeshDesc
	uint u_scratch_slice;	// u32 of the slice of a group in u_scratch: two lists, each as large as the largest mesh
	uint u_feedback;		// rw bindless, one bit for each page of the meshes that stream: a cluster of a group of the page was drawn (if it is loaded) or a group of the page is needed (if it is not), the CPU clears the bits after reading them
	uint u_occluded;		// rw bindless: [0] occluded clusters from pass 1, from 16 entries of 16 bytes (instance, mesh, cluster | error_small_bit, instance_ref)
	uint u_feedback_capacity;	// groups in u_feedback, 0 if there is none
	uint u_occluded_capacity;	// entries of u_occluded
	// Occlusion culling with the pyramid of depths (hzb.hlsl). The rows of the matrices (projection * rotation of the camera) transform positions relative to the camera of the
	// culling. The previous frame is the camera and the pyramid of the previous frame: its positions are the ones of the current frame + u_prev_delta.
	float4 u_prev_vp[4];
	float4 u_cur_vp[4];
	float4 u_prev_delta;	// xyz: the camera of the culling - the camera of the previous frame
	uint u_hzb_mode;		// 0: no occlusion culling. 1: the first pass of a frame: clusters hidden in the previous frame are appended to u_occluded. 2: the second pass (POST_OCCLUDED)
	uint u_hzb_prev;		// bindless texture, the pyramid of the previous frame
	uint u_hzb_cur;			// bindless texture, the pyramid made after the first pass of this frame
	uint u_hzb_mips;		// the levels of the full resolution pyramid (the first one, level 0, is not in the textures)
	float2 u_hzb_size;		// of the level 0, the size of the depth buffer
	uint u_args;			// rw bindless, indirect dispatch args (POST_ARGS)
	uint u_hzb_padding;
};

// threads of a group, GPUSceneConst::CLUSTER_THREADS
#define GROUP_SIZE 64

struct MeshDesc {
	float4 camera_offset;		// -cam_frac
	uint clusters;				// byte offset in u_cluster_data of MeshClusterGPU[]
	uint nodes;					// byte offset in u_cluster_data of MeshNodeGPU[node_count], then the indices of root_count roots
	uint node_count;
	uint root_count;
	uint roots_offset;			// byte offset of the roots in nodes
	uint cluster_count;
	uint mesh_index;			// in the mesh table
	uint groups;				// byte offset in u_cluster_data of the table of groups (MeshGroupAddressGPU, 16 bytes), the flags are the last word
};

// the description of a mesh of the mesh table of the GPU scene (GPUClusterMesh), `mesh` is its index in the table. The nodes follow the clusters (80 bytes each),
// the roots follow the nodes (64 bytes each), see gpu_cluster_models.h
MeshDesc loadSceneDesc(uint mesh) {
	ByteAddressBuffer meshes = bindless_buffers[u_meshes];
	const uint4 a = meshes.Load4(mesh * CLUSTER_MESH_STRIDE + CLUSTER_MESH_TABLE);			// clusters, group table, cluster count, node count
	const uint root_count = meshes.Load(mesh * CLUSTER_MESH_STRIDE + CLUSTER_MESH_ROOT_COUNT);
	MeshDesc r;
	r.camera_offset = float4(-u_cam_frac, 1);
	r.clusters = a.x; r.nodes = a.x + a.z * CLUSTER_STRIDE; r.cluster_count = a.z; r.node_count = a.w;
	r.root_count = root_count; r.roots_offset = a.w * NODE_STRIDE;
	r.groups = a.y;
	r.mesh_index = mesh;
	return r;
}

struct Instance {
	float4 rot;
	float3 pos;
	float3 scale;
};

// `slot` is the model instance, `instance` is 0 if the model instance is drawn itself, otherwise the instance + 1 in the buffer of the instances of the chunk, which is `slot`
Instance loadInstance(MeshDesc desc, uint slot, uint instance) {
	Instance r;
	// positions are in fixed point. The difference to the camera is exact, camera_offset (added below) is -cam_frac.
	if (instance == 0) {
		ByteAddressBuffer instances = bindless_buffers[u_instances];
		const uint offset = slot * INSTANCE_STRIDE;
		r.pos = float3(asint(instances.Load3(offset + INSTANCE_POS)) - u_cam_p) * SCENE_POS_UNIT;
		r.rot = asfloat(instances.Load4(offset + INSTANCE_ROT));
		r.scale = asfloat(instances.Load3(offset + INSTANCE_SCALE));
	}
	else {
		// an instance of a chunk of an instanced model: 32 bytes in the buffer of the instances, relative to the position of the chunk
		ByteAddressBuffer chunks = bindless_buffers[u_chunks];
		r.pos = float3(asint(chunks.Load3(slot * CHUNK_STRIDE + CHUNK_P)) - u_cam_p) * SCENE_POS_UNIT;
		ByteAddressBuffer data = bindless_buffers[NonUniformResourceIndex(chunks.Load(slot * CHUNK_STRIDE + CHUNK_INSTANCE_BUFFER))];
		const uint instance_at = (instance - 1) * CHUNK_INSTANCE_STRIDE;
		const float4 rot_lod = asfloat(data.Load4(instance_at + CHUNK_INSTANCE_ROT_LOD));
		const float4 pos_scale = asfloat(data.Load4(instance_at + CHUNK_INSTANCE_POS_SCALE));
		r.rot = float4(rot_lod.xyz, sqrt(saturate(1 - dot(rot_lod.xyz, rot_lod.xyz))));
		r.pos += pos_scale.xyz;
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

#define HZB_DETAIL_LEVELS 2
// The pyramid has no level 0 (hzb.hlsl): its first level is half of the size of the depth buffer, so clusters whose rectangle is 4 pixels or smaller are tested at the
// level 1 (a texel is 2 x 2 pixels). It culled the same as with level 0, in the benchmarks (nanite_bench) with half of the cost of building the pyramid.
#define HZB_MIN_MIP 1

// True if the sphere (relative to the camera of the culling) is certainly behind what the pyramid has: every pixel that it can cover has a nearer depth than the
// nearest point of the sphere. `previous`: the pyramid and the camera of the previous frame, otherwise the ones of this frame. False if it is not sure (the sphere
// may touch the near plane).
bool isOccluded(float3 center, float radius, bool previous) {
	const float3 c = previous ? center + u_prev_delta.xyz : center;
	const float4 p = float4(c, 1);
	float4 clip;
	float4 row0, row1;
	if (previous) {
		clip = float4(dot(u_prev_vp[0], p), dot(u_prev_vp[1], p), dot(u_prev_vp[2], p), dot(u_prev_vp[3], p));
		row0 = u_prev_vp[0];
		row1 = u_prev_vp[1];
	}
	else {
		clip = float4(dot(u_cur_vp[0], p), dot(u_cur_vp[1], p), dot(u_cur_vp[2], p), dot(u_cur_vp[3], p));
		row0 = u_cur_vp[0];
		row1 = u_cur_vp[1];
	}
	// The sphere is bounded in the space of the view: the distance along the view direction of its points is in [w - r, w + r] (w = clip.w), the nearest of them gives the largest
	// rectangle and the nearest depth. The scales of the projection are the lengths of the rows of the rotation (the projection of the camera is symmetric).
	const float w_near = clip.w - radius;
	if (w_near <= 1e-4) return false; // it may touch the near plane
	const float w_far = clip.w + radius;
	const float2 scale = float2(length(row0.xyz), length(row1.xyz)) * radius;
	const float2 n_max = clip.xy + scale;
	const float2 n_min = clip.xy - scale;
	const float2 ndc_max = n_max / float2(n_max.x > 0 ? w_near : w_far, n_max.y > 0 ? w_near : w_far);
	const float2 ndc_min = n_min / float2(n_min.x < 0 ? w_near : w_far, n_min.y < 0 ? w_near : w_far);
	// the depth is reversed, larger is nearer, the nearest point of the sphere
	const float nearest = clip.z / w_near;
	const float2 uv_a = toScreenUV(ndc_min * 0.5 + 0.5);
	const float2 uv_b = toScreenUV(ndc_max * 0.5 + 0.5);
	float2 mn = saturate(min(uv_a, uv_b));
	float2 mx = saturate(max(uv_a, uv_b));

	// The level where the rectangle covers 2 to 4 texels in a row, so at most 5 (HZB_DETAIL_LEVELS finer than the one with 1 texel): the texels of a coarser level cover much more
	// than the rectangle, the farthest depth of a larger area hides much less (a tiny cluster behind the surface of its own mesh is often next to its silhouette).
	// With 2 x 2 texels (the level without the bias) a test culled less than half of what this one does.
	const float2 size_px = (mx - mn) * u_hzb_size;
	const uint mip = min((uint)max(ceil(log2(max(max(size_px.x, size_px.y), 1.0))) - HZB_DETAIL_LEVELS, HZB_MIN_MIP), u_hzb_mips - 1);
	const int2 mip_size = max(int2(u_hzb_size) >> mip, 1);
	// A texel of a level covers the pixels [i << mip, (i + 1) << mip) (the last one also the rest of an odd size, see hzb.hlsl), so the texels are found from the pixels,
	// scaling the uv by the size of the level would drift away from them at the far edges when the size is not a multiple of 1 << mip.
	const int2 t0 = min(int2(mn * u_hzb_size) >> mip, mip_size - 1);
	const int2 t1 = min(int2(mx * u_hzb_size) >> mip, mip_size - 1);
	Texture2D<float4> hzb = bindless_textures[previous ? u_hzb_prev : u_hzb_cur];
	float farthest = 1e30;
	for (int y = t0.y; y <= t1.y; ++y) {
		for (int x = t0.x; x <= t1.x; ++x) farthest = min(farthest, hzb.Load(int3(x, y, mip - HZB_MIN_MIP)).r);	// the texture starts at the level HZB_MIN_MIP
	}
	// a margin for the rounding and for the 24 bits of the depth buffer
	return farthest > nearest * 1.0005 + 1e-6;
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

// Tests a cluster and appends it to the list of selected clusters if it is selected.
void processCluster(MeshDesc desc, Instance instance, float max_scale, uint instance_idx, uint cluster, uint instance_ref) {
	ByteAddressBuffer clusters = bindless_buffers[u_cluster_data];
	uint offset = desc.clusters + cluster * CLUSTER_STRIDE;
	float4 bounds = asfloat(clusters.Load4(offset + CLUSTER_BOUNDS));
	float4 lod_bounds = asfloat(clusters.Load4(offset + CLUSTER_LOD_BOUNDS));
	float4 parent_bounds = asfloat(clusters.Load4(offset + CLUSTER_PARENT_BOUNDS));
	uint4 misc = clusters.Load4(offset + CLUSTER_MISC);
	const float2 attribute_errors = asfloat(clusters.Load2(offset + CLUSTER_ATTRIBUTE_ERRORS)); // of the cluster and of its parent
	// Streaming: the clusters of a group that is not loaded are not drawn. A cluster whose children (the group it was refined to) are not loaded is drawn as if it was
	// accurate enough, if its parent is not drawn instead. The parent decides the same for all clusters of the group, so there are no holes and no overlaps.
	const uint2 group_info = clusters.Load2(offset + CLUSTER_GROUPS);	// the group of the cluster, the group of its children
	const uint own_flags = clusters.Load(desc.groups + group_info.x * GROUP_STRIDE + GROUP_FLAGS);
	if ((own_flags & GROUP_FLAG_LOADED) == 0) return;
	const uint child_flags = group_info.y == NO_GROUP ? GROUP_FLAG_LOADED : clusters.Load(desc.groups + group_info.y * GROUP_STRIDE + GROUP_FLAGS);
	const bool refined = (child_flags & GROUP_FLAG_LOADED) != 0;
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

	// Occlusion culling in two passes. The first pass appends clusters hidden by the previous frame to u_occluded instead of drawing them;
	// the second pass (POST_OCCLUDED) re-tests only u_occluded against the depth pyramid of the first pass, avoiding a full DAG re-traversal.
	if (u_hzb_mode == 1) {
		if (isOccluded(center, radius, true)) {
			RWByteAddressBuffer occluded = bindless_rw_buffers[u_occluded];
			uint at;
			occluded.InterlockedAdd(SELECTED_COUNT, 1, at);
			if (at < u_occluded_capacity) {
				occluded.Store4(SELECTED_HEADER + at * SELECTED_STRIDE, uint4(instance_idx, desc.mesh_index, cluster | (error_small ? 0x80000000u : 0u), instance_ref));
			}
			return;
		}
	}

	// the feedback of streaming: the group is used, and if the cluster is only drawn because its children are not loaded and it is not accurate enough, they are needed.
	// The ids in the groups are the ones of their pages. The page is loaded in the first case and it is not in the second, so one bit is enough, the CPU tells them apart by the state of the page.
	if (u_feedback_capacity != 0) {
		RWByteAddressBuffer feedback = bindless_rw_buffers[u_feedback];
		const uint own_id = own_flags >> GROUP_PAGE_SHIFT;
		if (own_id < u_feedback_capacity) markFeedback(feedback, own_id);
		const uint child_id = child_flags >> GROUP_PAGE_SHIFT;
		if (!refined && !error_small && child_id < u_feedback_capacity) markFeedback(feedback, child_id);
	}

	RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
	uint at;
	selected.InterlockedAdd(SELECTED_COUNT, 1, at);
	if (at < u_selected_capacity) selected.Store4(SELECTED_HEADER + at * SELECTED_STRIDE, uint4(instance_idx, desc.mesh_index, cluster | (computeDepthBin(center) << 27), instance_ref));
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
groupshared uint g_item_index;

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID) {
	#if defined POST_ARGS
		if (group_thread_id.x == 0) {
			bindless_rw_buffers[u_selected].Store4(SELECTED_COUNT, uint4(0, 0, 0, 0));
			const uint count = min(bindless_rw_buffers[u_occluded].Load(SELECTED_COUNT), u_occluded_capacity);
			const uint groups = (count + GROUP_SIZE - 1) / GROUP_SIZE;
			bindless_rw_buffers[u_args].Store3(ARGS_POST, uint3(min(groups, MAX_GROUPS_X), (groups + MAX_GROUPS_X - 1) / MAX_GROUPS_X, 1));
		}
	#elif defined POST_OCCLUDED
		const uint idx = (group_id.y * MAX_GROUPS_X + group_id.x) * GROUP_SIZE + group_thread_id.x;
		RWByteAddressBuffer occluded = bindless_rw_buffers[u_occluded];
		const uint count = min(occluded.Load(SELECTED_COUNT), u_occluded_capacity);
		if (idx >= count) return;
		const uint4 entry = occluded.Load4(SELECTED_HEADER + idx * SELECTED_STRIDE);
		const uint instance_idx = entry.x;
		const uint mesh_index = entry.y;
		const uint cluster = entry.z & 0x7fffffffu;
		const bool error_small = (entry.z & 0x80000000u) != 0;
		const uint instance_ref = entry.w;

		const MeshDesc desc = loadSceneDesc(mesh_index);
		const Instance instance = loadInstance(desc, instance_idx, instance_ref);
		const float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));

		ByteAddressBuffer clusters = bindless_buffers[u_cluster_data];
		const uint offset = desc.clusters + cluster * CLUSTER_STRIDE;
		const float4 bounds = asfloat(clusters.Load4(offset + CLUSTER_BOUNDS));
		const float3 cull_pos = instance.pos + u_cull_offset.xyz;
		const float3 center = cull_pos + rotateByQuat(instance.rot, bounds.xyz * instance.scale);
		const float radius = bounds.w * max_scale;
		if (isOccluded(center, radius, false)) return;

		if (u_feedback_capacity != 0) {
			const uint2 group_info = clusters.Load2(offset + CLUSTER_GROUPS);
			const uint own_flags = clusters.Load(desc.groups + group_info.x * GROUP_STRIDE + GROUP_FLAGS);
			const uint child_flags = group_info.y == NO_GROUP ? GROUP_FLAG_LOADED : clusters.Load(desc.groups + group_info.y * GROUP_STRIDE + GROUP_FLAGS);
			const bool refined = (child_flags & GROUP_FLAG_LOADED) != 0;
			RWByteAddressBuffer feedback = bindless_rw_buffers[u_feedback];
			const uint own_id = own_flags >> GROUP_PAGE_SHIFT;
			if (own_id < u_feedback_capacity) markFeedback(feedback, own_id);
			const uint child_id = child_flags >> GROUP_PAGE_SHIFT;
			if (!refined && !error_small && child_id < u_feedback_capacity) markFeedback(feedback, child_id);
		}

		RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
		uint at;
		selected.InterlockedAdd(SELECTED_COUNT, 1, at);
		if (at < u_selected_capacity) selected.Store4(SELECTED_HEADER + at * SELECTED_STRIDE, uint4(instance_idx, mesh_index, cluster | (computeDepthBin(center) << 27), instance_ref));
	#elif defined FLAT
		const uint group_index = group_id.y * MAX_GROUPS_X + group_id.x;
		// the number of items is in a counter of the GPU, u_item_capacity is the capacity of the list
		const uint item_count = min(bindless_rw_buffers[u_counts].Load(COUNTER_FLAT), u_item_capacity);
		if (group_index >= item_count) return; // the same for all threads of the group
		const uint4 item = bindless_buffers[u_items].Load4(u_items_offset + group_index * ITEM_STRIDE); // mesh, slot, first cluster, instance
		const MeshDesc desc = loadSceneDesc(item.x);
		const uint instance_idx = item.y;
		Instance instance = loadInstance(desc, instance_idx, item.w);
		// bounds are spheres, so non-uniform scale is handled by the largest axis
		float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));
		// 64 consecutive clusters of an instance
		const uint cluster = item.z + group_thread_id.x;
		if (cluster < desc.cluster_count) processCluster(desc, instance, max_scale, instance_idx, cluster, item.w);
	#else
		// Persistent groups with dynamic atomic work queue and early-exit tree traversal (SM 6.0 / DXC).
		RWByteAddressBuffer counts = bindless_rw_buffers[u_counts];
		const uint item_count = min(counts.Load(COUNTER_TREE), u_item_capacity);
		ByteAddressBuffer nodes = bindless_buffers[u_cluster_data];
		RWByteAddressBuffer scratch = bindless_rw_buffers[u_scratch];
		if ((group_id.x + 1) * u_scratch_slice > u_scratch_size) return;	// the slice does not fit, the same for all threads of the group (the CPU makes it fit)
		const uint scratch_base = group_id.x * u_scratch_slice * 4;

		[loop] for (;;) {
			if (group_thread_id.x == 0) {
				counts.InterlockedAdd(COUNTER_TREE_QUEUE, 1, g_item_index);
			}
			GroupMemoryBarrierWithGroupSync();
			const uint group_index = g_item_index;
			if (group_index >= item_count) break;

			const uint4 item = bindless_buffers[u_items].Load4(u_items_offset + group_index * ITEM_STRIDE); // mesh, slot, first cluster, instance
			const MeshDesc desc = loadSceneDesc(item.x);
			const uint instance_idx = item.y;
			Instance instance = loadInstance(desc, instance_idx, item.w);
			// bounds are spheres, so non-uniform scale is handled by the largest axis
			float max_scale = max(instance.scale.x, max(instance.scale.y, instance.scale.z));
			const uint list_size = desc.node_count * 4;	// two lists of the mesh are in the slice

			// the roots are the first nodes to visit
			for (uint r = group_thread_id.x; r < desc.root_count; r += GROUP_SIZE) {
				scratch.Store(scratch_base + r * 4, nodes.Load(desc.nodes + desc.roots_offset + r * ROOT_STRIDE));
			}
			if (group_thread_id.x == 0) {
				g_current_count = desc.root_count;
				g_next_count = 0;
			}
			DeviceMemoryBarrierWithGroupSync();

			uint list = 0;
			[loop] for (uint depth = 0; depth < u_tree_depth; ++depth) {
				const uint count = g_current_count;
				if (count == 0) break;
				const uint from = scratch_base + list * list_size;
				const uint to = scratch_base + (1 - list) * list_size;

				for (uint i = group_thread_id.x; i < count; i += GROUP_SIZE) {
					const uint node_id = scratch.Load(from + i * 4);
					const uint node_offset = desc.nodes + node_id * NODE_STRIDE;
					const float4 bounds = asfloat(nodes.Load4(node_offset + NODE_BOUNDS));
					const float4 lod_bounds = asfloat(nodes.Load4(node_offset + NODE_LOD_BOUNDS));
					const uint4 misc = nodes.Load4(node_offset + NODE_MISC); // min error, max parent error, first, count and flags
					const float2 attribute_errors = asfloat(nodes.Load2(node_offset + NODE_ATTRIBUTE_ERRORS)); // the smallest and the largest of parents
					if (!isNodeNeeded(bounds, lod_bounds, asfloat(misc.x), asfloat(misc.y), attribute_errors.x, attribute_errors.y, nodes.Load(node_offset + NODE_UNREFINED) != 0, instance, max_scale)) continue;

					const uint child_count = misc.w & NODE_COUNT_MASK;
					if (misc.w & NODE_LEAF_BIT) {
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
		}
	#endif
}
