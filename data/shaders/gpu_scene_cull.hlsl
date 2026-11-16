#include "engine/shaders/common.hlsli"

// Culls the instances of the GPU scene (src/renderer/gpu_scene.h) by a frustum (a sphere around the position of each) and generates the work of the
// culling of clusters (nanite.hlsl) for the instances that are inside: for each mesh with clusters of the model of the instance (the model table and
// the mesh table of the scene), one item (mesh, slot, first cluster) per group of 64 clusters of a flat mesh, or one item of a mesh with a tree,
// appended to the list of flat or tree items.
// Counters (u32): [1] flat items and [2] tree items (can be more than fit into the lists).
// An instance of a PLAIN model (a model without clusters, GPUSceneModel) has no work of clusters: its LOD is chosen by the distance to u_lod_ref, and each
// mesh of the LOD is appended to the list of selected clusters (u_selected) with the cluster 0xffffffff, which gpu_scene_variants.hlsl sorts into the draws
// of the meshes. The flags of the instance (at 44) can force its LOD.
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

// A mesh of a PLAIN model is drawn: the entry of the list of selected clusters (instance, mesh, cluster, group) with no cluster
void emitPlain(uint model, uint slot, float3 center, uint instance_flags) {
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
	const uint first = range & 0xffff;
	const uint end = range >> 16;	// the last mesh + 1, 0 if the LOD has no meshes
	const uint first_mesh = bindless_buffers[u_models].Load(model * 48);
	RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
	for (uint mesh = first; mesh < end; ++mesh) {
		uint at;
		selected.InterlockedAdd(0, 1, at);
		if (at < u_selected_capacity) selected.Store4(16 + at * 16, uint4(slot, first_mesh + mesh, 0xffffffff, 0));
	}
}

// The work of the clusters of the meshes of the model of an instance: `instance` is its slot, or its number in the group if `group_ref` is not 0
void emitWork(RWByteAddressBuffer counter, uint model, uint instance, uint group_ref) {
	if (u_meshes == 0) return;
	const uint2 meshes = bindless_buffers[u_models].Load2(model * 48);
	for (uint mesh_index = 0; mesh_index < meshes.y; ++mesh_index) {
		const uint mesh = meshes.x + mesh_index;
		const uint2 info = bindless_buffers[u_meshes].Load2(mesh * 64 + 36);	// flags, groups of a flat mesh
		const bool flat = (info.x & MESH_FLAT) != 0;
		const uint count = flat ? info.y : 1;
		for (uint group = 0; group < count; ++group) {
			uint at;
			counter.InterlockedAdd(flat ? 4 : 8, 1, at);
			if (at < u_item_capacity) bindless_rw_buffers[flat ? u_flat_items : u_tree_items].Store4(at * 16, uint4(mesh, instance, flat ? group * 64 : 0, group_ref));
		}
	}
}

[numthreads(64, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	RWByteAddressBuffer counter = bindless_rw_buffers[u_counter];
	#if defined ARGS
		if (thread_id.x == 0) {
			RWByteAddressBuffer args = bindless_rw_buffers[u_args];
			const uint flat = min(counter.Load(4), u_item_capacity);
			const uint tree = min(counter.Load(8), u_item_capacity);
			args.Store3(16, uint3(min(flat, 65535u), (flat + 65534) / 65535, 1));
			args.Store3(32, uint3(min(tree, 65535u), (tree + 65534) / 65535, 1));
		}
	#elif defined PASS0
		// one group clears the counters
		if (thread_id.x < 3) counter.Store(thread_id.x * 4, 0);
		if (thread_id.x == 0) {
			bindless_rw_buffers[u_selected].Store4(0, uint4(0, 0, 0, 0));
			const uint groups = (u_capacity + 63) / 64;
			bindless_rw_buffers[u_args].Store3(0, uint3(min(groups, 65535u), (groups + 65534) / 65535, 1));
		}
	#elif defined GROUPS
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
		for (int i = 0; i < 6; ++i) {
			if (dot(u_planes[i], float4(center, 1)) < -radius) return;
		}
		if (bindless_buffers[u_models].Load(group.y * 48 + 8) != 0) return;	// instanced models are made of clusters
		emitWork(counter, group.y, local, low + 1);
	#else
		const uint slot = thread_id.y * 65535 * 64 + thread_id.x;
		if (slot >= u_capacity) return;

		const uint4 b = bindless_buffers[u_bounds].Load4(slot * 16);
		const float radius = asfloat(b.w);
		if (radius < 0) return;

		const float3 center = float3(asint(b.xyz) - u_cam_p) * u_unit - u_cam_frac;
		for (int i = 0; i < 6; ++i) {
			if (dot(u_planes[i], float4(center, 1)) < -radius) return;
		}
		const uint model = bindless_buffers[u_instances].Load(slot * 96 + 28);
		if (model >= u_model_count) return;

		if (bindless_buffers[u_models].Load(model * 48 + 8) != 0) emitPlain(model, slot, center, bindless_buffers[u_instances].Load(slot * 96 + 44));
		else emitWork(counter, model, slot, 0);
	#endif
}
