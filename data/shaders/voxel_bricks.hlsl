#include "engine/shaders/common.hlsli"
#include "engine/shaders/gpu_scene_layout.hlsli"
#include "engine/shaders/hzb_common.hlsli"

// Voxel clusters (clusters without triangles, see MeshVoxelCluster in src/renderer/mesh_clusters.h) of the clusters that nanite.hlsl selected for a view are drawn as bricks:
// every brick (4 x 4 x 4 voxels, a 64 bit mask) of a selected voxel cluster that is not culled is an instance of a cube that voxels.hlsl ray marches. The passes, in the
// order of a view (the selected clusters are final after nanite.hlsl):
//   FRAME_CLEAR  once in a frame: the pool of bricks is empty
//   CLEAR        the view starts its range of the pool, clears its stats/occluded counter, and writes the dispatch arguments of EXPAND
//   EXPAND       CLUSTERS_PER_GROUP voxel clusters per group: appends bricks that are in the frustum and not hidden to the pool
//   RETEST       the second pass of the main view: one thread for each brick that the first pass found hidden, tests it with the pyramid of this frame and appends it to the pool if it is visible
//   ARGS         the command of the indirect draw of the view (DrawIndexedIndirect of a quad, an instance for each brick of the range of the view)
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
	HZBUniforms u_hzb;
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
	uint u_occluded;		// rw bindless, the bricks that the pyramid of the previous frame hides, 16 bytes each (the same as the pool)
	uint u_occluded_capacity;
	uint u_args;			// rw bindless, indirect dispatch args: [0] EXPAND (1 group per voxel cluster), [16] RETEST
	uint u_quads;			// rw bindless, precomputed screen-space quad bounds per brick (16 bytes each)
	float4 u_draw_vp[4];	// rows of projection * view of the drawing camera
	float4 u_draw_delta;	// xyz: cull camera - draw camera, w: 1 if ortho
};

#define GROUP_SIZE 64
#define CUBE_INDEX_COUNT 6

uint waveAppendBool(RWByteAddressBuffer counters, uint counter_offset, bool active) {
	const uint total = WaveActiveCountBits(active);
	uint base = 0;
	if (total > 0 && WaveIsFirstLane()) counters.InterlockedAdd(counter_offset, total, base);
	return WaveReadLaneFirst(base) + WavePrefixCountBits(active);
}

#define EXPAND_GROUP_SIZE 64
#define CLUSTERS_LOG2 4
#define CLUSTERS_PER_GROUP (1 << CLUSTERS_LOG2)	// the first threads of the group set up one cluster each, the bricks of all of them are expanded by one thread each (see EXPAND)

#if !defined FRAME_CLEAR && !defined CLEAR && !defined ARGS && !defined RETEST
struct ClusterSetup {
	uint slot;
	uint instance;
	uint header;
	uint payload;
	uint brick_count;
	float3 voxel_scale;
	float3 base_pos;
	float3 vx_ws;
	float3 vy_ws;
	float3 vz_ws;
	float4 clip_base;
	float4 clip_x;
	float4 clip_y;
	float4 clip_z;
	float2 row_scale;
	float4 draw_base;
	float4 draw_x;
	float4 draw_y;
	float4 draw_z;
};
groupshared uint g_scan[CLUSTERS_PER_GROUP];	// inclusive prefix sum of the brick counts of the clusters of the group
groupshared ClusterSetup g_clusters[CLUSTERS_PER_GROUP];
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
			counters.Store(VOXEL_OCCLUDED_COUNT, 0);
			if (u_args != 0) bindless_rw_buffers[u_args].Store4(16, uint4(0, 0, 0, 0));
		}
	#elif defined CLEAR
		if (thread_id.x == 0) {
			RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
			const uint at = VOXEL_VIEWS_AT + u_view_slot * VOXEL_VIEW_STRIDE;
			counters.Store(at, counters.Load(0));
			counters.Store3(at + VOXEL_VIEW_STATS, uint3(0, 0, 0));
			if (u_hzb.mode == 1) counters.Store(VOXEL_OCCLUDED_COUNT, 0);
			const uint vc = min(bindless_buffers[u_selected].Load(SELECTED_COUNT), u_selected_capacity);
			bindless_rw_buffers[u_args].Store3(0, dispatchGroups((vc + CLUSTERS_PER_GROUP - 1) / CLUSTERS_PER_GROUP));
		}
	#elif defined ARGS
		if (thread_id.x == 0) {
			RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
			const uint at = VOXEL_VIEWS_AT + u_view_slot * VOXEL_VIEW_STRIDE;
			const uint end = min(counters.Load(0), u_brick_capacity);
			const uint begin = min(counters.Load(at), end);
			counters.Store4(at + VOXEL_VIEW_COMMAND, uint4(begin, CUBE_INDEX_COUNT, end - begin, 0));
			counters.Store2(at + VOXEL_VIEW_COMMAND + 16, uint2(0, 0));
			if (u_hzb.mode == 1) {
				const uint occ = min(counters.Load(VOXEL_OCCLUDED_COUNT), u_occluded_capacity);
				const uint retest_groups = (occ + GROUP_SIZE - 1) / GROUP_SIZE;
				bindless_rw_buffers[u_args].Store3(16, dispatchGroups(retest_groups));
			}
		}
	#elif defined RETEST
		const uint index = thread_id.y * MAX_GROUPS_X * GROUP_SIZE + thread_id.x;
		RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
		const uint count = min(counters.Load(VOXEL_OCCLUDED_COUNT), u_occluded_capacity);
		if (index >= count) return;
		RWByteAddressBuffer occluded = bindless_rw_buffers[u_occluded];
		const uint4 brick_entry = occluded.Load4(index * VOXEL_BRICK_STRIDE);	// slot | (b << VOXEL_BRICK_INDEX_SHIFT), instance, header, brick record

		ByteAddressBuffer data = bindless_buffers[u_cluster_data];
		const VoxelClusterHeader hdr = data.Load<VoxelClusterHeader>(brick_entry.z);
		const int3 base_voxel = hdr.base_voxel;
		const float size = hdr.voxel_size;
		const float3 grid_origin = hdr.grid_origin;
		const uint3 brick_xyz = data.Load3(brick_entry.w);
		const int3 brick_coord = int3(brick_xyz.z & 1023, (brick_xyz.z >> 10) & 1023, (brick_xyz.z >> 20) & 1023);
		float3 sub_min, sub_max;
		uint sub_packed;
		unpackBrickSubBox(brick_xyz.x, brick_xyz.y, sub_min, sub_max, sub_packed);
		float4 rot;
		float3 position, scale;
		loadSceneTransform(u_instances, u_chunks, u_cam_p, u_cam_frac, brick_entry.x & VOXEL_BRICK_SLOT_MASK, brick_entry.y, rot, position, scale);
		const float3 center = position + rotateByQuat(rot, (grid_origin + (float3(base_voxel + brick_coord * 4) + 0.5 * (sub_min + sub_max)) * size) * scale);
		const float radius = 0.5 * length((sub_max - sub_min) * scale) * size;
		bool visible = !isOccludedHZB(u_hzb, center, radius, false);
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

		const uint at = waveAppendBool(counters, 0, visible);
		RWByteAddressBuffer bricks = bindless_rw_buffers[u_bricks];
		RWByteAddressBuffer quads = bindless_rw_buffers[u_quads];
		if (visible && at < u_brick_capacity) {
			bricks.Store4(at * VOXEL_BRICK_STRIDE, brick_entry);
			quads.Store4(at * VOXEL_BRICK_STRIDE, quad);
		}
	#else
		// EXPAND: CLUSTERS_PER_GROUP voxel clusters per group. 1. every thread sets up its cluster (in LDS), 2. prefix sum of the brick counts, 3. the bricks of all the clusters
		// of the group are numbered and one thread takes a brick at a time (the cluster of a brick is found by a binary search in the sums), so all the lanes work
		// until the last brick, not until the brick count of the largest cluster of the wave.
		const uint gtid = group_thread_id.x;
		const uint vc_idx = (group_id.y * MAX_GROUPS_X + group_id.x) * CLUSTERS_PER_GROUP + gtid;
		ByteAddressBuffer data = bindless_buffers[u_cluster_data];
		const bool hzb_prev = (u_hzb.mode == 1);

		if (gtid < CLUSTERS_PER_GROUP) {
			const uint sub = gtid;
			ByteAddressBuffer selected = bindless_buffers[u_selected];
			const uint selected_count = min(selected.Load(SELECTED_COUNT), u_selected_capacity);
			if (vc_idx >= selected_count) {
				g_clusters[sub].brick_count = 0;
			}
			else {
				const uint4 entry = selected.Load4(SELECTED_HEADER + vc_idx * SELECTED_STRIDE);	// slot, mesh, cluster, instance
				const uint cluster = entry.z & SELECTED_CLUSTER_MASK;

				ByteAddressBuffer meshes = bindless_buffers[u_meshes];
				const ClusterMesh cm = meshes.Load<ClusterMesh>(entry.y * CLUSTER_MESH_STRIDE);
				const uint cluster_at = cm.clusters + cluster * CLUSTER_STRIDE;

				const uint headers_offset = clusterHeadersOffset(cm);
				const uint header = headers_offset + cluster * CLUSTER_HEADER_STRIDE;
				const uint group_idx = data.Load(cluster_at + CLUSTER_GROUP);
				const uint stream_base = data.Load4(cm.group_table + group_idx * GROUP_STRIDE).x;
				const VoxelClusterHeader hdr = data.Load<VoxelClusterHeader>(header);
				const int3 base_voxel = hdr.base_voxel;
				const float3 grid_origin = hdr.grid_origin;
				const uint brick_count = min(hdr.brick_count, 64u);
				const float size = hdr.voxel_size;
				const uint payload = stream_base + (hdr.payload_bit_offset >> 3);

				float4 rot;
				float3 position, scale;
				loadSceneTransform(u_instances, u_chunks, u_cam_p, u_cam_frac, entry.x, entry.w, rot, position, scale);
				const float3 voxel_scale = size * scale;

				const float3 base_pos = position + rotateByQuat(rot, (grid_origin + float3(base_voxel) * size) * scale);
				const float3 vx_ws = rotateByQuat(rot, float3(voxel_scale.x, 0, 0));
				const float3 vy_ws = rotateByQuat(rot, float3(0, voxel_scale.y, 0));
				const float3 vz_ws = rotateByQuat(rot, float3(0, 0, voxel_scale.z));

				ClusterSetup cs;
				cs.slot = entry.x;
				cs.instance = entry.w;
				cs.header = header;
				cs.payload = payload;
				cs.brick_count = brick_count;
				cs.voxel_scale = voxel_scale;
				cs.base_pos = base_pos;
				cs.vx_ws = vx_ws;
				cs.vy_ws = vy_ws;
				cs.vz_ws = vz_ws;

				const float4 dp0 = float4(base_pos + u_draw_delta.xyz, 1.0);
				cs.draw_base = float4(dot(u_draw_vp[0], dp0), dot(u_draw_vp[1], dp0), dot(u_draw_vp[2], dp0), dot(u_draw_vp[3], dp0));
				cs.draw_x = float4(dot(u_draw_vp[0].xyz, vx_ws), dot(u_draw_vp[1].xyz, vx_ws), dot(u_draw_vp[2].xyz, vx_ws), dot(u_draw_vp[3].xyz, vx_ws));
				cs.draw_y = float4(dot(u_draw_vp[0].xyz, vy_ws), dot(u_draw_vp[1].xyz, vy_ws), dot(u_draw_vp[2].xyz, vy_ws), dot(u_draw_vp[3].xyz, vy_ws));
				cs.draw_z = float4(dot(u_draw_vp[0].xyz, vz_ws), dot(u_draw_vp[1].xyz, vz_ws), dot(u_draw_vp[2].xyz, vz_ws), dot(u_draw_vp[3].xyz, vz_ws));

				cs.clip_base = 0; cs.clip_x = 0; cs.clip_y = 0; cs.clip_z = 0; cs.row_scale = 0;
				if (u_hzb.mode != 0 && brick_count > 1) {
					const float4 vp0 = hzb_prev ? u_hzb.prev_vp[0] : u_hzb.cur_vp[0];
					const float4 vp1 = hzb_prev ? u_hzb.prev_vp[1] : u_hzb.cur_vp[1];
					const float4 vp2 = hzb_prev ? u_hzb.prev_vp[2] : u_hzb.cur_vp[2];
					const float4 vp3 = hzb_prev ? u_hzb.prev_vp[3] : u_hzb.cur_vp[3];
					const float4 p0 = float4(hzb_prev ? base_pos + u_hzb.prev_delta.xyz : base_pos, 1.0);
					cs.clip_base = float4(dot(vp0, p0), dot(vp1, p0), dot(vp2, p0), dot(vp3, p0));
					cs.clip_x = float4(dot(vp0.xyz, vx_ws), dot(vp1.xyz, vx_ws), dot(vp2.xyz, vx_ws), dot(vp3.xyz, vx_ws));
					cs.clip_y = float4(dot(vp0.xyz, vy_ws), dot(vp1.xyz, vy_ws), dot(vp2.xyz, vy_ws), dot(vp3.xyz, vy_ws));
					cs.clip_z = float4(dot(vp0.xyz, vz_ws), dot(vp1.xyz, vz_ws), dot(vp2.xyz, vz_ws), dot(vp3.xyz, vz_ws));
					cs.row_scale = float2(length(vp0.xyz), length(vp1.xyz));
				}
				g_clusters[sub] = cs;
			}
		}
		if (gtid < CLUSTERS_PER_GROUP) g_scan[gtid] = g_clusters[gtid].brick_count;
		GroupMemoryBarrierWithGroupSync();
		[unroll]
		for (uint off = 1; off < CLUSTERS_PER_GROUP; off <<= 1) {
			const uint add = (gtid < CLUSTERS_PER_GROUP && gtid >= off) ? g_scan[gtid - off] : 0;
			GroupMemoryBarrierWithGroupSync();
			if (gtid < CLUSTERS_PER_GROUP) g_scan[gtid] += add;
			GroupMemoryBarrierWithGroupSync();
		}
		const uint total_bricks = g_scan[CLUSTERS_PER_GROUP - 1];

		RWByteAddressBuffer counters = bindless_rw_buffers[u_counters];
		RWByteAddressBuffer bricks = bindless_rw_buffers[u_bricks];
		RWByteAddressBuffer quads = bindless_rw_buffers[u_quads];
		RWByteAddressBuffer occluded = bindless_rw_buffers[u_occluded];

		// total_bricks is the same for the whole group, so the wave operations below are reached by all the lanes
		for (uint base_b = 0; base_b < total_bricks; base_b += EXPAND_GROUP_SIZE) {
			const uint idx = base_b + gtid;
			const bool valid = idx < total_bricks;
			uint sub = 0;
			uint b = 0;
			if (valid) {
				// the first cluster whose inclusive sum is over idx
				uint lo = 0;
				uint hi = CLUSTERS_PER_GROUP - 1;
				[unroll]
				for (uint step = 0; step < CLUSTERS_LOG2; ++step) {
					const uint mid = (lo + hi) >> 1;
					if (g_scan[mid] > idx) hi = mid; else lo = mid + 1;
				}
				sub = lo;
				b = idx - (sub > 0 ? g_scan[sub - 1] : 0);
			}
			const ClusterSetup cs = g_clusters[sub];
			bool vis = false;
			bool occ = false;
			bool stat_cand = false, stat_frustum = false, stat_hzb = false;	// VOXEL_STATS
			uint4 quad = 0;
			if (valid) {
				const uint3 brick_xyz = data.Load3(cs.payload + b * 16);
				if ((brick_xyz.x | brick_xyz.y) != 0) {
					stat_cand = true;
					float3 sub_min, sub_max;
					uint sub_packed;
					unpackBrickSubBox(brick_xyz.x, brick_xyz.y, sub_min, sub_max, sub_packed);
					const float3 bc = float3(brick_xyz.z & 1023, (brick_xyz.z >> 10) & 1023, (brick_xyz.z >> 20) & 1023);
					if (cs.brick_count == 1) {
						vis = true;
					}
					else {
						const float3 vc = bc * 4.0 + 0.5 * (sub_min + sub_max);
						const float3 center = cs.base_pos + cs.vx_ws * vc.x + cs.vy_ws * vc.y + cs.vz_ws * vc.z;
						const float radius = 0.5 * length((sub_max - sub_min) * cs.voxel_scale);
						stat_frustum = true;
						if (insideFrustum(u_planes, center, radius)) {
							stat_frustum = false;
							if (u_hzb.mode != 0) {
								const float4 clip = cs.clip_base + cs.clip_x * vc.x + cs.clip_y * vc.y + cs.clip_z * vc.z;
								if (isOccludedClip(clip, radius, cs.row_scale * radius, hzb_prev ? u_hzb.prev : u_hzb.cur, u_hzb.size, u_hzb.mips)) {
									occ = hzb_prev;
									stat_hzb = true;
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
						const float4 c0 = cs.draw_base + cs.draw_x * lo.x + cs.draw_y * lo.y + cs.draw_z * lo.z;
						bool behind_camera;
						quad = computeBrickQuad(c0, cs.draw_x * ext.x, cs.draw_y * ext.y, cs.draw_z * ext.z, u_draw_delta.w != 0.0, sub_packed, behind_camera);
						if (behind_camera) vis = false;
					}
				}
			}

			#ifdef VOXEL_STATS
				// per view, words 40, 44, 48 of its block: the bricks of the selected voxel clusters, the ones outside of the frustum, the ones that the pyramid hides
				// (bricks of clusters with a single brick are not tested)
				const uint wc = WaveActiveCountBits(stat_cand);
				const uint wf = WaveActiveCountBits(stat_frustum);
				const uint wh = WaveActiveCountBits(stat_hzb);
				if (WaveIsFirstLane()) {
					const uint stats_at = VOXEL_VIEWS_AT + u_view_slot * VOXEL_VIEW_STRIDE + VOXEL_VIEW_STATS;
					uint unused;
					if (wc > 0) counters.InterlockedAdd(stats_at, wc, unused);
					if (wf > 0) counters.InterlockedAdd(stats_at + 4, wf, unused);
					if (wh > 0) counters.InterlockedAdd(stats_at + 8, wh, unused);
				}
			#endif
			const uint4 brick_record = uint4(cs.slot | (b << VOXEL_BRICK_INDEX_SHIFT), cs.instance, cs.header, cs.payload + b * 16);
			const uint vis_at = waveAppendBool(counters, 0, vis);
			if (vis && vis_at < u_brick_capacity) {
				bricks.Store4(vis_at * VOXEL_BRICK_STRIDE, brick_record);
				quads.Store4(vis_at * VOXEL_BRICK_STRIDE, quad);
			}

			const uint occ_at = waveAppendBool(counters, VOXEL_OCCLUDED_COUNT, occ);
			if (occ && occ_at < u_occluded_capacity) {
				occluded.Store4(occ_at * VOXEL_BRICK_STRIDE, brick_record);
			}
		}
	#endif
}
