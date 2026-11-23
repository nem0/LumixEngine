#include "engine/shaders/common.hlsli"
#include "engine/shaders/gpu_scene_layout.hlsli"

// GPU scene, see src/renderer/gpu_scene.h. Writes the entries that changed (GPUSceneEntry, 128 bytes) to the buffers of the scene.
//
// u_bounds:     16 bytes per slot, int3 position (fixed point), float radius (< 0: the slot is free or disabled)
// u_instances: 96 bytes per slot (GPUSceneInstance):
//                 0 float4 rot, 16 int3 p, uint model, 32 float3 scale, uint flags,
//                48 float4 prev_rot, 64 int3 prev_p, uint material_set, 80 float3 prev_scale, uint moved_frame
//
// An entry writes only the parts in its mask. A new transform moves the current one to prev_* and sets moved_frame to this frame; the readers use prev_* only if
// moved_frame is the current frame, so an instance that stops moving needs no second write. A slot that has no transform yet (FRESH, or moved_frame NO_STATE) gets the
// new transform as the previous one too. A second write in the same frame keeps prev_*, which is the transform of the previous frame then.

cbuffer UniformData : register(b4) {
	uint u_entries;			// bindless, GPUSceneEntry[]
	uint u_entries_offset;	// bytes
	uint u_entry_count;
	uint u_bounds;			// rw bindless
	uint u_instances;		// rw bindless
	uint u_frame;
	uint u_padding0;
	uint u_padding1;
};

#define TRANSFORM 1
#define FRESH 2
#define MATERIAL_SET 4
#define STATIC 8
#define DISABLE 16

#define GROUP_THREADS 64

[numthreads(GROUP_THREADS, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	const uint index = thread_id.y * MAX_GROUPS_X * GROUP_THREADS + thread_id.x;
	if (index >= u_entry_count) return;

	ByteAddressBuffer entries = bindless_buffers[u_entries];
	RWByteAddressBuffer bounds = bindless_rw_buffers[u_bounds];
	RWByteAddressBuffer instances = bindless_rw_buffers[u_instances];

	const uint e = u_entries_offset + index * ENTRY_STRIDE;
	const uint slot = entries.Load(e + ENTRY_SLOT);
	const uint mask = entries.Load(e + ENTRY_MASK);
	const uint d = e + ENTRY_INSTANCE;	// the model instance of the entry
	const uint b = slot * BOUNDS_STRIDE;
	const uint o = slot * INSTANCE_STRIDE;

	if (mask & STATIC) {
		instances.Store(o + INSTANCE_MODEL, entries.Load(d + INSTANCE_MODEL));
		instances.Store(o + INSTANCE_FLAGS, entries.Load(d + INSTANCE_FLAGS));
	}
	if (mask & MATERIAL_SET) instances.Store(o + INSTANCE_MATERIAL_SET, entries.Load(d + INSTANCE_MATERIAL_SET));
	if (mask & TRANSFORM) {
		const uint4 rot = entries.Load4(d + INSTANCE_ROT);
		const uint3 p = entries.Load3(d + INSTANCE_POS);
		const uint3 scale = entries.Load3(d + INSTANCE_SCALE);
		const uint moved_frame = instances.Load(o + INSTANCE_MOVED_FRAME);
		if ((mask & FRESH) || moved_frame == NO_STATE) {
			instances.Store4(o + INSTANCE_PREV_ROT, rot);
			instances.Store3(o + INSTANCE_PREV_POS, p);
			instances.Store3(o + INSTANCE_PREV_SCALE, scale);
		}
		else if (moved_frame != u_frame) {
			// the current transform is the one of the previous frame
			instances.Store4(o + INSTANCE_PREV_ROT, instances.Load4(o + INSTANCE_ROT));
			instances.Store3(o + INSTANCE_PREV_POS, instances.Load3(o + INSTANCE_POS));
			instances.Store3(o + INSTANCE_PREV_SCALE, instances.Load3(o + INSTANCE_SCALE));
		}
		instances.Store4(o + INSTANCE_ROT, rot);
		instances.Store3(o + INSTANCE_POS, p);
		instances.Store3(o + INSTANCE_SCALE, scale);
		instances.Store(o + INSTANCE_MOVED_FRAME, u_frame);
		bounds.Store4(b, entries.Load4(e + ENTRY_BOUNDS));
	}
	else if (mask & FRESH) {
		instances.Store(o + INSTANCE_MOVED_FRAME, NO_STATE);
	}
	if (mask & DISABLE) bounds.Store4(b, uint4(0, 0, 0, asuint(-1.0)));	// a negative radius
}
