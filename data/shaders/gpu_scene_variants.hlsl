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
//   SCATTER   one thread for each selected cluster or mesh: writes its 32-byte draw instance entry (DRAW_INSTANCE_STRIDE, see gpu_scene_layout.hlsli)
//             to its place in the variant; vertex shaders pull transforms directly from GPUScene::instances / GPUSceneChunks::chunks.
// An entry of the list of selected clusters is (slot, mesh of the mesh table, cluster | (depth_bin << 27), instance); the cluster of a mesh without clusters is
// NO_CLUSTER (0xffffffff) and its mesh is the index of the mesh in its model (a model without clusters has no table of meshes). The instance is
// 0 for a model instance that is drawn itself, otherwise the index + 1 of an instance of a chunk of an instanced model in the buffer of the instances of the model instance.
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

#ifdef EMIT_CLUSTER_COMMANDS
cbuffer EmitClusterCommandsUniforms : register(b4) {
	uint u_emit_out;				// bindless, m_scene_out (32 bytes per instance)
	uint u_emit_commands;			// bindless, m_scene_commands (24 bytes per variant: [0] kept, [4] base in u_emit_out)
	uint u_emit_command_offset;		// byte offset of this cluster draw group's entry in u_emit_commands
	uint u_emit_cluster_commands;	// rw bindless, per-cluster 24-byte DrawIndexedIndirect scratch buffer (written from offset 0)
};
#else
cbuffer UniformData : register(b4) {
	uint u_selected_capacity;	// entries of the list of selected clusters
	uint u_out_capacity;		// instances that fit into u_out
	uint u_selected;			// rw bindless, nanite.hlsl: [0] number of selected clusters, from 16 entries (slot, mesh, cluster, 0)
	uint u_instances;			// bindless, the model instances of the GPU scene, 96 bytes
	uint u_counters;			// rw bindless
	uint u_commands;			// rw bindless
	uint u_meshes;				// bindless
	uint u_variants;			// bindless
	uint u_variant_count;
	uint u_group_base;			// bindless
	uint u_group_base_offset;	// in bytes
	uint u_out;					// rw bindless, 32 bytes for each instance of the draws
	uint u_args;				// rw bindless, dispatch arguments
	uint u_bone_table;			// bindless, u32 for each slot: the byte offset of its bones in the bones buffer (0: none), 0 if there are no bones
	uint u_sets;				// bindless
	uint u_lod_models;			// bindless
	uint u_command_base;		// byte offset of the commands of the view in u_commands
	uint u_bone_table_offset;	// in bytes
	uint u_chunks;				// bindless, the chunks of instanced models (GPUSceneChunk, 32 bytes)
	uint u_cluster_data;		// bindless, buffer of cluster data (ClusterStreamer::getDataBuffer)
	uint u_cluster_cmd_capacity;// max per-cluster indirect commands per cluster draw group (ClusterStreamer::getCommandsCapacity)
	uint u_depth_bin_groups;	// number of cluster draw groups to front-to-back depth-bin (0 to disable, e.g. shadow views)
	uint u_scratch;				// rw bindless, per-selected uint2(variant_or_bk, material) cached by COUNT for SCATTER
	uint u_padding0;
};
#endif

// u_commands, a command of an indirect draw: draw id, index count, instance count, first index, base vertex, base instance
#define COMMAND_STRIDE 24
#define COMMAND_BASE_VERTEX 16

#ifdef SCAN
	#define GROUP_SIZE 1024		// threads that scan the variants
	groupshared uint g_sums[GROUP_SIZE];
	groupshared uint g_wave_sums[GROUP_SIZE / 4];	// the sum of each wave, then its first slot (there are at most GROUP_SIZE / 4 waves)
#else
	#define GROUP_SIZE 64
	#if !defined EMIT_CLUSTER_COMMANDS && !defined FRAME_CLEAR && !defined CLEAR && !defined BIN_ARGS
		groupshared uint g_bin_counts[DEPTH_BINS];
		groupshared uint g_bin_bases[DEPTH_BINS];
		groupshared uint g_common_group;
	#endif
#endif
#define SCAN_RANGES GROUP_SIZE

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID) {
	#ifdef EMIT_CLUSTER_COMMANDS
		const uint i = thread_id.y * MAX_GROUPS_X * GROUP_SIZE + thread_id.x;
		const uint2 count_base = bindless_buffers[u_emit_commands].Load2(u_emit_command_offset);
		if (i >= count_base.x) return;
		const uint slot = count_base.y + i;
		const uint4 cd = bindless_buffers[u_emit_out].Load4(slot * DRAW_INSTANCE_STRIDE + DRAW_INSTANCE_CLUSTER_DATA);
		// 24-byte command: u_draw_id = slot, DrawIndexedIndirect directly on tightly packed u16 indices in u_cluster_data
		RWByteAddressBuffer cluster_cmds = bindless_rw_buffers[u_emit_cluster_commands];
		cluster_cmds.Store4(i * 24, uint4(slot, cd.w, 1u, cd.y >> 1));
		cluster_cmds.Store2(i * 24 + 16, uint2(0u, 0u));
		return;
	#elif defined FRAME_CLEAR
		if (thread_id.x == 0) bindless_rw_buffers[u_counters].Store(COUNTERS_OUT_CURSOR_AT, 0);
		return;
	#else
	RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
	RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
	const uint selected_count = min(selected.Load(SELECTED_COUNT), u_selected_capacity);

	#if defined CLEAR
		for (uint i = thread_id.x; i < u_variant_count; i += GROUP_SIZE) {
			counters.Store(COUNTERS_COUNTS_AT + i * 4, 0);
			counters.Store(COUNTERS_CURSORS_AT + i * 4, 0);
			counters.Store(COUNTERS_BASES_AT + i * 4, 0);
		}
		const uint bin_count = min(u_depth_bin_groups, (uint)MAX_DEPTH_BIN_GROUPS) * DEPTH_BINS;
		for (uint b = thread_id.x; b < bin_count; b += GROUP_SIZE) {
			counters.Store(COUNTERS_BIN_COUNTS_AT + b * 4, 0);
			counters.Store(COUNTERS_BIN_CURSORS_AT + b * 4, 0);
		}
	#elif defined BIN_ARGS
		if (thread_id.x == 0) {
			const uint groups = (selected_count + GROUP_SIZE - 1) / GROUP_SIZE;
			bindless_rw_buffers[u_args].Store3(ARGS_BIN, dispatchGroups(groups));
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
			if (group_bases.Load(u_group_base_offset + (x & VARIANT_GROUP_MASK) * 4) != NO_COMMAND_GROUP) sum += counters.Load(COUNTERS_COUNTS_AT + v * 4);
		}
		// The first slot of a range is the cursor + the sums of the ranges before it, cut at the capacity (the cut of the serial loop min(base + range, capacity)
		// gives the same, since the ranges are not negative): a prefix sum in each wave, then one of the sums of the waves.
		const uint lanes = WaveGetLaneCount();
		const uint wave = thread_id.x / lanes;
		const uint in_wave = WavePrefixSum(sum);
		const uint wave_sum = WaveActiveSum(sum);
		if (WaveIsFirstLane()) g_wave_sums[wave] = wave_sum;
		GroupMemoryBarrierWithGroupSync();
		const uint wave_count = GROUP_SIZE / lanes;
		if (thread_id.x < lanes) {
			// the first wave scans the sums of the waves, lanes at a time
			uint carry = counters.Load(COUNTERS_OUT_CURSOR_AT);	// what the views before took
			for (uint w0 = 0; w0 < wave_count; w0 += lanes) {
				const uint w = w0 + thread_id.x;
				const uint s = w < wave_count ? g_wave_sums[w] : 0;
				const uint first_of_wave = carry + WavePrefixSum(s);
				carry += WaveActiveSum(s);
				if (w < wave_count) g_wave_sums[w] = first_of_wave;
			}
			if (thread_id.x == 0) counters.Store(COUNTERS_OUT_CURSOR_AT, min(carry, u_out_capacity));	// what does not fit is cut
		}
		GroupMemoryBarrierWithGroupSync();
		g_sums[thread_id.x] = min(g_wave_sums[wave] + in_wave, u_out_capacity);
		RWByteAddressBuffer commands = bindless_rw_buffers[u_commands];
		uint base = g_sums[thread_id.x];
		for (uint w = first; w < end; ++w) {
			const uint4 variant = variants.Load4(w * VARIANT_STRIDE);
			if (variant.x == NO_VARIANT) continue;	// a free id
			const uint group_info = group_bases.Load(u_group_base_offset + (variant.x & VARIANT_GROUP_MASK) * 4);
			if (group_info == NO_COMMAND_GROUP) continue;
			const uint group_base = group_info & GROUP_BASE_COMMAND_MASK;
			const uint count = counters.Load(COUNTERS_COUNTS_AT + w * 4);
			const uint command = u_command_base + (group_base + (variant.x >> VARIANT_INDEX_SHIFT)) * COMMAND_STRIDE;
			if ((group_info & GROUP_BASE_CLUSTER_BIT) != 0) {
				const uint cluster_group_idx = (group_info >> GROUP_BASE_CLUSTER_IDX_SHIFT) & GROUP_BASE_CLUSTER_IDX_MASK;
				const uint kept = min(min(count, u_out_capacity - base), u_cluster_cmd_capacity);
				counters.Store(COUNTERS_COUNTS_AT + w * 4, kept);
				counters.Store(COUNTERS_BASES_AT + w * 4, base);
				if (cluster_group_idx < u_depth_bin_groups && cluster_group_idx < MAX_DEPTH_BIN_GROUPS) {
					uint bin_off = base;
					uint rem = kept;
					for (uint b = 0; b < DEPTH_BINS; ++b) {
						const uint bk = cluster_group_idx * DEPTH_BINS + b;
						const uint bc = min(counters.Load(COUNTERS_BIN_COUNTS_AT + bk * 4), rem);
						counters.Store(COUNTERS_BIN_COUNTS_AT + bk * 4, bc);
						counters.Store(COUNTERS_BIN_OFFSETS_AT + bk * 4, bin_off);
						bin_off += bc;
						rem -= bc;
					}
				}
				// 24-byte slot in u_commands: [0] kept (pCountBuffer for ExecuteIndirect), [4] base in u_out, [8..16] DispatchIndirect args for EMIT_CLUSTER_COMMANDS
				const uint groups = (kept + 63) / 64;
				commands.Store4(command, uint4(kept, base, min(groups, MAX_GROUPS_X), (groups + MAX_GROUPS_X - 1) / MAX_GROUPS_X));
				commands.Store(command + 16, 1u);
				base += kept;
			}
			else {
				const uint kept = min(count, u_out_capacity - base);
				counters.Store(COUNTERS_COUNTS_AT + w * 4, kept);
				counters.Store(COUNTERS_BASES_AT + w * 4, base);
				commands.Store4(command, uint4(base, variant.w, kept, variant.y));	// draw id, index count, instance count, first index
				commands.Store2(command + COMMAND_BASE_VERTEX, uint2(variant.z, 0));						// base vertex, base instance
				base += kept;
			}
		}
	#elif defined COUNT
		const uint gtid = group_thread_id.x;
		const uint index = thread_id.y * MAX_GROUPS_X * GROUP_SIZE + thread_id.x;
		if (gtid < DEPTH_BINS) g_bin_counts[gtid] = 0;
		if (gtid == 0) g_common_group = 0xffffffffu;
		GroupMemoryBarrierWithGroupSync();

		bool use_depth_bin = false;
		uint cluster_group_idx = 0;
		uint bin = 0;
		if (index < selected_count) {
			RWByteAddressBuffer scratch = bindless_rw_buffers[u_scratch];
			const uint4 entry = selected.Load4(SELECTED_HEADER + index * SELECTED_STRIDE);	// slot, mesh, cluster, instance
			const bool in_chunk = entry.w != 0;
			const uint model_id = in_chunk ? bindless_buffers[u_chunks].Load(entry.x * CHUNK_STRIDE + CHUNK_MODEL) : bindless_buffers[u_instances].Load(entry.x * INSTANCE_STRIDE + INSTANCE_MODEL);
			uint2 mesh_info;
			if (entry.z == NO_CLUSTER) {
				mesh_info = uint2(bindless_buffers[u_lod_models].Load((model_id & ~MODEL_LOD_BIT) * LOD_MODEL_STRIDE + LOD_MODEL_FIRST_SET) + entry.y, entry.y);
			}
			else {
				const uint cluster_idx = entry.z & SELECTED_CLUSTER_MASK;
				const uint clusters_offset = bindless_buffers[u_meshes].Load(entry.y * CLUSTER_MESH_STRIDE);
				const uint mat_idx = bindless_buffers[u_cluster_data].Load(clusters_offset + cluster_idx * CLUSTER_STRIDE + CLUSTER_INDEX_COUNT) >> CLUSTER_MATERIAL_SHIFT;
				const uint default_first = bindless_buffers[u_meshes].Load(entry.y * CLUSTER_MESH_STRIDE + CLUSTER_MESH_SET);
				mesh_info = uint2(default_first + mat_idx, mat_idx);
			}
			uint set_entry = mesh_info.x;
			const uint set = in_chunk ? NO_MATERIAL_SET : bindless_buffers[u_instances].Load(entry.x * INSTANCE_STRIDE + INSTANCE_MATERIAL_SET);
			if (set != NO_MATERIAL_SET) set_entry = set + mesh_info.y;
			const uint2 set_data = bindless_buffers[u_sets].Load2(set_entry * SET_ENTRY_STRIDE);
			const uint variant = set_data.x;
			const uint material = set_data.y;
			if (variant == NO_VARIANT) {
				scratch.Store2(index * 8, uint2(0xffffffffu, 0));
			}
			else {
				const uint group_info = bindless_buffers[u_group_base].Load(u_group_base_offset + (bindless_buffers[u_variants].Load(variant * VARIANT_STRIDE) & VARIANT_GROUP_MASK) * 4);
				if (group_info == NO_COMMAND_GROUP) {
					scratch.Store2(index * 8, uint2(0xffffffffu, 0));
				}
				else {
					const bool is_cluster_group = (group_info & GROUP_BASE_CLUSTER_BIT) != 0;
					cluster_group_idx = (group_info >> GROUP_BASE_CLUSTER_IDX_SHIFT) & GROUP_BASE_CLUSTER_IDX_MASK;
					const bool lod = entry.z == NO_CLUSTER;
					bin = entry.z >> SELECTED_DEPTH_BIN_SHIFT;
					use_depth_bin = is_cluster_group && !lod && cluster_group_idx < u_depth_bin_groups && cluster_group_idx < MAX_DEPTH_BIN_GROUPS;
					waveIncrement(counters, COUNTERS_COUNTS_AT + variant * 4);
					if (use_depth_bin) {
						const uint bk = cluster_group_idx * DEPTH_BINS + bin;
						scratch.Store2(index * 8, uint2(bk | 0x80000000u, material));
						InterlockedMin(g_common_group, cluster_group_idx);
					}
					else {
						scratch.Store2(index * 8, uint2(variant, material));
					}
				}
			}
		}
		GroupMemoryBarrierWithGroupSync();
		const uint cg = g_common_group;
		if (use_depth_bin) {
			if (cluster_group_idx == cg) {
				InterlockedAdd(g_bin_counts[bin], 1);
			}
			else {
				waveIncrement(counters, COUNTERS_BIN_COUNTS_AT + (cluster_group_idx * DEPTH_BINS + bin) * 4);
			}
		}
		GroupMemoryBarrierWithGroupSync();
		if (cg != 0xffffffffu && gtid < DEPTH_BINS) {
			const uint c = g_bin_counts[gtid];
			if (c > 0) counters.InterlockedAdd(COUNTERS_BIN_COUNTS_AT + (cg * DEPTH_BINS + gtid) * 4, c);
		}
	#else // SCATTER
		const uint gtid = group_thread_id.x;
		const uint index = thread_id.y * MAX_GROUPS_X * GROUP_SIZE + thread_id.x;
		if (gtid < DEPTH_BINS) g_bin_counts[gtid] = 0;
		if (gtid == 0) g_common_group = 0xffffffffu;
		GroupMemoryBarrierWithGroupSync();

		bool valid = false;
		bool use_depth_bin = false;
		uint key = 0;
		uint material = 0;
		uint cg_idx = 0;
		uint bin = 0;
		if (index < selected_count) {
			const uint2 cached = bindless_rw_buffers[u_scratch].Load2(index * 8);
			if (cached.x != 0xffffffffu) {
				valid = true;
				use_depth_bin = (cached.x & 0x80000000u) != 0;
				key = cached.x & 0x7fffffffu;
				material = cached.y;
				if (use_depth_bin) {
					cg_idx = key / DEPTH_BINS;
					bin = key % DEPTH_BINS;
					InterlockedMin(g_common_group, cg_idx);
				}
			}
		}
		GroupMemoryBarrierWithGroupSync();
		const uint cg = g_common_group;
		uint local_rank = 0;
		if (valid && use_depth_bin && cg_idx == cg) {
			InterlockedAdd(g_bin_counts[bin], 1, local_rank);
		}
		GroupMemoryBarrierWithGroupSync();
		if (cg != 0xffffffffu && gtid < DEPTH_BINS) {
			const uint c = g_bin_counts[gtid];
			if (c > 0) {
				uint base;
				counters.InterlockedAdd(COUNTERS_BIN_CURSORS_AT + (cg * DEPTH_BINS + gtid) * 4, c, base);
				g_bin_bases[gtid] = base;
			}
		}
		GroupMemoryBarrierWithGroupSync();

		if (!valid) return;
		uint slot;
		if (use_depth_bin) {
			uint bin_rank;
			if (cg_idx == cg) {
				bin_rank = g_bin_bases[bin] + local_rank;
			}
			else {
				bin_rank = waveIncrement(counters, COUNTERS_BIN_CURSORS_AT + key * 4);
			}
			if (bin_rank >= counters.Load(COUNTERS_BIN_COUNTS_AT + key * 4)) return; // did not fit
			slot = counters.Load(COUNTERS_BIN_OFFSETS_AT + key * 4) + bin_rank;
		}
		else {
			const uint rank = waveIncrement(counters, COUNTERS_CURSORS_AT + key * 4);
			if (rank >= counters.Load(COUNTERS_COUNTS_AT + key * 4)) return; // did not fit
			slot = counters.Load(COUNTERS_BASES_AT + key * 4) + rank;
		}
		const uint4 entry = selected.Load4(SELECTED_HEADER + index * SELECTED_STRIDE);	// slot, mesh, cluster, instance
		const bool lod = entry.z == NO_CLUSTER;
		const uint cluster = entry.z & SELECTED_CLUSTER_MASK;
		RWByteAddressBuffer output = bindless_rw_buffers[u_out];
		const uint o = slot * DRAW_INSTANCE_STRIDE;
		if (lod) {
			// a mesh without clusters is drawn by a command with its first index, its vertices are found by the base vertex that the instance has (POOLED_VERTICES)
			const uint base_vertex = bindless_buffers[u_variants].Load(key * VARIANT_STRIDE + VARIANT_BASE_VERTEX);
			uint bones = 0;
			if (entry.w == 0 && u_bone_table != 0) {
				bones = bindless_buffers[u_bone_table].Load(u_bone_table_offset + entry.x * 4);
			}
			output.Store4(o + DRAW_INSTANCE_SLOT, uint4(entry.x, entry.w, material, base_vertex));
			output.Store(o + DRAW_INSTANCE_BONES, bones);
		}
		else {
			// pre-resolve cluster stream, index and header byte offsets once per visible cluster so vertex pulling skips the indirection chain
			const uint mesh = entry.y;
			ByteAddressBuffer mesh_table = bindless_buffers[u_meshes];
			const ClusterMesh cm = mesh_table.Load<ClusterMesh>(mesh * CLUSTER_MESH_STRIDE);
			const uint headers_offset = clusterHeadersOffset(cm);
			ByteAddressBuffer cluster_data = bindless_buffers[u_cluster_data];
			const uint group_id = cluster_data.Load(cm.clusters + cluster * CLUSTER_STRIDE + CLUSTER_GROUP);
			const uint4 group_address = cluster_data.Load4(cm.group_table + group_id * GROUP_STRIDE);
			const uint stream_base = group_address.x;
			const uint2 cluster_idx_range = cluster_data.Load2(cm.clusters + cluster * CLUSTER_STRIDE + CLUSTER_FIRST_INDEX);
			const uint index_base = group_address.y + cluster_idx_range.x * 2;
			const uint header_addr = headers_offset + cluster * CLUSTER_HEADER_STRIDE;
			output.Store4(o + DRAW_INSTANCE_SLOT, uint4(entry.x, entry.w, material, cluster));
			output.Store4(o + DRAW_INSTANCE_CLUSTER_DATA, uint4(stream_base, index_base, header_addr, cluster_idx_range.y & CLUSTER_INDEX_COUNT_MASK));
		}
	#endif
	#endif
}
