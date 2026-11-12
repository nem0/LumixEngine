#include "engine/shaders/common.hlsli"

// GPU scene, see src/renderer/gpu_scene.h. Writes the entries that changed (GPUScene::Entry, 128 bytes) to the buffers of the scene.
//
// u_bounds:    16 bytes per slot, int3 position (fixed point), float radius (< 0: the slot is free)
// u_instances: 96 bytes per slot, float4 rot, int3 p, uint mesh_id, float3 scale, uint flags,
//                                 float4 prev_rot, int3 prev_p, uint material_set, float3 prev_scale, uint
//              material_set: the first entry of the material set of the instance in the material sets (gpu_scene_variants.hlsl), 0xffffffff: it has the materials of its model
//
// An entry with HAS_NEW writes the new state of the slot and moves the old one to prev_*, unless TELEPORT is set, then prev_* is the new state.
// An entry without HAS_NEW is a slot that changed in the last frame and not in this one: its previous state catches up with the current one,
// otherwise the instance would have motion vectors while it stands still.

cbuffer UniformData : register(b4) {
	uint u_entries;			// bindless, Entry[]
	uint u_entries_offset;	// bytes
	uint u_entry_count;
	uint u_bounds;			// rw bindless
	uint u_instances;		// rw bindless
	uint u_padding0;
	uint u_padding1;
	uint u_padding2;
};

#define HAS_NEW 1
#define TELEPORT 2

[numthreads(64, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	const uint index = thread_id.y * 65535 * 64 + thread_id.x;
	if (index >= u_entry_count) return;

	ByteAddressBuffer entries = bindless_buffers[u_entries];
	RWByteAddressBuffer bounds = bindless_rw_buffers[u_bounds];
	RWByteAddressBuffer instances = bindless_rw_buffers[u_instances];

	const uint e = u_entries_offset + index * 128;
	const uint slot = entries.Load(e);
	const uint flags = entries.Load(e + 4);
	const uint4 new_bounds = entries.Load4(e + 16);
	uint4 n0 = entries.Load4(e + 32);		// rot
	uint4 n1 = entries.Load4(e + 48);		// p, mesh_id
	uint4 n2 = entries.Load4(e + 64);		// scale, flags
	const uint new_set = entries.Load(e + 108);	// material set (the word after prev_p)
	const uint b = slot * 16;
	const uint o = slot * 96;

	// state of the slot before this entry
	const uint4 old_bounds = bounds.Load4(b);
	const uint4 old0 = instances.Load4(o);
	const uint4 old2 = instances.Load4(o + 32);
	const uint old_set = instances.Load(o + 76);

	if ((flags & HAS_NEW) == 0) {
		// catch up: previous = current
		instances.Store4(o + 48, old0);
		instances.Store4(o + 64, uint4(old_bounds.xyz, old_set));
		instances.Store4(o + 80, uint4(old2.xyz, 0));
		return;
	}

	bounds.Store4(b, new_bounds);
	instances.Store4(o, n0);
	instances.Store4(o + 16, n1);
	instances.Store4(o + 32, n2);
	if (flags & TELEPORT) {
		instances.Store4(o + 48, n0);
		instances.Store4(o + 64, uint4(new_bounds.xyz, new_set));
		instances.Store4(o + 80, uint4(n2.xyz, 0));
	}
	else {
		instances.Store4(o + 48, old0);
		instances.Store4(o + 64, uint4(old_bounds.xyz, new_set));
		instances.Store4(o + 80, uint4(old2.xyz, 0));
	}
}
