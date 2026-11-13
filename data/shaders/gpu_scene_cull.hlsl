#include "engine/shaders/common.hlsli"

// Culls the instances of the GPU scene (src/renderer/gpu_scene.h) by a frustum (a sphere around the position of each) and generates the work of the
// culling of clusters (nanite.hlsl) for the instances that are inside: for each mesh with clusters of the model of the instance (the model table and
// the mesh table of the scene), one item (mesh, slot, first cluster) per group of 64 clusters of a flat mesh, or one item of a mesh with a tree,
// appended to the list of flat or tree items.
// Counters (u32): [1] flat items and [2] tree items (can be more than fit into the lists).
// An instance of a PLAIN model (a model without clusters, GPUSceneModel) has no work of clusters: its LOD is chosen by the distance to u_lod_ref, and each
// mesh of the LOD is appended to the list of selected clusters (u_selected) with the cluster 0xffffffff, which gpu_scene_variants.hlsl sorts into the draws
// of the meshes. The flags of the instance (at 44) can force its LOD.
// The items of the threads of a group are reserved together (emitAggregated): one atomic of the group on each counter, not one of each thread.
//   PASS0: clears the counters and the list of selected clusters, and writes the groups of the dispatch of the next pass to u_args (indirect dispatch)
//   GROUPS: after the culling of slots, one thread for each instance of the groups of instanced models (GPUSceneGroup): the same test and the same work, the
//           item has the group (+ 1) in its last word and the number of the instance in the group in the second
//   ARGS: after the culling, writes the groups of the dispatches of the flat items (u_args + 16) and the tree items (u_args + 32) of the culling of
//         clusters (nanite.hlsl, SCENE_LISTS)
//   otherwise: one thread per slot, positions are fixed point, the camera-relative position is exact:
//     float3(p - cam_p) * unit - cam_frac
// Planes are relative to the camera, as the ones of nanite.hlsl.

cbuffer UniformData : register(b4) {
	float4 u_planes[6];
	int3 u_cam_p;
	float u_unit;
	float3 u_cam_frac;
	uint u_capacity;
	uint u_bounds;			// bindless, 16 bytes per slot: int3 p, float radius (< 0: free slot)
	uint u_counter;			// rw bindless, one u32
	uint u_args;			// rw bindless, three u32: the groups of the dispatch of the pass that counts
	uint u_selected_capacity;	// entries of the list of selected clusters
	uint u_instances;		// bindless, 96 bytes per slot (mesh_id at 28)
	uint u_model_count;		// counters of models: u_model_count
	uint u_models;			// bindless, GPUSceneModel[] (first mesh, mesh count)
	uint u_meshes;			// bindless, GPUSceneMesh[], 64 bytes
	uint u_flat_items;		// rw bindless, 16 bytes: mesh, slot, first cluster, 0
	uint u_tree_items;		// rw bindless, the same
	uint u_item_capacity;	// of each list
	uint u_padding2;
	uint u_selected;		// rw bindless, the list of selected clusters of nanite.hlsl (ARGS clears its counters)
	uint u_groups;			// bindless, GPUSceneGroup[], 32 bytes
	uint u_group_count;
	uint u_group_instances;	// of all the groups
	float3 u_lod_ref;		// the point the LODs are chosen from, relative to the camera of the culling
	float u_lod_scale;		// squared distances are multiplied by it
};

#define MESH_FLAT 1

// What a thread has to emit, see emitAggregated. (CullWork)0 is no work.
struct CullWork {
	uint kind;			// 0: nothing, 1: work of clusters of the meshes of a model, 2: meshes of a PLAIN model
	uint instance;		// `slot`, or the number in the group if it is work of clusters of a group
	uint a;				// clusters: the model. plain: the first mesh of the model in the mesh table
	uint b;				// clusters: the group (+ 1), 0 if none. plain: the meshes of the LOD, first | end << 16
};

// The meshes of a PLAIN model that are drawn: the LOD is chosen by the distance, or forced by the flags of the instance.
// Returns first | end << 16, end is the last mesh + 1, 0 if the LOD has no meshes
uint plainLodRange(uint model, float3 center, uint instance_flags) {
	const uint4 lods = bindless_buffers[u_models].Load4(model * 48 + 16);
	const uint lod4 = bindless_buffers[u_models].Load(model * 48 + 12);
	const float4 distances = asfloat(bindless_buffers[u_models].Load4(model * 48 + 32));
	const float3 d = center - u_lod_ref;
	const float d2 = dot(d, d) * u_lod_scale;
	uint range = lod4;
	if ((instance_flags & 1) != 0) {
		// the LOD of the instance is set (previews)
		const uint lod = (instance_flags >> 1) & 7;
		if (lod == 0) range = lods.x;
		else if (lod == 1) range = lods.y;
		else if (lod == 2) range = lods.z;
		else if (lod == 3) range = lods.w;
	}
	else if (d2 < distances.x) range = lods.x;
	else if (d2 < distances.y) range = lods.y;
	else if (d2 < distances.z) range = lods.z;
	else if (d2 < distances.w) range = lods.w;
	return range;
}

// Culls the instance of a slot, one thread per slot
void cullSlot(uint3 thread_id, out CullWork w) {
	w = (CullWork)0;
	const uint slot = thread_id.y * 65535 * 64 + thread_id.x;
	if (slot >= u_capacity) return;

	const uint4 b = bindless_buffers[u_bounds].Load4(slot * 16);
	const float radius = asfloat(b.w);
	if (radius < 0) return;

	const float3 center = float3(asint(b.xyz) - u_cam_p) * u_unit - u_cam_frac;
	bool inside = true;
	for (int i = 0; i < 6; ++i) {
		if (dot(u_planes[i], float4(center, 1)) < -radius) inside = false;
	}
	if (!inside) return;
	const uint model = bindless_buffers[u_instances].Load(slot * 96 + 28);
	if (model >= u_model_count) return;

	w.instance = slot;
	if (bindless_buffers[u_models].Load(model * 48 + 8) != 0) {
		w.kind = 2;
		w.a = bindless_buffers[u_models].Load(model * 48);
		w.b = plainLodRange(model, center, bindless_buffers[u_instances].Load(slot * 96 + 44));
	}
	else {
		w.kind = 1;
		w.a = model;
	}
}

// Culls an instance of the groups of instanced models, one thread per instance
void cullGroupInstance(uint3 thread_id, out CullWork w) {
	w = (CullWork)0;
	const uint index = thread_id.y * 65535 * 64 + thread_id.x;
	if (index >= u_group_instances) return;

	// the group of the instance: the last one that starts at or before it
	ByteAddressBuffer groups = bindless_buffers[u_groups];
	uint low = 0;
	uint high = u_group_count - 1;
	while (low < high) {
		const uint middle = (low + high + 1) >> 1;
		if (groups.Load(middle * 32 + 24) <= index) low = middle;
		else high = middle - 1;
	}
	const uint4 origin = groups.Load4(low * 32);	// origin, number of instances
	const uint4 group = groups.Load4(low * 32 + 16);	// buffer, model, first instance, radius
	const uint local = index - group.z;
	if (local >= origin.w || group.y >= u_model_count) return;

	// the instances of the groups are in buffers of their own
	const float4 pos_scale = asfloat(bindless_buffers[NonUniformResourceIndex(group.x)].Load4(local * 32 + 16));
	const float3 center = float3(asint(origin.xyz) - u_cam_p) * u_unit - u_cam_frac + pos_scale.xyz;
	const float radius = asfloat(group.w) * pos_scale.w;
	bool inside = true;
	for (int i = 0; i < 6; ++i) {
		if (dot(u_planes[i], float4(center, 1)) < -radius) inside = false;
	}
	if (!inside) return;
	if (bindless_buffers[u_models].Load(group.y * 48 + 8) != 0) return;	// instanced models are made of clusters
	w.kind = 1;
	w.instance = local;
	w.a = group.y;
	w.b = low + 1;
}

// Items of the flat and of the tree list that the clusters of the meshes of the model need
void countClusterItems(uint model, out uint flat_count, out uint tree_count) {
	flat_count = 0;
	tree_count = 0;
	if (u_meshes == 0) return;
	const uint2 meshes = bindless_buffers[u_models].Load2(model * 48);
	for (uint mesh_index = 0; mesh_index < meshes.y; ++mesh_index) {
		const uint2 info = bindless_buffers[u_meshes].Load2((meshes.x + mesh_index) * 64 + 36);	// flags, groups of a flat mesh
		if ((info.x & MESH_FLAT) != 0) flat_count += info.y;
		else tree_count += 1;
	}
}

// Stores the items of the work: the first of the flat items goes to `flat_at`, the first of the tree items to `tree_at`
void writeClusterItems(uint model, uint instance, uint group_ref, uint flat_at, uint tree_at) {
	if (u_meshes == 0) return;
	const uint2 meshes = bindless_buffers[u_models].Load2(model * 48);
	for (uint mesh_index = 0; mesh_index < meshes.y; ++mesh_index) {
		const uint mesh = meshes.x + mesh_index;
		const uint2 info = bindless_buffers[u_meshes].Load2(mesh * 64 + 36);	// flags, groups of a flat mesh
		const bool flat = (info.x & MESH_FLAT) != 0;
		const uint count = flat ? info.y : 1;
		for (uint group = 0; group < count; ++group) {
			uint at;
			if (flat) at = flat_at++;
			else at = tree_at++;
			if (at < u_item_capacity) bindless_rw_buffers[flat ? u_flat_items : u_tree_items].Store4(at * 16, uint4(mesh, instance, flat ? group * 64 : 0, group_ref));
		}
	}
}

groupshared uint g_total[3];	// items of the group: flat, tree, plain
groupshared uint g_base[3];		// where they start in their lists

// The threads of a group put their items into the lists together. Every list has one counter, and an atomic add of each thread on the same address is slow when
// many instances are visible: a thread reserves its items with groupshared counters, and one thread of the group adds the total to the counter in memory.
// It has barriers, so every thread of the group must call it (none returns before).
void emitAggregated(CullWork w, uint group_index) {
	uint counts[3] = { 0, 0, 0 };
	if (w.kind == 1) countClusterItems(w.a, counts[0], counts[1]);
	else if (w.kind == 2) counts[2] = (w.b >> 16) > (w.b & 0xffff) ? (w.b >> 16) - (w.b & 0xffff) : 0;

	if (group_index < 3) g_total[group_index] = 0;
	GroupMemoryBarrierWithGroupSync();

	uint at[3] = { 0, 0, 0 };
	if (counts[0] != 0) InterlockedAdd(g_total[0], counts[0], at[0]);
	if (counts[1] != 0) InterlockedAdd(g_total[1], counts[1], at[1]);
	if (counts[2] != 0) InterlockedAdd(g_total[2], counts[2], at[2]);
	GroupMemoryBarrierWithGroupSync();

	// the counters of memory: [1] flat items and [2] tree items, the list of selected clusters has its own at 0
	if (group_index < 3) {
		const uint total = g_total[group_index];
		uint base = 0;
		if (total != 0) {
			if (group_index == 2) bindless_rw_buffers[u_selected].InterlockedAdd(0, total, base);
			else bindless_rw_buffers[u_counter].InterlockedAdd(4 + group_index * 4, total, base);
		}
		g_base[group_index] = base;
	}
	GroupMemoryBarrierWithGroupSync();

	if (w.kind == 1) {
		writeClusterItems(w.a, w.instance, w.b, g_base[0] + at[0], g_base[1] + at[1]);
	}
	else if (w.kind == 2) {
		RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
		uint entry = g_base[2] + at[2];
		for (uint mesh = w.b & 0xffff; mesh < (w.b >> 16); ++mesh, ++entry) {
			// the entry (slot, mesh, cluster, group) with no cluster
			if (entry < u_selected_capacity) selected.Store4(16 + entry * 16, uint4(w.instance, w.a + mesh, 0xffffffff, 0));
		}
	}
}

[numthreads(64, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex) {
	#if defined ARGS
		RWByteAddressBuffer counter = bindless_rw_buffers[u_counter];
		if (thread_id.x == 0) {
			RWByteAddressBuffer args = bindless_rw_buffers[u_args];
			const uint flat = min(counter.Load(4), u_item_capacity);
			const uint tree = min(counter.Load(8), u_item_capacity);
			args.Store3(16, uint3(min(flat, 65535u), (flat + 65534) / 65535, 1));
			args.Store3(32, uint3(min(tree, 65535u), (tree + 65534) / 65535, 1));
		}
	#elif defined PASS0
		RWByteAddressBuffer counter = bindless_rw_buffers[u_counter];
		// one group clears the counters
		if (thread_id.x < 3) counter.Store(thread_id.x * 4, 0);
		if (thread_id.x == 0) {
			bindless_rw_buffers[u_selected].Store4(0, uint4(0, 0, 0, 0));
			const uint groups = (u_capacity + 63) / 64;
			bindless_rw_buffers[u_args].Store3(0, uint3(min(groups, 65535u), (groups + 65534) / 65535, 1));
		}
	#elif defined GROUPS
		CullWork w;
		cullGroupInstance(thread_id, w);
		emitAggregated(w, group_index);
	#else
		CullWork w;
		cullSlot(thread_id, w);
		emitAggregated(w, group_index);
	#endif
}
