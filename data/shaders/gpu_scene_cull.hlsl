#include "engine/shaders/common.hlsli"
#include "engine/shaders/gpu_scene_layout.hlsli"

// Culls the model instances of the GPU scene (src/renderer/gpu_scene.h) by a frustum (the sphere of each) and generates the work of the culling of clusters (nanite.hlsl)
// for what is inside: for each mesh with clusters of the model, one item (mesh, slot, first cluster, instance) per group of 64 clusters of a flat mesh, or one item of
// a mesh with a tree, appended to the list of flat or tree items. `instance` is 0 for a model instance that is drawn itself, otherwise the instance (+ 1) of a chunk of an
// instanced model, its index in the buffer of the instances of the model instance.
// Counters (u32): [1] flat items, [2] tree items, [3] chunk items (can be more than fit into the lists).
// The id of a model of a model instance (GPUSceneInstance::model) is an index in the table of models with clusters (GPUClusterModel, gpu_cluster_models.h), or, with the bit
// MODEL_LOD_BIT, in the table of models without clusters (GPULodModel, gpu_lod_models.h).
// A model without clusters has no work of clusters: its LOD is chosen by the distance to u_lod_ref, and each mesh of the LOD is appended to the
// list of selected clusters (u_selected) with the cluster 0xffffffff and the index of the mesh in its model, which gpu_scene_variants.hlsl sorts into the draws of the
// meshes. The flags of the model instance can force its LOD.
//   PASS0: clears the counters and the list of selected clusters, and writes the groups of the dispatch of the model instance pass to u_args (indirect dispatch)
//   otherwise (the model instance pass): one thread per slot, positions are fixed point, the camera-relative position is exact:
//     float3(p - cam_p) * unit - cam_frac
//   CHUNK_CULL (after the model instance pass): one thread per chunk of an instanced model (gpu_scene_chunks.h): the same test of the sphere of the chunk, and one chunk item (chunk,
//     first instance, count) for each 64 of its instances.
//   FILTER (with the model instance pass): only the slots with a bit set in u_filter are culled in (renderEntities), a variant of its own so that other views do not pay for it.
//   CHUNK_INSTANCE_ARGS: after the model instance pass, the groups of the dispatch of CHUNK_INSTANCE_CULL (u_args + 64)
//   CHUNK_INSTANCE_CULL: a group of 64 threads for each chunk item, a thread for each instance: the same test and the same work as a model instance. The `slot` of the work of an
//     instance is its chunk
//   ARGS: after the culling, writes the groups of the dispatches of the flat items (u_args + 16) and the tree items (u_args + 32) of the culling of
//         clusters (nanite.hlsl)
// Planes are relative to the camera, as the ones of nanite.hlsl.

cbuffer UniformData : register(b4) {
	float4 u_planes[6];
	int3 u_cam_p;
	float u_unit;
	float3 u_cam_frac;
	uint u_capacity;
	uint u_bounds;			// bindless, 16 bytes per slot: int3 p, float radius (< 0: free slot)
	uint u_counter;			// rw bindless, four u32
	uint u_args;			// rw bindless, the groups of the indirect dispatches
	uint u_selected_capacity;	// entries of the list of selected clusters
	uint u_instances;		// bindless, GPUSceneInstance[], 96 bytes per slot
	uint u_model_count;		// models with clusters
	uint u_models;			// bindless, GPUClusterModel[], 8 bytes
	uint u_meshes;			// bindless, GPUClusterMesh[], 32 bytes
	uint u_flat_items;		// rw bindless, 16 bytes: mesh, slot, first cluster, instance
	uint u_tree_items;		// rw bindless, the same
	uint u_item_capacity;	// of each list
	uint u_clear_occluded;	// 1 in the first pass of occlusion culling: PASS0 clears u_occluded
	uint u_selected;		// rw bindless, the list of selected clusters of nanite.hlsl
	uint u_chunk_items;		// rw bindless, 16 bytes: slot, first instance, count, 0
	uint u_chunk_capacity;
	uint u_lod_models;	// bindless, GPULodModel[], 40 bytes
	float3 u_lod_ref;		// the point the LODs are chosen from, relative to the camera of the culling
	float u_lod_scale;		// squared distances are multiplied by it
	uint u_filter;			// bindless, a bit for each slot (FILTER)
	uint u_filter_offset;	// in bytes
	uint u_lod_model_count;
	uint u_occluded;		// rw bindless, list of occluded clusters written by pass 1 of nanite.hlsl
	uint u_chunk_bounds;	// bindless, 16 bytes per chunk, like u_bounds (CHUNK_CULL)
	uint u_chunks;			// bindless, GPUSceneChunk[], 32 bytes: int3 p, model, instance buffer, first instance, instance count, float instance radius
	uint u_chunk_slots;		// the chunks the tables have
	uint u_tree_groups;		// groups of the dispatch of the trees of nanite.hlsl at most
};

#define FLAT_MAX_CLUSTERS 512		// GPUSceneConst::FLAT_MAX_CLUSTERS
#define CLUSTER_THREADS 64
#define GROUP_THREADS 64			// numthreads of main

// A mesh of a model without clusters is drawn: the entry of the list of selected clusters (slot, mesh in its model, cluster, instance) with no cluster
void emitLod(uint model, uint slot, float3 center, uint flags, uint instance) {
	ByteAddressBuffer models = bindless_buffers[u_lod_models];
	const uint far_range = models.Load(model * LOD_MODEL_STRIDE + LOD_MODEL_FAR_RANGE);
	const uint4 lods = models.Load4(model * LOD_MODEL_STRIDE + LOD_MODEL_LODS);
	const float4 distances = asfloat(models.Load4(model * LOD_MODEL_STRIDE + LOD_MODEL_DISTANCES));
	const float3 d = center - u_lod_ref;
	const float d2 = dot(d, d) * u_lod_scale;
	uint range = far_range;
	if ((flags & INSTANCE_FORCED_LOD) != 0) {
		// the LOD of the model instance is set (previews)
		const uint lod = (flags >> INSTANCE_FORCED_LOD_SHIFT) & INSTANCE_FORCED_LOD_MASK;
		if (lod == 0) range = lods.x;
		else if (lod == 1) range = lods.y;
		else if (lod == 2) range = lods.z;
		else if (lod == 3) range = lods.w;
	}
	else if (d2 < distances.x) range = lods.x;
	else if (d2 < distances.y) range = lods.y;
	else if (d2 < distances.z) range = lods.z;
	else if (d2 < distances.w) range = lods.w;
	const uint first = range & 0xffff;
	const uint end = range >> 16;	// the last mesh + 1, 0 if the LOD has no meshes
	RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
	for (uint mesh = first; mesh < end; ++mesh) {
		uint at;
		selected.InterlockedAdd(SELECTED_COUNT, 1, at);
		if (at < u_selected_capacity) selected.Store4(SELECTED_HEADER + at * SELECTED_STRIDE, uint4(slot, mesh, NO_CLUSTER, instance));
	}
}

bool insideFrustum(float3 center, float radius) {
	for (int i = 0; i < 6; ++i) {
		if (dot(u_planes[i], float4(center, 1)) < -radius) return false;
	}
	return true;
}

// what a visible model instance or instance with the model draws
void emit(RWByteAddressBuffer counter, uint model, uint slot, float3 center, uint flags, uint instance) {
	if (model == NO_MODEL) return;
	if ((model & MODEL_LOD_BIT) != 0) {
		const uint index = model & ~MODEL_LOD_BIT;
		if (index < u_lod_model_count) emitLod(index, slot, center, flags, instance);
	}
	else if (model < u_model_count && u_meshes != 0) {
		// the work of the clusters of the meshes of the model
		const uint2 meshes = bindless_buffers[u_models].Load2(model * CLUSTER_MODEL_STRIDE);	// first mesh, mesh count
		for (uint mesh_index = 0; mesh_index < meshes.y; ++mesh_index) {
			const uint mesh = meshes.x + mesh_index;
			const uint cluster_count = bindless_buffers[u_meshes].Load(mesh * CLUSTER_MESH_STRIDE + CLUSTER_MESH_CLUSTER_COUNT);
			const bool flat = cluster_count <= FLAT_MAX_CLUSTERS;
			const uint count = flat ? (cluster_count + CLUSTER_THREADS - 1) / CLUSTER_THREADS : 1;	// groups of a flat mesh
			for (uint group = 0; group < count; ++group) {
				uint at;
				counter.InterlockedAdd(flat ? COUNTER_FLAT : COUNTER_TREE, 1, at);
				if (at < u_item_capacity) bindless_rw_buffers[flat ? u_flat_items : u_tree_items].Store4(at * ITEM_STRIDE, uint4(mesh, slot, flat ? group * CLUSTER_THREADS : 0, instance));
			}
		}
	}
}

// the groups of an indirect dispatch of `count` threads
uint3 dispatchGroups(uint count) {
	return uint3(min(count, MAX_GROUPS_X), (count + MAX_GROUPS_X - 1) / MAX_GROUPS_X, 1);
}

[numthreads(GROUP_THREADS, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID) {
	RWByteAddressBuffer counter = bindless_rw_buffers[u_counter];
	#if defined ARGS
		if (thread_id.x == 0) {
			RWByteAddressBuffer args = bindless_rw_buffers[u_args];
			const uint flat = min(counter.Load(COUNTER_FLAT), u_item_capacity);
			const uint tree = min(counter.Load(COUNTER_TREE), u_item_capacity);
			args.Store3(ARGS_FLAT, dispatchGroups(flat));
			args.Store3(ARGS_TREE, dispatchGroups(min(tree, u_tree_groups)));	// a group takes more than one item if there are more
		}
	#elif defined CHUNK_INSTANCE_ARGS
		if (thread_id.x == 0) {
			const uint chunk_items = min(counter.Load(COUNTER_CHUNK), u_chunk_capacity);
			bindless_rw_buffers[u_args].Store3(ARGS_CHUNK_INSTANCES, dispatchGroups(chunk_items));
		}
	#elif defined PASS0
		// one group clears the counters
		if (thread_id.x < COUNTER_COUNT) counter.Store(thread_id.x * 4, 0);
		if (thread_id.x == 0) {
			bindless_rw_buffers[u_selected].Store4(SELECTED_COUNT, uint4(0, 0, 0, 0));
			if (u_clear_occluded != 0) bindless_rw_buffers[u_occluded].Store4(SELECTED_COUNT, uint4(0, 0, 0, 0));
			const uint groups = (u_capacity + GROUP_THREADS - 1) / GROUP_THREADS;
			bindless_rw_buffers[u_args].Store3(ARGS_MODEL_INSTANCES, dispatchGroups(groups));
		}
	#elif defined CHUNK_CULL
		const uint slot = thread_id.y * MAX_GROUPS_X * GROUP_THREADS + thread_id.x;
		if (slot >= u_chunk_slots) return;
		const uint4 b = bindless_buffers[u_chunk_bounds].Load4(slot * BOUNDS_STRIDE);
		const float radius = asfloat(b.w);
		if (radius < 0) return;
		const float3 center = float3(asint(b.xyz) - u_cam_p) * u_unit - u_cam_frac;
		if (!insideFrustum(center, radius)) return;

		// its instances are tested by CHUNK_INSTANCE_CULL, GROUP_THREADS in each item
		const uint2 range = bindless_buffers[u_chunks].Load2(slot * CHUNK_STRIDE + CHUNK_INSTANCE_RANGE);	// first instance, count
		RWByteAddressBuffer chunk_items = bindless_rw_buffers[u_chunk_items];
		for (uint first = 0; first < range.y; first += GROUP_THREADS) {
			uint at;
			counter.InterlockedAdd(COUNTER_CHUNK, 1, at);
			if (at < u_chunk_capacity) chunk_items.Store4(at * ITEM_STRIDE, uint4(slot, range.x + first, min((uint)GROUP_THREADS, range.y - first), 0));
		}
	#elif defined CHUNK_INSTANCE_CULL
		const uint item_index = group_id.y * MAX_GROUPS_X + group_id.x;
		if (item_index >= min(counter.Load(COUNTER_CHUNK), u_chunk_capacity)) return;	// the same for all threads of the group
		const uint4 item = bindless_rw_buffers[u_chunk_items].Load4(item_index * ITEM_STRIDE);	// slot, first instance, count
		if (group_thread_id.x >= item.z) return;
		const uint slot = item.x;
		const uint instance = item.y + group_thread_id.x;

		ByteAddressBuffer chunks = bindless_buffers[u_chunks];
		const uint4 p_model = chunks.Load4(slot * CHUNK_STRIDE + CHUNK_P);
		const uint instance_buffer = chunks.Load(slot * CHUNK_STRIDE + CHUNK_INSTANCE_BUFFER);
		const float instance_radius = asfloat(chunks.Load(slot * CHUNK_STRIDE + CHUNK_INSTANCE_RADIUS));
		const float4 pos_scale = asfloat(bindless_buffers[NonUniformResourceIndex(instance_buffer)].Load4(instance * CHUNK_INSTANCE_STRIDE + CHUNK_INSTANCE_POS_SCALE));
		const float3 center = float3(asint(p_model.xyz) - u_cam_p) * u_unit - u_cam_frac + pos_scale.xyz;
		if (!insideFrustum(center, instance_radius * pos_scale.w)) return;
		emit(counter, p_model.w, slot, center, 0, instance + 1);  // 0 is a model instance that is drawn itself
	#else
		const uint slot = thread_id.y * MAX_GROUPS_X * GROUP_THREADS + thread_id.x;
		if (slot >= u_capacity) return;
		#ifdef FILTER
			if ((bindless_buffers[u_filter].Load(u_filter_offset + (slot >> 5) * 4) & (1u << (slot & 31))) == 0) return;
		#endif

		const uint4 b = bindless_buffers[u_bounds].Load4(slot * BOUNDS_STRIDE);
		const float radius = asfloat(b.w);
		if (radius < 0) return;

		const float3 center = float3(asint(b.xyz) - u_cam_p) * u_unit - u_cam_frac;
		if (!insideFrustum(center, radius)) return;

		ByteAddressBuffer instances = bindless_buffers[u_instances];
		const uint model = instances.Load(slot * INSTANCE_STRIDE + INSTANCE_MODEL);
		const uint flags = instances.Load(slot * INSTANCE_STRIDE + INSTANCE_FLAGS);
		emit(counter, model, slot, center, flags, 0);
	#endif
}
