#include "engine/shaders/common.hlsli"

// Sorts the clusters that nanite.hlsl (SCENE_LISTS) selected into bins, and writes the instance data of the draws. A bin is what one draw can draw:
// meshes whose materials have the same program and state in the view. The passes:
//   FRAME_CLEAR  once in a frame: all of u_out is free
//   CLEAR     clears the counters of the bins
//   BIN_ARGS  the groups of the dispatches of COUNT and SCATTER, from the number of selected clusters (u_args + 48)
//   COUNT     one thread for each selected cluster: counts it in its bin (clusters of meshes that are not drawn in the view are not counted)
//   SCAN      one thread: the first slot of each bin (a bin is a range of u_out), and its draw command; bins that do not fit into u_out are cut
//   SCATTER   one thread for each selected cluster: writes its instance data (96 bytes, the layout of the DYNAMIC variant of surface shaders,
//             the same as nanite.hlsl writes) to its place in the bin
// A cluster is found by the draw by its instance: the cluster is in the scale.w, the mesh of the mesh table of the scene in the prev_pos.w
// (vertex_pulling.hlsli, CLUSTER_SCENE), the material index is in the prev_scale.w.
//
// u_bins (rw): [2] the first free slot of u_out, which is shared by all the views of a frame (FRAME_CLEAR empties it when the frame is set up, SCAN takes
//              the slots of its bins from it), from 64: u32 counts, cursors and first slots of BINS bins
// u_commands (rw): a draw command for each bin, 24 bytes: draw id (the first slot), index count, instance count, first index, base vertex, base instance.
//                  The commands of a view start at the byte u_command_base, each view that is drawn has its own, since the draws are executed after the
//                  passes of all the views
// u_mesh_view: for each mesh of the mesh table, from u_mesh_view_offset: bin (0xffffffff: not drawn) and material index, the bit 31 of it is set if clusters
//              of the mesh are culled by their cones (nanite.hlsl reads it). From u_bin_info_offset: the index count of the draw of each bin
// u_overrides: the material indices of instances that have their own materials (of the same shader as the ones of the model, so the same program draws
//              them): u_override_count entries of 12 bytes from u_overrides_offset, sorted by slot and then mesh: slot, mesh of the mesh table, material index.
//              Meshes of instances that are not here use the material index of u_mesh_view.

#define BINS 256
#define SCENE_POS_UNIT (1.0 / 1024.0)

cbuffer UniformData : register(b4) {
	int3 u_cam_p;				// the camera in the fixed point of the GPU scene
	uint u_selected_capacity;	// entries of the list of selected clusters
	float3 u_cam_frac;
	uint u_out_capacity;		// instances that fit into u_out
	uint u_selected;			// rw bindless, nanite.hlsl: [0] number of selected clusters, from 16 entries (slot, mesh, cluster, 0)
	uint u_instances;			// bindless, the instances of the GPU scene, 96 bytes
	uint u_bins;				// rw bindless
	uint u_commands;			// rw bindless
	uint u_mesh_view;			// bindless
	uint u_mesh_view_offset;
	uint u_bin_info_offset;
	uint u_bin_count;
	uint u_out;					// rw bindless, 96 bytes for each instance of the draws
	uint u_args;				// rw bindless, dispatch arguments
	uint u_groups;				// bindless, GPUSceneGroup[] (gpu_scene.h), for the clusters of instanced models
	uint u_padding1;
	uint u_overrides;			// bindless
	uint u_overrides_offset;
	uint u_override_count;
	uint u_command_base;		// byte offset of the commands of the view in u_commands
};

// the material index of a mesh of an instance, `fallback` if it has no override (binary search)
uint findMaterial(uint slot, uint mesh, uint fallback) {
	ByteAddressBuffer overrides = bindless_buffers[u_overrides];
	int low = 0;
	int high = (int)u_override_count - 1;
	while (low <= high) {
		const int middle = (low + high) >> 1;
		const uint3 entry = overrides.Load3(u_overrides_offset + middle * 12);
		if (entry.x == slot && entry.y == mesh) return entry.z;
		if (entry.x < slot || (entry.x == slot && entry.y < mesh)) low = middle + 1;
		else high = middle - 1;
	}
	return fallback;
}

#define COUNTS_AT 64
#define CURSORS_AT (64 + BINS * 4)
#define BASES_AT (64 + BINS * 8)

[numthreads(64, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	#ifdef FRAME_CLEAR
		if (thread_id.x == 0) bindless_rw_buffers[u_bins].Store(8, 0);
		return;
	#endif
	RWByteAddressBuffer bins = bindless_rw_buffers[u_bins];
	RWByteAddressBuffer selected = bindless_rw_buffers[u_selected];
	const uint selected_count = min(selected.Load(0), u_selected_capacity);

	#if defined CLEAR
		for (uint i = thread_id.x; i < BINS; i += 64) {
			bins.Store(COUNTS_AT + i * 4, 0);
			bins.Store(CURSORS_AT + i * 4, 0);
			bins.Store(BASES_AT + i * 4, 0);
		}
	#elif defined BIN_ARGS
		if (thread_id.x == 0) {
			const uint groups = (selected_count + 63) / 64;
			bindless_rw_buffers[u_args].Store3(48, uint3(min(groups, 65535u), (groups + 65534) / 65535, 1));
		}
	#elif defined SCAN
		if (thread_id.x == 0) {
			RWByteAddressBuffer commands = bindless_rw_buffers[u_commands];
			ByteAddressBuffer view = bindless_buffers[u_mesh_view];
			uint base = bins.Load(8);	// what the views before took
			for (uint b = 0; b < u_bin_count; ++b) {
				const uint count = bins.Load(COUNTS_AT + b * 4);
				const uint space = base < u_out_capacity ? u_out_capacity - base : 0;
				const uint kept = min(count, space);
				bins.Store(COUNTS_AT + b * 4, kept);
				bins.Store(BASES_AT + b * 4, base);
				commands.Store4(u_command_base + b * 24, uint4(base, view.Load(u_bin_info_offset + b * 4), kept, 0));
				commands.Store2(u_command_base + b * 24 + 16, uint2(0, 0));
				base += kept;
			}
			bins.Store(8, base);
		}
	#else // COUNT and SCATTER
		const uint index = thread_id.y * 65535 * 64 + thread_id.x;
		if (index >= selected_count) return;
		const uint4 entry = selected.Load4(16 + index * 16);	// slot, mesh, cluster
		const uint2 mesh_view = bindless_buffers[u_mesh_view].Load2(u_mesh_view_offset + entry.y * 8);	// bin, material index
		if (mesh_view.x == 0xffffffff) return;	// the mesh is not drawn in this view
		#ifdef COUNT
			uint previous;
			bins.InterlockedAdd(COUNTS_AT + mesh_view.x * 4, 1, previous);
		#else
			uint rank;
			bins.InterlockedAdd(CURSORS_AT + mesh_view.x * 4, 1, rank);
			if (rank >= bins.Load(COUNTS_AT + mesh_view.x * 4)) return; // did not fit
			const uint slot = bins.Load(BASES_AT + mesh_view.x * 4) + rank;

			float4 rot;
			float3 position;
			uint3 scale;
			float4 prev_rot;
			float3 prev_position;
			uint3 prev_scale;
			if (entry.w == 0) {
				// a slot of the scene
				ByteAddressBuffer instances = bindless_buffers[u_instances];
				const uint at = entry.x * 96;
				rot = asfloat(instances.Load4(at));
				position = float3(asint(instances.Load3(at + 16)) - u_cam_p) * SCENE_POS_UNIT - u_cam_frac;
				scale = instances.Load3(at + 32);
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
			output.Store4(o + 16, asuint(float4(position, (float)entry.z)));	// the cluster as float is only read by CLUSTER_DEBUG
			output.Store4(o + 32, uint4(scale, entry.z));
			output.Store4(o + 48, asuint(prev_rot));
			output.Store4(o + 64, uint4(asuint(prev_position), entry.y));		// the mesh of the mesh table
			output.Store4(o + 80, uint4(prev_scale, (entry.w == 0 ? findMaterial(entry.x, entry.y, mesh_view.y & 0x7fffffff) : (mesh_view.y & 0x7fffffff))));	// the material index
		#endif
	#endif
}
