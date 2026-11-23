#include "engine/shaders/common.hlsli"
#include "engine/shaders/gpu_scene_layout.hlsli"

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
// An entry of the list of selected clusters is (slot, mesh of the mesh table, cluster, instance), the cluster of a mesh without clusters is 0xffffffff and its mesh is
// the index of the mesh in its model (a model without clusters has no table of meshes). The instance is
// 0 for a model instance that is drawn itself, otherwise the index + 1 of an instance of a chunk of an instanced model in the buffer of the instances of the model instance. Its instance data has no cluster and no LOD fade (the position and scale words are 0), the scale word has the offset of the bones of skinned meshes.
// A cluster is found by the draw by its instance: the cluster is in the scale.w, the mesh of the mesh table of the scene in the prev_pos.w
// (vertex_pulling.hlsli, CLUSTER_MESH), the material index is in the prev_scale.w.
//
// u_counters (rw): [2] the first free slot of u_out, which is shared by all the views of a frame (FRAME_CLEAR empties it when the frame is set up, SCAN takes
//              the slots of its variants from it), from 64: u32 counts, cursors and first slots of MAX_VARIANTS variants
// u_commands (rw): a draw command for each variant, 24 bytes: draw id (the first slot), index count, instance count, first index, base vertex, base instance.
//                  The commands of a view start at the byte u_command_base, each view that is drawn has its own, since the draws are executed after the
//                  passes of all the views. The command of a variant is the one number `group_base[group] + index in the group` of them.
// u_meshes: the mesh table of the scene (GPUClusterMesh, 32 bytes), its entry in the default set of its model (u_sets) and its index in the model are at 24
// u_lod_models: the table of models without clusters (GPULodModel, 40 bytes), the first entry of the default set of the model is its first word
// u_variants: 16 bytes for each variant, x = group | index in the group << 16 (0xffffffff: a free id, it has no command), first index, base vertex, index count
// u_group_base: from u_group_base_offset, u32 for each draw group: its first command in this view, 0xffffffff if the group is not drawn in it
// u_sets: the material sets, 8 bytes for each mesh of a model, variant and material index. Every model has a default set with its own materials (the mesh table
//         has the entry of each mesh), instances that have their own materials (GPUScene::setOverrides) have another set. The set of an instance starts at the
//         entry that is in the word of its model instance at 76 (0xffffffff: the materials of its model), the entry of a mesh is `first + index of the mesh in its model`.
// The previous transform of a model instance (for motion vectors) is valid only if the model instance was moved in this frame (its word at 92 is u_frame), see gpu_scene.hlsl.

#define MAX_VARIANTS 8192

cbuffer UniformData : register(b4) {
	int3 u_cam_p;				// the camera in the fixed point of the GPU scene
	uint u_selected_capacity;	// entries of the list of selected clusters
	float3 u_cam_frac;
	uint u_out_capacity;		// instances that fit into u_out
	uint u_selected;			// rw bindless, nanite.hlsl: [0] number of selected clusters, from 16 entries (slot, mesh, cluster, 0)
	uint u_instances;			// bindless, the model instances of the GPU scene, 96 bytes
	uint u_counters;				// rw bindless
	uint u_commands;			// rw bindless
	uint u_meshes;				// bindless
	uint u_variants;			// bindless
	uint u_variant_count;
	uint u_group_base;			// bindless
	uint u_group_base_offset;	// in bytes
	uint u_out;					// rw bindless, 96 bytes for each instance of the draws
	uint u_args;				// rw bindless, dispatch arguments
	uint u_frame;				// the number of the frame, see GPUSceneInstance::moved_frame
	uint u_bone_table;			// bindless, u32 for each slot: the byte offset of its bones in the bones buffer (0: none), 0 if there are no bones
	uint u_sets;				// bindless
	uint u_lod_models;		// bindless
	uint u_command_base;		// byte offset of the commands of the view in u_commands
	uint u_bone_table_offset;	// in bytes
	uint u_chunks;				// bindless, the chunks of instanced models (GPUSceneChunk, 32 bytes)
	uint2 u_padding2;
};

// u_counters: [OUT_CURSOR_AT] the first free slot of u_out, from COUNTS_AT the counts, cursors and bases of the variants, MAX_VARIANTS u32 each
#define OUT_CURSOR_AT 8
#define COUNTS_AT 64
#define CURSORS_AT (COUNTS_AT + MAX_VARIANTS * 4)
#define BASES_AT (COUNTS_AT + MAX_VARIANTS * 8)

// u_commands, a command of an indirect draw: draw id, index count, instance count, first index, base vertex, base instance
#define COMMAND_STRIDE 24
#define COMMAND_BASE_VERTEX 16

#ifdef SCAN
	#define GROUP_SIZE 1024		// threads that scan the variants
	groupshared uint g_sums[GROUP_SIZE];
#else
	#define GROUP_SIZE 64
#endif
#define SCAN_RANGES GROUP_SIZE

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	#ifdef FRAME_CLEAR
		if (thread_id.x == 0) bindless_rw_buffers[u_counters].Store(OUT_CURSOR_AT, 0);
		return;
	#endif
	RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
	RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
	const uint selected_count = min(selected.Load(SELECTED_COUNT), u_selected_capacity);

	#if defined CLEAR
		for (uint i = thread_id.x; i < u_variant_count; i += GROUP_SIZE) {
			counters.Store(COUNTS_AT + i * 4, 0);
			counters.Store(CURSORS_AT + i * 4, 0);
			counters.Store(BASES_AT + i * 4, 0);
		}
	#elif defined BIN_ARGS
		if (thread_id.x == 0) {
			const uint groups = (selected_count + GROUP_SIZE - 1) / GROUP_SIZE;
			bindless_rw_buffers[u_args].Store3(ARGS_BIN, uint3(min(groups, MAX_GROUPS_X), (groups + MAX_GROUPS_X - 1) / MAX_GROUPS_X, 1));
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
			const uint x = variants.Load(v * VARIANT_STRIDE);
			if (x == NO_VARIANT) continue;	// a free id
			if (group_bases.Load(u_group_base_offset + (x & VARIANT_GROUP_MASK) * 4) != NO_COMMAND_GROUP) sum += counters.Load(COUNTS_AT + v * 4);
		}
		g_sums[thread_id.x] = sum;
		GroupMemoryBarrierWithGroupSync();
		if (thread_id.x == 0) {
			uint base = counters.Load(OUT_CURSOR_AT);	// what the views before took
			for (uint i = 0; i < GROUP_SIZE; ++i) {
				const uint range = g_sums[i];
				g_sums[i] = base;
				base = min(base + range, u_out_capacity);	// what does not fit is cut
			}
			counters.Store(OUT_CURSOR_AT, base);
		}
		GroupMemoryBarrierWithGroupSync();
		RWByteAddressBuffer commands = bindless_rw_buffers[u_commands];
		uint base = g_sums[thread_id.x];
		for (uint w = first; w < end; ++w) {
			const uint4 variant = variants.Load4(w * VARIANT_STRIDE);
			if (variant.x == NO_VARIANT) continue;	// a free id
			const uint group_base = group_bases.Load(u_group_base_offset + (variant.x & VARIANT_GROUP_MASK) * 4);
			if (group_base == NO_COMMAND_GROUP) continue;
			const uint count = counters.Load(COUNTS_AT + w * 4);
			const uint kept = min(count, u_out_capacity - base);
			counters.Store(COUNTS_AT + w * 4, kept);
			counters.Store(BASES_AT + w * 4, base);
			const uint command = u_command_base + (group_base + (variant.x >> VARIANT_INDEX_SHIFT)) * COMMAND_STRIDE;
			commands.Store4(command, uint4(base, variant.w, kept, variant.y));	// draw id, index count, instance count, first index
			commands.Store2(command + COMMAND_BASE_VERTEX, uint2(variant.z, 0));						// base vertex, base instance
			base += kept;
		}
	#else // COUNT and SCATTER
		const uint index = thread_id.y * MAX_GROUPS_X * GROUP_SIZE + thread_id.x;
		if (index >= selected_count) return;
		const uint4 entry = selected.Load4(SELECTED_HEADER + index * SELECTED_STRIDE);	// slot, mesh, cluster, instance
		// the entries of an instance of a chunk (instance is not 0) have the chunk instead of the model instance in their slot, it has no material set (the materials of its model)
		const bool in_chunk = entry.w != 0;
		const uint model_id = in_chunk ? bindless_buffers[u_chunks].Load(entry.x * CHUNK_STRIDE + CHUNK_MODEL) : bindless_buffers[u_instances].Load(entry.x * INSTANCE_STRIDE + INSTANCE_MODEL);
		uint2 mesh_info;	// its entry in the default set of its model, index of the mesh in its model
		if (entry.z == NO_CLUSTER) {
			// a mesh without clusters: the entry is the mesh of the model
			mesh_info = uint2(bindless_buffers[u_lod_models].Load((model_id & ~MODEL_LOD_BIT) * LOD_MODEL_STRIDE + LOD_MODEL_FIRST_SET) + entry.y, entry.y);
		}
		else {
			mesh_info = bindless_buffers[u_meshes].Load2(entry.y * CLUSTER_MESH_STRIDE + CLUSTER_MESH_SET);
		}
		uint set_entry = mesh_info.x;
		// a model instance can have its own materials, its material set has the entry of every mesh of the model
		const uint set = in_chunk ? NO_MATERIAL_SET : bindless_buffers[u_instances].Load(entry.x * INSTANCE_STRIDE + INSTANCE_MATERIAL_SET);
		if (set != NO_MATERIAL_SET) set_entry = set + mesh_info.y;
		const uint2 set_data = bindless_buffers[u_sets].Load2(set_entry * SET_ENTRY_STRIDE);
		const uint variant = set_data.x;	// NO_VARIANT: the mesh is not drawn
		const uint material = set_data.y;
		if (variant == NO_VARIANT) return;	// the mesh is not drawn
		// the group of the variant is not drawn in this view
		if (bindless_buffers[u_group_base].Load(u_group_base_offset + (bindless_buffers[u_variants].Load(variant * VARIANT_STRIDE) & VARIANT_GROUP_MASK) * 4) == NO_COMMAND_GROUP) return;
		#ifdef COUNT
			uint previous;
			counters.InterlockedAdd(COUNTS_AT + variant * 4, 1, previous);
		#else
			uint rank;
			counters.InterlockedAdd(CURSORS_AT + variant * 4, 1, rank);
			if (rank >= counters.Load(COUNTS_AT + variant * 4)) return; // did not fit
			const uint slot = counters.Load(BASES_AT + variant * 4) + rank;
			const bool lod = entry.z == NO_CLUSTER;
			// a mesh without clusters is drawn by a command with its first index, its vertices are found by the base vertex that the instance has (POOLED_VERTICES)
			const uint base_vertex = lod ? bindless_buffers[u_variants].Load(variant * VARIANT_STRIDE + VARIANT_BASE_VERTEX) : entry.y;

			uint bones = 0;
			if (lod && entry.w == 0 && u_bone_table != 0) {
				bones = bindless_buffers[u_bone_table].Load(u_bone_table_offset + entry.x * 4);
			}

			RWByteAddressBuffer output = bindless_rw_buffers[u_out];
			const uint o = slot * DRAW_INSTANCE_STRIDE;
			output.Store4(o + DRAW_INSTANCE_SLOT, uint4(entry.x, base_vertex, lod ? bones : entry.z, entry.w));
			output.Store(o + DRAW_INSTANCE_MATERIAL, material);
		#endif
	#endif
}
