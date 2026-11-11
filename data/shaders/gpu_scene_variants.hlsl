#include "engine/shaders/common.hlsli"

// Sorts the clusters that nanite.hlsl (SCENE_LISTS) selected, and the meshes without clusters that gpu_scene_cull.hlsl selected, into variants and writes the
// instance data of the draws. A variant (GPUScene, gpu_scene.h) is what one command of an indirect draw draws: the meshes with clusters that have the same material
// program, or one mesh without clusters with a material program. The variants of the same program and layer are a draw group, drawn by one indirect call of
// consecutive commands (the CPU makes the calls, a view draws the groups of the layers it has). The passes:
//   FRAME_CLEAR  once in a frame: all of u_out is free
//   CLEAR     clears the counters of the variants
//   BIN_ARGS  the groups of the dispatches of COUNT and SCATTER, from the number of selected clusters (u_args + 48)
//   COUNT     one thread for each selected cluster or mesh: counts it in its variant (the ones of groups that are not drawn in the view are not counted)
//   SCAN      one group of threads: the first slot of each variant (a variant is a range of u_out), and its command; variants that do not fit into u_out are cut
//   SCATTER   one thread for each selected cluster or mesh: writes its instance data (96 bytes, the layout of the MESH_INSTANCE variant of surface shaders,
//             the same as nanite.hlsl writes) to its place in the variant
// An entry of the list of selected clusters is (slot, mesh of the mesh table, cluster, group of instanced models + 1), the cluster of a mesh without clusters is
// 0xffffffff. Its instance data has no cluster and no LOD fade (the position and scale words are 0), the scale word has the offset of the bones of skinned meshes.
// A cluster is found by the draw by its instance: the cluster is in the scale.w, the mesh of the mesh table of the scene in the prev_pos.w
// (vertex_pulling.hlsli, CLUSTER_MESH), the material index is in the prev_scale.w.
//
// u_counters (rw): [2] the first free slot of u_out, which is shared by all the views of a frame (FRAME_CLEAR empties it when the frame is set up, SCAN takes
//              the slots of its variants from it), from 64: u32 counts, cursors and first slots of MAX_VARIANTS variants
// u_commands (rw): a draw command for each variant, 24 bytes: draw id (the first slot), index count, instance count, first index, base vertex, base instance.
//                  The commands of a view start at the byte u_command_base, each view that is drawn has its own, since the draws are executed after the
//                  passes of all the views. The command of a variant is the one number `group_base[group] + index in the group` of them.
// u_meshes: the mesh table of the scene (GPUSceneMesh, 64 bytes), its variant, index in the model and material index are at 52
// u_variants: 16 bytes for each variant, x = group | index in the group << 16, first index, base vertex, index count
// u_group_base: from u_group_base_offset, u32 for each draw group: its first command in this view, 0xffffffff if the group is not drawn in it
// u_overrides: the materials of instances that have their own (GPUScene::setOverrides): u_override_count entries of 16 bytes sorted by slot and then mesh: slot,
//              index of the mesh in its model, material index, variant. Only slots with the flag HAS_OVERRIDE (16, the flags of the instance) are searched.

#define MAX_VARIANTS 8192
#define SCENE_POS_UNIT (1.0 / 1024.0)

cbuffer UniformData : register(b4) {
	int3 u_cam_p;				// the camera in the fixed point of the GPU scene
	uint u_selected_capacity;	// entries of the list of selected clusters
	float3 u_cam_frac;
	uint u_out_capacity;		// instances that fit into u_out
	uint u_selected;			// rw bindless, nanite.hlsl: [0] number of selected clusters, from 16 entries (slot, mesh, cluster, 0)
	uint u_instances;			// bindless, the instances of the GPU scene, 96 bytes
	uint u_counters;				// rw bindless
	uint u_commands;			// rw bindless
	uint u_meshes;				// bindless
	uint u_variants;			// bindless
	uint u_variant_count;
	uint u_group_base;			// bindless
	uint u_group_base_offset;	// in bytes
	uint u_out;					// rw bindless, 96 bytes for each instance of the draws
	uint u_args;				// rw bindless, dispatch arguments
	uint u_groups;				// bindless, GPUSceneGroup[] (gpu_scene.h), for the clusters of instanced models
	uint u_bone_table;			// bindless, u32 for each slot: the byte offset of its bones in the bones buffer (0: none), 0 if there are no bones
	uint u_overrides;			// bindless
	uint u_override_count;
	uint u_command_base;		// byte offset of the commands of the view in u_commands
	uint u_bone_table_offset;	// in bytes
	uint3 u_padding2;
};

// the variant and the material index of a mesh of an instance, they are not changed if it has no override (binary search), `mesh` is the index in its model
void findOverride(uint slot, uint mesh, inout uint variant, inout uint material) {
	ByteAddressBuffer overrides = bindless_buffers[u_overrides];
	int low = 0;
	int high = (int)u_override_count - 1;
	while (low <= high) {
		const int middle = (low + high) >> 1;
		const uint4 entry = overrides.Load4(middle * 16);
		if (entry.x == slot && entry.y == mesh) {
			material = entry.z;
			variant = entry.w;
			return;
		}
		if (entry.x < slot || (entry.x == slot && entry.y < mesh)) low = middle + 1;
		else high = middle - 1;
	}
}

#define COUNTS_AT 64
#define CURSORS_AT (64 + MAX_VARIANTS * 4)
#define BASES_AT (64 + MAX_VARIANTS * 8)

#ifdef SCAN
	#define GROUP_SIZE 1024
	groupshared uint g_sums[GROUP_SIZE];
#else
	#define GROUP_SIZE 64
#endif

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	#ifdef FRAME_CLEAR
		if (thread_id.x == 0) bindless_rw_buffers[u_counters].Store(8, 0);
		return;
	#endif
	RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
	RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
	const uint selected_count = min(selected.Load(0), u_selected_capacity);

	#if defined CLEAR
		for (uint i = thread_id.x; i < u_variant_count; i += 64) {
			counters.Store(COUNTS_AT + i * 4, 0);
			counters.Store(CURSORS_AT + i * 4, 0);
			counters.Store(BASES_AT + i * 4, 0);
		}
	#elif defined BIN_ARGS
		if (thread_id.x == 0) {
			const uint groups = (selected_count + 63) / 64;
			bindless_rw_buffers[u_args].Store3(48, uint3(min(groups, 65535u), (groups + 65534) / 65535, 1));
		}
	#elif defined SCAN
		// every thread takes a range of variants: the sums of the ranges are scanned by the first thread, then each thread gives out the slots of its range
		const uint chunk = (u_variant_count + GROUP_SIZE - 1) / GROUP_SIZE;
		const uint first = thread_id.x * chunk;
		const uint end = min(first + chunk, u_variant_count);
		ByteAddressBuffer variants = bindless_buffers[u_variants];
		ByteAddressBuffer group_bases = bindless_buffers[u_group_base];
		uint sum = 0;
		for (uint v = first; v < end; ++v) {
			const uint group = variants.Load(v * 16) & 0xffff;
			if (group_bases.Load(u_group_base_offset + group * 4) != 0xffffffff) sum += counters.Load(COUNTS_AT + v * 4);
		}
		g_sums[thread_id.x] = sum;
		GroupMemoryBarrierWithGroupSync();
		if (thread_id.x == 0) {
			uint base = counters.Load(8);	// what the views before took
			for (uint i = 0; i < GROUP_SIZE; ++i) {
				const uint range = g_sums[i];
				g_sums[i] = base;
				base = min(base + range, u_out_capacity);	// what does not fit is cut
			}
			counters.Store(8, base);
		}
		GroupMemoryBarrierWithGroupSync();
		RWByteAddressBuffer commands = bindless_rw_buffers[u_commands];
		uint base = g_sums[thread_id.x];
		for (uint w = first; w < end; ++w) {
			const uint4 variant = variants.Load4(w * 16);
			const uint group_base = group_bases.Load(u_group_base_offset + (variant.x & 0xffff) * 4);
			if (group_base == 0xffffffff) continue;
			const uint count = counters.Load(COUNTS_AT + w * 4);
			const uint kept = min(count, u_out_capacity - base);
			counters.Store(COUNTS_AT + w * 4, kept);
			counters.Store(BASES_AT + w * 4, base);
			const uint command = u_command_base + (group_base + (variant.x >> 16)) * 24;
			commands.Store4(command, uint4(base, variant.w, kept, variant.y));	// draw id, index count, instance count, first index
			commands.Store2(command + 16, uint2(variant.z, 0));						// base vertex, base instance
			base += kept;
		}
	#else // COUNT and SCATTER
		const uint index = thread_id.y * 65535 * 64 + thread_id.x;
		if (index >= selected_count) return;
		const uint4 entry = selected.Load4(16 + index * 16);	// slot, mesh, cluster
		const uint3 mesh_info = bindless_buffers[u_meshes].Load3(entry.y * 64 + 52);	// variant, index of the mesh in its model, material index
		uint variant = mesh_info.x;
		uint material = mesh_info.z;
		// a slot of the scene can have its own material
		if (entry.w == 0 && u_override_count != 0 && (bindless_buffers[u_instances].Load(entry.x * 96 + 44) & 16) != 0) findOverride(entry.x, mesh_info.y, variant, material);
		if (variant == 0xffffffff) return;	// the mesh is not drawn
		// the group of the variant is not drawn in this view
		if (bindless_buffers[u_group_base].Load(u_group_base_offset + (bindless_buffers[u_variants].Load(variant * 16) & 0xffff) * 4) == 0xffffffff) return;
		#ifdef COUNT
			uint previous;
			counters.InterlockedAdd(COUNTS_AT + variant * 4, 1, previous);
		#else
			uint rank;
			counters.InterlockedAdd(CURSORS_AT + variant * 4, 1, rank);
			if (rank >= counters.Load(COUNTS_AT + variant * 4)) return; // did not fit
			const uint slot = counters.Load(BASES_AT + variant * 4) + rank;
			const bool plain = entry.z == 0xffffffff;
			// a mesh without clusters is drawn by a command with its first index, its vertices are found by the base vertex that the instance has (POOLED_VERTICES)
			const uint base_vertex = plain ? bindless_buffers[u_variants].Load(variant * 16 + 8) : entry.y;

			float4 rot;
			float3 position;
			uint3 scale;
			float4 prev_rot;
			float3 prev_position;
			uint3 prev_scale;
			uint bones = 0;
			if (entry.w == 0) {
				// a slot of the scene
				ByteAddressBuffer instances = bindless_buffers[u_instances];
				const uint at = entry.x * 96;
				rot = asfloat(instances.Load4(at));
				position = float3(asint(instances.Load3(at + 16)) - u_cam_p) * SCENE_POS_UNIT - u_cam_frac;
				scale = instances.Load3(at + 32);
				if (u_bone_table != 0) bones = bindless_buffers[u_bone_table].Load(u_bone_table_offset + entry.x * 4);
				prev_rot = asfloat(instances.Load4(at + 48));
				prev_position = float3(asint(instances.Load3(at + 64)) - u_cam_p) * SCENE_POS_UNIT - u_cam_frac;
				prev_scale = instances.Load3(at + 80);
			}
			else {
				// an instance of an instanced model, entry.x is its number in the group entry.w - 1: 32 bytes, rotation xyz and lod, position and uniform scale
				ByteAddressBuffer groups = bindless_buffers[u_groups];
				const uint4 origin = groups.Load4((entry.w - 1) * 32);
				ByteAddressBuffer data = bindless_buffers[groups.Load((entry.w - 1) * 32 + 16)];
				const float4 rot_lod = asfloat(data.Load4(entry.x * 32));
				const float4 pos_scale = asfloat(data.Load4(entry.x * 32 + 16));
				rot = float4(rot_lod.xyz, sqrt(saturate(1 - dot(rot_lod.xyz, rot_lod.xyz))));
				position = float3(asint(origin.xyz) - u_cam_p) * SCENE_POS_UNIT - u_cam_frac + pos_scale.xyz;
				scale = asuint(pos_scale.www);
				// not moving
				prev_rot = rot;
				prev_position = position;
				prev_scale = scale;
			}

			RWByteAddressBuffer output = bindless_rw_buffers[u_out];
			const uint o = slot * 96;
			output.Store4(o, asuint(rot));
			// the cluster as float is only read by CLUSTER_DEBUG, plain meshes have no LOD fade (the fourth word of both positions)
			output.Store4(o + 16, asuint(float4(position, plain ? 0.0 : (float)entry.z)));
			output.Store4(o + 32, uint4(scale, plain ? bones : entry.z));	// plain: the offset of the bones (skinned meshes)
			output.Store4(o + 48, asuint(prev_rot));
			output.Store4(o + 64, uint4(asuint(prev_position), base_vertex));		// the mesh of the mesh table, or the base vertex
			output.Store4(o + 80, uint4(prev_scale, material));	// the material index
		#endif
	#endif
}
