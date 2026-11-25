//@surface
#include "engine/shaders/common.hlsli"
#include "engine/shaders/gpu_scene_layout.hlsli"

// Draws the bricks that voxel_bricks.hlsl expanded from the selected voxel clusters (see MeshVoxelCluster in src/renderer/mesh_clusters.h). A brick is 4 x 4 x 4 voxels, an
// instance of a cube that is drawn with DrawIndexedIndirect (the indices of the cube are 0 .. 35, so SV_VertexID is the corner of the cube). The pixel shader marches the
// ray of the pixel through the voxels of the brick (a 64 bit mask) in the space of the model and shades the first voxel that it hits. The depth of the voxel is written, so
// the voxels are not cubes that the depth test sees, it is the depth that the pixel shader writes. Both faces of the cube are drawn (the camera can be inside), the pixels of
// the far face march the same ray as the ones of the near face.
// Only the deferred and the depth (and shadow) passes draw voxels, DEFERRED and DEPTH come from the bucket of the view.

cbuffer VoxelDraw : register(b4) {
	uint u_bricks;			// bindless, the pool of bricks (voxel_bricks.hlsl)
	uint u_cluster_data;	// bindless, the buffer of cluster data (the clusters, their headers and the voxels)
	uint u_instances;		// bindless, GPUScene::instances
	uint u_chunks;			// bindless, GPUSceneChunks::chunks
	int3 u_cam_p;			// the camera in the fixed point of the GPU scene
	uint u_quads;			// bindless, precomputed screen-space quad bounds per brick (voxel_bricks.hlsl)
	float3 u_cam_frac;
	uint u_voxel_padding2;
};

// the first brick of the draw, SV_InstanceID does not include the start instance of an indirect draw
cbuffer DrawId : register(b6) {
	uint u_draw_id;
};

#define BRICK_STRIDE 16

#if defined DEPTH && defined SOLID_DEPTH
struct VSOutput {
	float4 position : SV_POSITION;
};
#else
struct VSOutput {
	noperspective centroid float4 position : SV_POSITION;	// SV_DepthLessEqual needs this interpolation mode of the position
	float3 pos_ws : TEXCOORD0;
	float3 local_pos : TEXCOORD1;					// ray point on the near plane of the sub-box in [0, 4] brick-voxel space
	nointerpolation float3 ray_param : TEXCOORD2;	// ro_local (perspective) or rd_local (ortho), in [0, 4] brick-voxel space
	nointerpolation uint4 brick : TEXCOORD3;		// mask low, mask high, attributes offset (unused in DEPTH), packed sub_min/sub_max (2 bits each)
	#ifndef DEPTH
		nointerpolation float4 rot : TEXCOORD4;
		#ifdef CLUSTER_DEBUG
			nointerpolation uint debug_id : TEXCOORD5;
		#endif
	#endif
};
#endif

// the transform of a model instance or of an instance of a chunk of an instanced model, relative to the camera, see voxel_bricks.hlsl
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

// the frame is a rotation of (1, 0, 0) to the tangent and (0, 0, 1) to the normal, see packClusterFrame in cluster_builder.cpp
float3 unpackFrameNormal(uint packed) {
	const uint largest = packed >> 30;
	const float3 abc = float3((int)(packed << 22) >> 22, (int)(packed << 12) >> 22, (int)(packed << 2) >> 22) * (0.70710678 / 511.0);
	const float d = sqrt(max(0, 1 - dot(abc, abc)));
	float4 q;
	if (largest == 0) q = float4(d, abc);
	else if (largest == 1) q = float4(abc.x, d, abc.y, abc.z);
	else if (largest == 2) q = float4(abc.x, abc.y, d, abc.z);
	else q = float4(abc, d);
	return float3(2 * (q.x * q.z + q.w * q.y), 2 * (q.y * q.z - q.w * q.x), 1 - 2 * (q.x * q.x + q.y * q.y));
}

#ifdef CLUSTER_DEBUG
float3 voxelDebugColor(uint id) {
	uint h = id * 747796405u + 2891336453u;
	h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
	h = (h >> 22u) ^ h;
	return float3(h & 255u, (h >> 8) & 255u, (h >> 16) & 255u) / 255.0 * 0.8 + 0.1;
}
#endif

VSOutput mainVS(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
	const uint at = (instance_id + u_draw_id) * BRICK_STRIDE;
	const uint4 q = bindless_buffers[u_quads].Load4(at);	// packed ndc_min.xy, packed ndc_max.xy, qz, sub_packed
	const float2 ndc_min = float2(q.x & 0xffffu, q.x >> 16) * (2.0 / 65535.0) - 1.0;
	const float2 ndc_max = float2(q.y & 0xffffu, q.y >> 16) * (2.0 / 65535.0) - 1.0;

	// First 6 indices of m_voxel_cube_ib are (0, 2, 1, 1, 2, 3); mapping bit0->min_x and bit1->max_y gives CCW winding in NDC.
	const float qx = (vertex_id & 1) ? ndc_min.x : ndc_max.x;
	const float qy = (vertex_id & 2) ? ndc_max.y : ndc_min.y;
	const float qz = asfloat(q.z);

	VSOutput output;
	output.position = float4(qx, qy, qz, 1.0);
	#if !(defined DEPTH && defined SOLID_DEPTH)
		ByteAddressBuffer bricks = bindless_buffers[u_bricks];
		const uint4 entry = bricks.Load4(at);	// slot | (b << 26), instance, the header of the cluster, the record of the brick
		const uint slot = entry.x & 0x03ffffffu;
		const uint brick_idx = entry.x >> 26;
		const uint header = entry.z & ~15u;

		ByteAddressBuffer data = bindless_buffers[u_cluster_data];
		const int3 base_voxel = asint(data.Load3(header));
		const uint4 h1 = data.Load4(header + 16);
		const float size = asfloat(h1.w);
		const float3 grid_origin = asfloat(data.Load3(header + 32));
		const uint4 brick = data.Load4(entry.w);	// mask low, mask high, the position in bricks, the first voxel
		const int3 brick_coord = int3(brick.z & 1023, (brick.z >> 10) & 1023, (brick.z >> 20) & 1023);
		#ifndef DEPTH
			const uint attributes = entry.w + (h1.y - brick_idx) * 16;
		#endif

		float4 rot;
		float3 position_ws, scale;
		loadTransform(slot, entry.y, rot, position_ws, scale);
		const float3 brick_origin = grid_origin + float3(base_voxel + brick_coord * 4) * size;
		const float4 inv_rot = float4(-rot.xyz, rot.w);
		const bool is_ortho = abs(Pass_ws_to_ndc[0][3]) + abs(Pass_ws_to_ndc[1][3]) + abs(Pass_ws_to_ndc[2][3]) < 1e-6;

		const float4 wp = mul(float4(qx, qy, qz, 1.0), Pass_ndc_to_ws);
		const float3 pos_ws = is_ortho ? wp.xyz : (wp.xyz / wp.w);
		output.local_pos = (rotateByQuat(inv_rot, pos_ws - position_ws) / scale - brick_origin) / size;
		output.ray_param = is_ortho
			? rotateByQuat(inv_rot, Pass_view_dir.xyz) / (scale * size)
			: (rotateByQuat(inv_rot, -position_ws) / scale - brick_origin) / size;
		#ifdef DEPTH
			output.brick = uint4(brick.x, brick.y, 0, q.w);
		#else
			output.brick = uint4(brick.x, brick.y, attributes + brick.w * 8, q.w);
			output.rot = rot;
			#ifdef CLUSTER_DEBUG
				output.debug_id = (header >> 4);
			#endif
		#endif
		output.pos_ws = pos_ws;
	#endif
	return output;
}

float safeInverse(float v) {
	return 1.0 / (abs(v) < 1e-9 ? (v < 0 ? -1e-9 : 1e-9) : v);
}

struct Hit {
	bool valid;
	#ifdef DEPTH
		float depth;
	#else
		float3 pos_ws;
		float3 normal;		// world space
		float3 color;
		float ao;
	#endif
};

float hitDepth(float3 pos_ws) {
	const float4 clip = mul(float4(pos_ws, 1), Pass_ws_to_ndc);
	return clip.z / clip.w;
}

#if !(defined DEPTH && defined SOLID_DEPTH)
Hit marchBrick(VSOutput input) {
	Hit hit = (Hit)0;
	const uint2 mask = input.brick.xy;
	const bool is_ortho = abs(Pass_ws_to_ndc[0][3]) + abs(Pass_ws_to_ndc[1][3]) + abs(Pass_ws_to_ndc[2][3]) < 1e-6;

	// Ray in [0, 4] brick-voxel space against the tight occupied sub-box [sub_min, sub_max].
	const float3 ro = is_ortho ? input.local_pos : input.ray_param;
	const float3 rd = is_ortho ? input.ray_param : (input.local_pos - input.ray_param);
	const float3 inv_rd = float3(safeInverse(rd.x), safeInverse(rd.y), safeInverse(rd.z));

	const uint sp = input.brick.w;
	const int3 sub_min_i = int3(sp & 3, (sp >> 2) & 3, (sp >> 4) & 3);
	const int3 sub_max_i = int3((sp >> 6) & 3, (sp >> 8) & 3, (sp >> 10) & 3);
	const float3 t0 = (float3(sub_min_i) - ro) * inv_rd;
	const float3 t1 = (float3(sub_max_i + 1) - ro) * inv_rd;
	const float3 tn = min(t0, t1);
	const float3 tf = max(t0, t1);
	const float t_enter = is_ortho ? max(max(tn.x, tn.y), tn.z) : max(max(max(tn.x, tn.y), tn.z), 0.0);
	const float t_exit = min(min(tf.x, tf.y), tf.z);
	if (t_enter > t_exit) return hit;

	if (all(sub_min_i == sub_max_i)) {
		hit.valid = true;
		#ifdef DEPTH
			hit.depth = is_ortho
				? (input.position.z + t_enter * mul(float4(Pass_view_dir.xyz, 0), Pass_ws_to_ndc).z)
				: hitDepth(input.pos_ws * t_enter);
		#else
			hit.pos_ws = is_ortho ? (input.pos_ws + Pass_view_dir.xyz * t_enter) : (input.pos_ws * t_enter);
			const uint2 voxel = bindless_buffers[u_cluster_data].Load2(input.brick.z);
			hit.normal = normalize(rotateByQuat(input.rot, unpackFrameNormal(voxel.x)));
			#ifdef CLUSTER_DEBUG
				const uint bit = sub_min_i.x + (sub_min_i.y << 2) + (sub_min_i.z << 4);
				#ifdef VOXEL_DEBUG_BRICKS
					hit.color = voxelDebugColor(input.brick.z);
				#elif defined CLUSTER_DEBUG_TRIANGLES
					hit.color = voxelDebugColor(input.debug_id * 2654435761u + bit + input.brick.z);
				#else
					hit.color = voxelDebugColor(input.debug_id);
				#endif
			#else
				hit.color = float3(voxel.y & 0xff, (voxel.y >> 8) & 0xff, (voxel.y >> 16) & 0xff) * (1.0 / 255.0);
			#endif
			hit.ao = (voxel.y >> 24) * (1.0 / 255.0);
		#endif
		return hit;
	}

	float t = t_enter;
	const float3 p = ro + rd * (t + 1e-4 * rsqrt(max(dot(rd, rd), 1e-18)));
	int3 cell = clamp(int3(floor(p)), sub_min_i, sub_max_i);

	const int3 step_dir = int3(rd.x < 0 ? -1 : 1, rd.y < 0 ? -1 : 1, rd.z < 0 ? -1 : 1);
	const float3 t_delta = abs(inv_rd);
	float3 t_next = (float3(cell) + float3(step_dir > 0) - ro) * inv_rd;

	[loop]
	for (uint i = 0; i < 10; ++i) {
		const uint bit = cell.x + (cell.y << 2) + (cell.z << 4);
		const uint word = bit < 32 ? mask.x : mask.y;
		const uint local_bit = bit & 31;
		if ((word >> local_bit) & 1) {
			hit.valid = true;
			#ifdef DEPTH
				hit.depth = is_ortho
					? (input.position.z + t * mul(float4(Pass_view_dir.xyz, 0), Pass_ws_to_ndc).z)
					: hitDepth(input.pos_ws * t);
			#else
				hit.pos_ws = is_ortho ? (input.pos_ws + Pass_view_dir.xyz * t) : (input.pos_ws * t);
				const uint below = (1u << local_bit) - 1;
				const uint index = bit < 32 ? countbits(mask.x & below) : countbits(mask.x) + countbits(mask.y & below);
				const uint2 voxel = bindless_buffers[u_cluster_data].Load2(input.brick.z + index * 8);
				hit.normal = normalize(rotateByQuat(input.rot, unpackFrameNormal(voxel.x)));
				#ifdef CLUSTER_DEBUG
					#ifdef VOXEL_DEBUG_BRICKS
							hit.color = voxelDebugColor(input.brick.z);	// the offset of the attributes is unique for each brick
						#elif defined CLUSTER_DEBUG_TRIANGLES
						hit.color = voxelDebugColor(input.debug_id * 2654435761u + bit + input.brick.z);
					#else
						hit.color = voxelDebugColor(input.debug_id);
					#endif
				#else
					hit.color = float3(voxel.y & 0xff, (voxel.y >> 8) & 0xff, (voxel.y >> 16) & 0xff) * (1.0 / 255.0);
				#endif
				hit.ao = (voxel.y >> 24) * (1.0 / 255.0);
			#endif
			return hit;
		}
		const float3 s = float3(
			t_next.x <= t_next.y && t_next.x <= t_next.z,
			t_next.y < t_next.x && t_next.y <= t_next.z,
			t_next.z < t_next.x && t_next.z < t_next.y
		);
		t = dot(t_next, s);
		t_next += t_delta * s;
		cell += step_dir * int3(s);
		if (any(cell < sub_min_i) || any(cell > sub_max_i)) return hit;
	}
	return hit;
}
#endif

#ifdef DEPTH
	#ifdef SOLID_DEPTH
		void mainPS(VSOutput input) {}
	#else
		float mainPS(VSOutput input) : SV_DepthLessEqual {
			const Hit hit = marchBrick(input);
			if (!hit.valid) discard;
			return hit.depth;
		}
	#endif
#elif defined DEFERRED
	struct Output {
		float4 gbuffer0 : SV_Target0;
		float4 gbuffer1 : SV_Target1;
		float4 gbuffer2 : SV_Target2;
		float4 gbuffer3 : SV_Target3;
		float depth : SV_DepthLessEqual;
	};

	Output mainPS(VSOutput input) {
		const Hit hit = marchBrick(input);
		if (!hit.valid) discard;

		Surface surface;
		surface.albedo = hit.color;
		surface.alpha = 1;
		surface.roughness = 0.8;
		surface.metallic = 0;
		surface.emission = 0;
		surface.translucency = 0.3;
		surface.ao = hit.ao;
		surface.shadow = 1;
		surface.pos_ws = hit.pos_ws;
		surface.V = normalize(-hit.pos_ws);
		// two sided: the normal faces the viewer
		surface.N = dot(hit.normal, surface.V) < 0 ? -hit.normal : hit.normal;
		surface.motion = computeStaticObjectMotionVector(hit.pos_ws);

		const GBufferOutput gb = packSurface(surface);
		Output output;
		output.gbuffer0 = gb.gbuffer0;
		output.gbuffer1 = gb.gbuffer1;
		output.gbuffer2 = gb.gbuffer2;
		output.gbuffer3 = gb.gbuffer3;
		output.depth = hitDepth(hit.pos_ws);
		return output;
	}
#else
	// not drawn: the program of the other passes does nothing
	void mainPS(VSOutput input) {
		discard;
	}
#endif
