#include "engine/shaders/common.hlsli"
#include "engine/shaders/gpu_scene_layout.hlsli"

// Voxel clusters (clusters without triangles, see MeshVoxelCluster in src/renderer/mesh_clusters.h) of the clusters that nanite.hlsl selected for a view are drawn as bricks:
// every brick (4 x 4 x 4 voxels, a 64 bit mask) of a selected voxel cluster that is not culled is an instance of a cube that voxels.hlsl ray marches. The passes, in the
// order of a view (the selected clusters are final after gpu_scene_variants.hlsl, which does not change them):
//   FRAME_CLEAR  once in a frame: the pool of bricks is empty
//   CLEAR        the view starts its range of the pool
//   EXPAND       one thread for each selected cluster: if it is a cluster of voxels, it appends its bricks that are in the frustum and not hidden to the pool
//   RETEST       the second pass of the main view: one thread for each brick that the first pass found hidden, tests it with the pyramid of this frame and appends it to the pool if it is visible
//   ARGS         the command of the indirect draw of the view (DrawIndexedIndirect of a cube, an instance for each brick of the range of the view)
// The pool is shared by the views of a frame, they are executed one after another, so the bricks of a view are consecutive. u_counters: [0] the end of the used part of the pool,
// the number of bricks that were appended (it can be over the capacity, the bricks that did not fit were dropped); from 64 a block of 64 bytes for each view: [0] the first brick
// of the view, from 16 the command (draw id, index count, instance count, first index, base vertex, base instance). The draw id is the first brick of the view, the vertex shader
// adds it to SV_InstanceID, which does not include the start instance.
// A brick is 32 bytes: slot and instance of the model instance (as in the selected clusters), the address of the header of its cluster (MeshClusterVertexGPU), the address of its
// record (4 words, see MeshVoxelCluster), the address of the attributes of the voxels of its cluster. Everything else voxels.hlsl reads from the buffer of cluster data.
// Occlusion: u_hzb_mode 1 tests with the pyramid of the previous frame (the first pass of the main view): the bricks that it hides are appended to the list u_occluded (its count is
// the word at 16 of u_counters), and RETEST of the second pass (mode 2, the pyramid of this frame) draws the ones that are visible after all. 0 does not test.

cbuffer UniformData : register(b4) {
	float4 u_planes[6];
	float4 u_prev_vp[4];	// the rows of projection * rotation of the camera of the previous frame
	float4 u_cur_vp[4];
	float4 u_prev_delta;	// xyz: the camera of this frame - the camera of the previous frame
	int3 u_cam_p;			// the camera in the fixed point of the GPU scene
	uint u_selected_capacity;
	float3 u_cam_frac;
	uint u_brick_capacity;	// entries of the pool
	uint u_view_slot;
	uint u_selected;		// rw bindless, nanite.hlsl: [0] the number of selected clusters, from 16 entries (slot, mesh, cluster, instance)
	uint u_meshes;			// bindless, the mesh table of the scene (GPUClusterMesh)
	uint u_cluster_data;	// bindless, the buffer of cluster data
	uint u_bricks;			// rw bindless
	uint u_counters;		// rw bindless
	uint u_instances;		// bindless, GPUScene::instances
	uint u_chunks;			// bindless, GPUSceneChunks::chunks
	uint u_hzb_mode;
	uint u_hzb_prev;		// bindless texture, the pyramid of the previous frame (hzb.hlsl)
	uint u_hzb_cur;
	uint u_hzb_mips;
	float2 u_hzb_size;
	uint u_occluded;		// rw bindless, the bricks that the pyramid of the previous frame hides, 16 bytes each (the same as the pool)
	uint u_occluded_capacity;
	uint u_args;			// rw bindless, indirect dispatch args: [0] EXPAND (1 group per voxel cluster), [16] RETEST
	uint u_quads;			// rw bindless, precomputed screen-space quad bounds per brick (16 bytes each)
	uint2 u_args_pad;
	float4 u_draw_vp[4];	// rows of projection * view of the drawing camera
	float4 u_draw_delta;	// xyz: cull camera - draw camera, w: 1 if ortho
};

#define GROUP_SIZE 64
#define BRICK_STRIDE 16
#define VIEWS_AT 64
#define VIEW_STRIDE 64
#define VIEW_COMMAND 16
#define CUBE_INDEX_COUNT 6

// the transform of a model instance or of an instance of a chunk of an instanced model, relative to the camera
void loadTransform(uint slot, uint instance_ref, out float4 rot, out float3 position, out float3 scale) {
	if (instance_ref == 0) {
		ByteAddressBuffer instances = bindless_buffers[u_instances];
		const uint at = slot * INSTANCE_STRIDE;
		rot = asfloat(instances.Load4(at + INSTANCE_ROT));
		position = float3(asint(instances.Load3(at + INSTANCE_POS)) - u_cam_p) * SCENE_POS_UNIT - u_cam_frac;
		scale = asfloat(instances.Load3(at + INSTANCE_SCALE));
	}
	else {
		ByteAddressBuffer chunks = bindless_buffers[u_chunks];
		const uint at = slot * CHUNK_STRIDE;
		ByteAddressBuffer data = bindless_buffers[NonUniformResourceIndex(chunks.Load(at + CHUNK_INSTANCE_BUFFER))];
		const uint instance_at = (instance_ref - 1) * CHUNK_INSTANCE_STRIDE;
		const float4 rot_lod = asfloat(data.Load4(instance_at + CHUNK_INSTANCE_ROT_LOD));
		const float4 pos_scale = asfloat(data.Load4(instance_at + CHUNK_INSTANCE_POS_SCALE));
		rot = float4(rot_lod.xyz, sqrt(saturate(1 - dot(rot_lod.xyz, rot_lod.xyz))));
		position = float3(asint(chunks.Load3(at + CHUNK_P)) - u_cam_p) * SCENE_POS_UNIT - u_cam_frac + pos_scale.xyz;
		scale = pos_scale.www;
	}
}

bool insideFrustum(float3 center, float radius) {
	for (int i = 0; i < 6; ++i) {
		if (dot(u_planes[i], float4(center, 1)) < -radius) return false;
	}
	return true;
}

// The same test as in nanite.hlsl (see there): true if the sphere (relative to the camera) is certainly behind what the pyramid has.
#define HZB_DETAIL_LEVELS 2
#define HZB_MIN_MIP 1
bool isOccludedClip(float4 clip, float radius, float2 row_scale, bool previous) {
	const float w_near = clip.w - radius;
	if (w_near <= 1e-4) return false;
	const float w_far = clip.w + radius;
	const float2 n_max = clip.xy + row_scale;
	const float2 n_min = clip.xy - row_scale;
	const float2 ndc_max = n_max / float2(n_max.x > 0 ? w_near : w_far, n_max.y > 0 ? w_near : w_far);
	const float2 ndc_min = n_min / float2(n_min.x < 0 ? w_near : w_far, n_min.y < 0 ? w_near : w_far);
	const float nearest = clip.z / w_near;
	const float2 uv_a = toScreenUV(ndc_min * 0.5 + 0.5);
	const float2 uv_b = toScreenUV(ndc_max * 0.5 + 0.5);
	const float2 mn = saturate(min(uv_a, uv_b));
	const float2 mx = saturate(max(uv_a, uv_b));

	const float2 size_px = (mx - mn) * u_hzb_size;
	const uint mip = min((uint)max(ceil(log2(max(max(size_px.x, size_px.y), 1.0))) - HZB_DETAIL_LEVELS, HZB_MIN_MIP), u_hzb_mips - 1);
	const int2 mip_size = max(int2(u_hzb_size) >> mip, 1);
	const int2 t0 = min(int2(mn * u_hzb_size) >> mip, mip_size - 1);
	const int2 t1 = min(int2(mx * u_hzb_size) >> mip, mip_size - 1);
	Texture2D<float4> hzb = bindless_textures[previous ? u_hzb_prev : u_hzb_cur];
	float farthest = 1e30;
	for (int y = t0.y; y <= t1.y; ++y) {
		for (int x = t0.x; x <= t1.x; ++x) farthest = min(farthest, hzb.Load(int3(x, y, mip - HZB_MIN_MIP)).r);
	}
	return farthest > nearest * 1.0005 + 1e-6;
}

bool isOccluded(float3 center, float radius, bool previous) {
	const float3 c = previous ? center + u_prev_delta.xyz : center;
	const float4 p = float4(c, 1);
	float4 clip;
	float2 row_scale;
	if (previous) {
		clip = float4(dot(u_prev_vp[0], p), dot(u_prev_vp[1], p), dot(u_prev_vp[2], p), dot(u_prev_vp[3], p));
		row_scale = float2(length(u_prev_vp[0].xyz), length(u_prev_vp[1].xyz)) * radius;
	}
	else {
		clip = float4(dot(u_cur_vp[0], p), dot(u_cur_vp[1], p), dot(u_cur_vp[2], p), dot(u_cur_vp[3], p));
		row_scale = float2(length(u_cur_vp[0].xyz), length(u_cur_vp[1].xyz)) * radius;
	}
	return isOccludedClip(clip, radius, row_scale, previous);
}

uint3 dispatchGroups(uint count) {
	return uint3(min(count, MAX_GROUPS_X), (count + MAX_GROUPS_X - 1) / MAX_GROUPS_X, 1);
}

#define EXPAND_GROUP_SIZE 32
#define CLUSTERS_PER_GROUP 4
#define THREADS_PER_CLUSTER (EXPAND_GROUP_SIZE / CLUSTERS_PER_GROUP)

#if !defined FRAME_CLEAR && !defined CLEAR && !defined ARGS && !defined RETEST
groupshared uint g_slot[CLUSTERS_PER_GROUP];
groupshared uint g_instance[CLUSTERS_PER_GROUP];
groupshared uint g_header[CLUSTERS_PER_GROUP];
groupshared uint g_payload[CLUSTERS_PER_GROUP];
groupshared uint g_brick_count[CLUSTERS_PER_GROUP];
groupshared float3 g_voxel_scale[CLUSTERS_PER_GROUP];
groupshared float3 g_base_pos[CLUSTERS_PER_GROUP];
groupshared float3 g_vx_ws[CLUSTERS_PER_GROUP];
groupshared float3 g_vy_ws[CLUSTERS_PER_GROUP];
groupshared float3 g_vz_ws[CLUSTERS_PER_GROUP];
groupshared float4 g_clip_base[CLUSTERS_PER_GROUP];
groupshared float4 g_clip_x[CLUSTERS_PER_GROUP];
groupshared float4 g_clip_y[CLUSTERS_PER_GROUP];
groupshared float4 g_clip_z[CLUSTERS_PER_GROUP];
groupshared float2 g_row_scale[CLUSTERS_PER_GROUP];
groupshared float4 g_draw_base[CLUSTERS_PER_GROUP];
groupshared float4 g_draw_x[CLUSTERS_PER_GROUP];
groupshared float4 g_draw_y[CLUSTERS_PER_GROUP];
groupshared float4 g_draw_z[CLUSTERS_PER_GROUP];
#define THREAD_GROUP_SIZE EXPAND_GROUP_SIZE
#else
#define THREAD_GROUP_SIZE GROUP_SIZE
#endif

void unpackBrickSubBox(uint mask_lo, uint mask_hi, out float3 sub_min, out float3 sub_max, out uint sub_packed) {
	const uint xy16 = ((mask_lo | mask_hi) | ((mask_lo | mask_hi) >> 16)) & 0xffffu;
	const uint x4 = ((xy16 | (xy16 >> 8)) | ((xy16 | (xy16 >> 8)) >> 4)) & 0xfu;
	const uint z4 = ((mask_lo & 0xffffu) ? 1u : 0u) | ((mask_lo >> 16) ? 2u : 0u) | ((mask_hi & 0xffffu) ? 4u : 0u) | ((mask_hi >> 16) ? 8u : 0u);
	const uint3 sub_min_u = uint3(firstbitlow(x4), firstbitlow(xy16) >> 2, firstbitlow(z4));
	const uint3 sub_max_u = uint3(firstbithigh(x4), firstbithigh(xy16) >> 2, firstbithigh(z4));
	sub_min = float3(sub_min_u);
	sub_max = float3(sub_max_u + 1);
	sub_packed = sub_min_u.x | (sub_min_u.y << 2) | (sub_min_u.z << 4) | (sub_max_u.x << 6) | (sub_max_u.y << 8) | (sub_max_u.z << 10);
}

uint4 computeBrickQuad(float4 c0, float4 cx, float4 cy, float4 cz, bool is_ortho, uint sub_packed, out bool behind_camera) {
	behind_camera = false;
	float3 ndc_min = float3(-1, -1, 0);
	float3 ndc_max = float3(1, 1, 1);
	if (is_ortho) {
		ndc_min = c0.xyz + min(0.0, cx.xyz) + min(0.0, cy.xyz) + min(0.0, cz.xyz);
		ndc_max = c0.xyz + max(0.0, cx.xyz) + max(0.0, cy.xyz) + max(0.0, cz.xyz);
	}
	else {
		const float max_w = c0.w + max(0.0, cx.w) + max(0.0, cy.w) + max(0.0, cz.w);
		if (max_w <= 1e-4) {
			behind_camera = true;
			return uint4(0, 0, asuint(-1.0), sub_packed);
		}
		const float min_w = c0.w + min(0.0, cx.w) + min(0.0, cy.w) + min(0.0, cz.w);
		if (min_w > 1e-4) {
			const float4 c100 = c0 + cx;
			const float4 c010 = c0 + cy;
			const float4 c110 = c100 + cy;
			const float4 c001 = c0 + cz;
			const float4 c101 = c100 + cz;
			const float4 c011 = c010 + cz;
			const float4 c111 = c110 + cz;
			const float3 n0 = c0.xyz / c0.w;
			const float3 n1 = c100.xyz / c100.w;
			const float3 n2 = c010.xyz / c010.w;
			const float3 n3 = c110.xyz / c110.w;
			const float3 n4 = c001.xyz / c001.w;
			const float3 n5 = c101.xyz / c101.w;
			const float3 n6 = c011.xyz / c011.w;
			const float3 n7 = c111.xyz / c111.w;
			ndc_min = min(min(min(n0, n1), min(n2, n3)), min(min(n4, n5), min(n6, n7)));
			ndc_max = max(max(max(n0, n1), max(n2, n3)), max(max(n4, n5), max(n6, n7)));
		}
	}
	const uint2 mn_u = uint2(floor(saturate(ndc_min.xy * 0.5 + 0.5) * 65535.0));
	const uint2 mx_u = uint2(ceil(saturate(ndc_max.xy * 0.5 + 0.5) * 65535.0));
	const float qz = clamp(ndc_max.z, 0.0, 1.0);
	return uint4(mn_u.x | (mn_u.y << 16), mx_u.x | (mx_u.y << 16), asuint(qz), sub_packed);
}

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID) {
	#if defined FRAME_CLEAR
		if (thread_id.x == 0) {
			RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
			counters.Store(0, 0);
			counters.Store(16, 0);
			if (u_args != 0) bindless_rw_buffers[u_args].Store4(16, uint4(0, 0, 0, 0));
		}
	#elif defined CLEAR
		if (thread_id.x == 0) {
			RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
			counters.Store(VIEWS_AT + u_view_slot * VIEW_STRIDE, counters.Load(0));
			if (u_hzb_mode == 1) counters.Store(16, 0);
			const uint vc = min(bindless_buffers[u_selected].Load(SELECTED_COUNT), u_selected_capacity);
			bindless_rw_buffers[u_args].Store3(0, dispatchGroups((vc + CLUSTERS_PER_GROUP - 1) / CLUSTERS_PER_GROUP));
		}
	#elif defined ARGS
		if (thread_id.x == 0) {
			RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
			const uint at = VIEWS_AT + u_view_slot * VIEW_STRIDE;
			const uint end = min(counters.Load(0), u_brick_capacity);
			const uint begin = min(counters.Load(at), end);
			counters.Store4(at + VIEW_COMMAND, uint4(begin, CUBE_INDEX_COUNT, end - begin, 0));
			counters.Store2(at + VIEW_COMMAND + 16, uint2(0, 0));
			if (u_hzb_mode == 1) {
				const uint occ = min(counters.Load(16), u_occluded_capacity);
				const uint retest_groups = (occ + GROUP_SIZE - 1) / GROUP_SIZE;
				bindless_rw_buffers[u_args].Store3(16, dispatchGroups(retest_groups));
			}
		}
	#elif defined RETEST
		const uint index = thread_id.y * MAX_GROUPS_X * GROUP_SIZE + thread_id.x;
		RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
		const uint count = min(counters.Load(16), u_occluded_capacity);
		if (index >= count) return;
		RWByteAddressBuffer occluded = bindless_rw_buffers[u_occluded];
		const uint4 brick_entry = occluded.Load4(index * BRICK_STRIDE);	// slot | (b << 26), instance, header, brick record

		ByteAddressBuffer data = bindless_buffers[u_cluster_data];
		const int3 base_voxel = asint(data.Load3(brick_entry.z));
		const float size = asfloat(data.Load(brick_entry.z + 28));
		const float3 grid_origin = asfloat(data.Load3(brick_entry.z + 32));
		const uint3 brick_xyz = data.Load3(brick_entry.w);
		const int3 brick_coord = int3(brick_xyz.z & 1023, (brick_xyz.z >> 10) & 1023, (brick_xyz.z >> 20) & 1023);
		float3 sub_min, sub_max;
		uint sub_packed;
		unpackBrickSubBox(brick_xyz.x, brick_xyz.y, sub_min, sub_max, sub_packed);
		float4 rot;
		float3 position, scale;
		loadTransform(brick_entry.x & 0x03ffffffu, brick_entry.y, rot, position, scale);
		const float3 center = position + rotateByQuat(rot, (grid_origin + (float3(base_voxel + brick_coord * 4) + 0.5 * (sub_min + sub_max)) * size) * scale);
		const float radius = 0.5 * length((sub_max - sub_min) * scale) * size;
		bool visible = !isOccluded(center, radius, false);
		uint4 quad = 0;
		if (visible) {
			const float3 p0_ws = position + u_draw_delta.xyz + rotateByQuat(rot, (grid_origin + (float3(base_voxel + brick_coord * 4) + sub_min) * size) * scale);
			const float3 ext = (sub_max - sub_min) * size * scale;
			const float3 ex_ws = rotateByQuat(rot, float3(ext.x, 0, 0));
			const float3 ey_ws = rotateByQuat(rot, float3(0, ext.y, 0));
			const float3 ez_ws = rotateByQuat(rot, float3(0, 0, ext.z));
			const float4 p0 = float4(p0_ws, 1.0);
			const float4 c0 = float4(dot(u_draw_vp[0], p0), dot(u_draw_vp[1], p0), dot(u_draw_vp[2], p0), dot(u_draw_vp[3], p0));
			const float4 cx = float4(dot(u_draw_vp[0].xyz, ex_ws), dot(u_draw_vp[1].xyz, ex_ws), dot(u_draw_vp[2].xyz, ex_ws), dot(u_draw_vp[3].xyz, ex_ws));
			const float4 cy = float4(dot(u_draw_vp[0].xyz, ey_ws), dot(u_draw_vp[1].xyz, ey_ws), dot(u_draw_vp[2].xyz, ey_ws), dot(u_draw_vp[3].xyz, ey_ws));
			const float4 cz = float4(dot(u_draw_vp[0].xyz, ez_ws), dot(u_draw_vp[1].xyz, ez_ws), dot(u_draw_vp[2].xyz, ez_ws), dot(u_draw_vp[3].xyz, ez_ws));
			bool behind_camera;
			quad = computeBrickQuad(c0, cx, cy, cz, u_draw_delta.w != 0.0, sub_packed, behind_camera);
			if (behind_camera) visible = false;
		}

		const uint wave_vis = WaveActiveCountBits(visible);
		uint vis_base = 0;
		if (wave_vis > 0 && WaveIsFirstLane()) counters.InterlockedAdd(0, wave_vis, vis_base);
		vis_base = WaveReadLaneFirst(vis_base);
		const uint vis_offset = WavePrefixCountBits(visible);

		RWByteAddressBuffer bricks = bindless_rw_buffers[u_bricks];
		RWByteAddressBuffer quads = bindless_rw_buffers[u_quads];
		if (visible) {
			const uint at = vis_base + vis_offset;
			if (at < u_brick_capacity) {
				bricks.Store4(at * BRICK_STRIDE, brick_entry);
				quads.Store4(at * BRICK_STRIDE, quad);
			}
		}
	#else
		// EXPAND: CLUSTERS_PER_GROUP voxel clusters per 32-thread group (THREADS_PER_CLUSTER threads per cluster).
		const uint gtid = group_thread_id.x;
		const uint sub = gtid / THREADS_PER_CLUSTER;
		const uint lane = gtid % THREADS_PER_CLUSTER;
		const uint vc_idx = (group_id.y * MAX_GROUPS_X + group_id.x) * CLUSTERS_PER_GROUP + sub;
		ByteAddressBuffer data = bindless_buffers[u_cluster_data];
		const bool hzb_prev = (u_hzb_mode == 1);

		if (lane == 0) {
			ByteAddressBuffer selected = bindless_buffers[u_selected];
			const uint selected_count = min(selected.Load(SELECTED_COUNT), u_selected_capacity);
			if (vc_idx >= selected_count) {
				g_brick_count[sub] = 0;
			}
			else {
				const uint4 entry = selected.Load4(SELECTED_HEADER + vc_idx * SELECTED_STRIDE);	// slot, mesh, cluster, instance
				const uint cluster = entry.z & 0x07ffffffu;

				ByteAddressBuffer meshes = bindless_buffers[u_meshes];
				const uint4 mesh_info = meshes.Load4(entry.y * CLUSTER_MESH_STRIDE + CLUSTER_MESH_TABLE);	// clusters, group table, cluster count, node count
				const uint root_count = meshes.Load(entry.y * CLUSTER_MESH_STRIDE + CLUSTER_MESH_ROOT_COUNT);
				const uint cluster_at = mesh_info.x + cluster * CLUSTER_STRIDE;

				const uint headers_offset = mesh_info.x + ((mesh_info.z * CLUSTER_STRIDE + mesh_info.w * NODE_STRIDE + root_count * ROOT_STRIDE + CLUSTER_ALIGNMENT_MASK) & ~(uint)CLUSTER_ALIGNMENT_MASK);
				const uint header = headers_offset + cluster * CLUSTER_HEADER_STRIDE;
				const uint group_idx = data.Load(cluster_at + CLUSTER_GROUP);
				const uint stream_base = data.Load4(mesh_info.y + group_idx * GROUP_STRIDE).x;
				const int3 base_voxel = asint(data.Load3(header));
				const uint4 h1 = data.Load4(header + 16);	// uv bits, the number of bricks, the bit offset of the payload in the stream of the group, the size of a voxel
				const float3 grid_origin = asfloat(data.Load3(header + 32));
				const uint brick_count = min(h1.y, 64u);
				const float size = asfloat(h1.w);
				const uint payload = stream_base + (h1.z >> 3);

				float4 rot;
				float3 position, scale;
				loadTransform(entry.x, entry.w, rot, position, scale);
				const float3 voxel_scale = size * scale;

				const float3 base_pos = position + rotateByQuat(rot, (grid_origin + float3(base_voxel) * size) * scale);
				const float3 vx_ws = rotateByQuat(rot, float3(voxel_scale.x, 0, 0));
				const float3 vy_ws = rotateByQuat(rot, float3(0, voxel_scale.y, 0));
				const float3 vz_ws = rotateByQuat(rot, float3(0, 0, voxel_scale.z));

				g_slot[sub] = entry.x;
				g_instance[sub] = entry.w;
				g_header[sub] = header;
				g_payload[sub] = payload;
				g_brick_count[sub] = brick_count;
				g_voxel_scale[sub] = voxel_scale;
				g_base_pos[sub] = base_pos;
				g_vx_ws[sub] = vx_ws;
				g_vy_ws[sub] = vy_ws;
				g_vz_ws[sub] = vz_ws;

				const float4 dp0 = float4(base_pos + u_draw_delta.xyz, 1.0);
				g_draw_base[sub] = float4(dot(u_draw_vp[0], dp0), dot(u_draw_vp[1], dp0), dot(u_draw_vp[2], dp0), dot(u_draw_vp[3], dp0));
				g_draw_x[sub] = float4(dot(u_draw_vp[0].xyz, vx_ws), dot(u_draw_vp[1].xyz, vx_ws), dot(u_draw_vp[2].xyz, vx_ws), dot(u_draw_vp[3].xyz, vx_ws));
				g_draw_y[sub] = float4(dot(u_draw_vp[0].xyz, vy_ws), dot(u_draw_vp[1].xyz, vy_ws), dot(u_draw_vp[2].xyz, vy_ws), dot(u_draw_vp[3].xyz, vy_ws));
				g_draw_z[sub] = float4(dot(u_draw_vp[0].xyz, vz_ws), dot(u_draw_vp[1].xyz, vz_ws), dot(u_draw_vp[2].xyz, vz_ws), dot(u_draw_vp[3].xyz, vz_ws));

				if (u_hzb_mode != 0 && brick_count > 1) {
					const float4 vp0 = hzb_prev ? u_prev_vp[0] : u_cur_vp[0];
					const float4 vp1 = hzb_prev ? u_prev_vp[1] : u_cur_vp[1];
					const float4 vp2 = hzb_prev ? u_prev_vp[2] : u_cur_vp[2];
					const float4 vp3 = hzb_prev ? u_prev_vp[3] : u_cur_vp[3];
					const float4 p0 = float4(hzb_prev ? base_pos + u_prev_delta.xyz : base_pos, 1.0);
					g_clip_base[sub] = float4(dot(vp0, p0), dot(vp1, p0), dot(vp2, p0), dot(vp3, p0));
					g_clip_x[sub] = float4(dot(vp0.xyz, vx_ws), dot(vp1.xyz, vx_ws), dot(vp2.xyz, vx_ws), dot(vp3.xyz, vx_ws));
					g_clip_y[sub] = float4(dot(vp0.xyz, vy_ws), dot(vp1.xyz, vy_ws), dot(vp2.xyz, vy_ws), dot(vp3.xyz, vy_ws));
					g_clip_z[sub] = float4(dot(vp0.xyz, vz_ws), dot(vp1.xyz, vz_ws), dot(vp2.xyz, vz_ws), dot(vp3.xyz, vz_ws));
					g_row_scale[sub] = float2(length(vp0.xyz), length(vp1.xyz));
				}
			}
		}
		GroupMemoryBarrierWithGroupSync();

		const uint brick_count = g_brick_count[sub];
		const uint max_bc = WaveActiveMax(brick_count);
		RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
		RWByteAddressBuffer bricks = bindless_rw_buffers[u_bricks];
		RWByteAddressBuffer quads = bindless_rw_buffers[u_quads];
		RWByteAddressBuffer occluded = bindless_rw_buffers[u_occluded];

		for (uint base_b = 0; base_b < max_bc; base_b += THREADS_PER_CLUSTER) {
			const uint b = base_b + lane;
			bool vis = false;
			bool occ = false;
			uint4 quad = 0;
			if (b < brick_count) {
				const uint3 brick_xyz = data.Load3(g_payload[sub] + b * 16);
				if ((brick_xyz.x | brick_xyz.y) != 0) {
					float3 sub_min, sub_max;
					uint sub_packed;
					unpackBrickSubBox(brick_xyz.x, brick_xyz.y, sub_min, sub_max, sub_packed);
					const float3 bc = float3(brick_xyz.z & 1023, (brick_xyz.z >> 10) & 1023, (brick_xyz.z >> 20) & 1023);
					if (brick_count == 1) {
						vis = true;
					}
					else {
						const float3 vc = bc * 4.0 + 0.5 * (sub_min + sub_max);
						const float3 center = g_base_pos[sub] + g_vx_ws[sub] * vc.x + g_vy_ws[sub] * vc.y + g_vz_ws[sub] * vc.z;
						const float radius = 0.5 * length((sub_max - sub_min) * g_voxel_scale[sub]);
						if (insideFrustum(center, radius)) {
							if (u_hzb_mode != 0) {
								const float4 clip = g_clip_base[sub] + g_clip_x[sub] * vc.x + g_clip_y[sub] * vc.y + g_clip_z[sub] * vc.z;
								if (isOccludedClip(clip, radius, g_row_scale[sub] * radius, hzb_prev)) {
									occ = hzb_prev;
								}
								else {
									vis = true;
								}
							}
							else {
								vis = true;
							}
						}
					}
					if (vis) {
						const float3 lo = bc * 4.0 + sub_min;
						const float3 ext = sub_max - sub_min;
						const float4 c0 = g_draw_base[sub] + g_draw_x[sub] * lo.x + g_draw_y[sub] * lo.y + g_draw_z[sub] * lo.z;
						bool behind_camera;
						quad = computeBrickQuad(c0, g_draw_x[sub] * ext.x, g_draw_y[sub] * ext.y, g_draw_z[sub] * ext.z, u_draw_delta.w != 0.0, sub_packed, behind_camera);
						if (behind_camera) vis = false;
					}
				}
			}

			const uint wave_vis = WaveActiveCountBits(vis);
			uint wave_vis_base = 0;
			if (wave_vis > 0 && WaveIsFirstLane()) counters.InterlockedAdd(0, wave_vis, wave_vis_base);
			wave_vis_base = WaveReadLaneFirst(wave_vis_base);
			const uint vis_off = WavePrefixCountBits(vis);
			if (vis) {
				const uint at = wave_vis_base + vis_off;
				if (at < u_brick_capacity) {
					bricks.Store4(at * BRICK_STRIDE, uint4(g_slot[sub] | (b << 26), g_instance[sub], g_header[sub], g_payload[sub] + b * 16));
					quads.Store4(at * BRICK_STRIDE, quad);
				}
			}

			const uint wave_occ = WaveActiveCountBits(occ);
			uint wave_occ_base = 0;
			if (wave_occ > 0 && WaveIsFirstLane()) counters.InterlockedAdd(16, wave_occ, wave_occ_base);
			wave_occ_base = WaveReadLaneFirst(wave_occ_base);
			const uint occ_off = WavePrefixCountBits(occ);
			if (occ) {
				const uint hidden = wave_occ_base + occ_off;
				if (hidden < u_occluded_capacity) {
					occluded.Store4(hidden * BRICK_STRIDE, uint4(g_slot[sub] | (b << 26), g_instance[sub], g_header[sub], g_payload[sub] + b * 16));
				}
			}
		}
	#endif
}
