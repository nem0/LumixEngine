// you can include this file and implement getSurface function for custom surface shaders

// the instance data of a draw are pulled (see below), a draw without any of the variants is a MESH_INSTANCE one
#if !defined SKINNED && !defined MESH_PARTICLE && !defined GRASS && !defined MESH_INSTANCE
	#define MESH_INSTANCE
#endif

#include "engine/shaders/vertex_pulling.hlsli"

// Vertices and instances are pulled by the vertex shader, see vertex_pulling.hlsli
struct VSInput {
	uint vertex_id : SV_VertexID;
	#if defined MESH_PARTICLE || defined MESH_INSTANCE || defined SKINNED || defined GRASS
		uint instance_id : SV_InstanceID;
	#endif
	#if (defined MESH_INSTANCE || defined SKINNED) && !defined MESH_PARTICLE && !defined GRASS
		#define HAS_MATERIAL_INDEX_ATTR
	#endif
};

struct VSOutput {
	float3 pos_ws : TEXCOORD0;
	float3 normal : TEXCOORD1;
	#ifdef UV0_ATTR
		float2 uv : TEXCOORD2;
	#endif
	#ifdef TANGENT_ATTR
		float3 tangent : TEXCOORD3;
	#endif
	#if defined HAS_MATERIAL_INDEX_ATTR && !defined DEPTH
		float4 prev_ndcpos_no_jitter : TEXCOORD4;
	#endif
	#ifdef AO_ATTR
		float ao : TEXCOORD6;
	#endif
	#ifdef GRASS
		#ifdef COLOR0_ATTR
			//float4 color : TEXCOORD6;
		#endif
		float pos_y : TEXCOORD7;
	#endif
	float4 position : SV_POSITION;
	#ifdef HAS_MATERIAL_INDEX_ATTR
		uint i_material_index : TEXCOORD8;
	#endif
	#ifdef CLUSTER_DEBUG
		nointerpolation uint cluster_id : TEXCOORD9;
	#endif
};

#if defined MESH_PARTICLE
	cbuffer ModelState : register(b4) {
		float3 position;
		uint material_index;
		uint u_particle_stride;	// in bytes
	};
#elif defined GRASS
	cbuffer ModelState : register(b4) {
		float3 u_grass_origin;
		float u_distance;
		uint material_index;
	};
#elif defined MESH_INSTANCE
#elif defined SKINNED
#endif

Surface getSurface(VSOutput input, uint material_index);

// Instance data of the draw is pulled by the instance index, see vertex_pulling.hlsli.
#if defined MESH_PARTICLE
	struct Instance {
		float4 rot_lod;
		float4 pos_scale;
	};

	// the first two outputs of the emitter, the stride is the size of all outputs
	Instance loadInstance(VSInput input) {
		ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
		const uint offset = pullInstanceAddress(input.instance_id, u_particle_stride);
		Instance r;
		r.rot_lod = asfloat(buffer.Load4(offset));
		r.pos_scale = asfloat(buffer.Load4(offset + 16));
		return r;
	}
#elif defined GRASS
	// 32 bytes
	struct Instance {
		float4 pos_scale;
		float4 rot;
	};

	Instance loadInstance(VSInput input) {
		ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
		const uint offset = pullInstanceAddress(input.instance_id, 32);
		Instance r;
		r.pos_scale = asfloat(buffer.Load4(offset));
		r.rot = asfloat(buffer.Load4(offset + 16));
		return r;
	}
#elif defined MESH_INSTANCE
	// 96 bytes: rotation, position and lod, scale, the same for the previous frame, material index
	struct Instance {
		float4 rot;
		float4 pos_lod;
		float4 scale;
		float4 prev_rot;
		float4 prev_pos_lod;
		float4 prev_scale;
		uint material_index;
		uint base_vertex;		// the word of prev_pos_lod.w, for POOLED_VERTICES
		bool moved;
	};

	Instance loadInstance(VSInput input) {
		ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
		Instance r;
		#ifdef INDIRECT_INSTANCE
			const uint offset = pullInstanceAddress(input.instance_id, DRAW_INSTANCE_STRIDE);
			const uint4 rec = buffer.Load4(offset + DRAW_INSTANCE_SLOT);
			r.base_vertex = rec.w;
			r.material_index = rec.z;
			float3 pos, scale, prev_pos, prev_scale;
			#ifdef DEPTH
				const bool load_prev = false;
			#else
				const bool load_prev = true;
			#endif
			r.moved = pullSceneTransform(rec.x, rec.y, load_prev, r.rot, pos, scale, r.prev_rot, prev_pos, prev_scale);
			r.pos_lod = float4(pos, (float)rec.w);
			r.scale = float4(scale, 0);
			r.prev_pos_lod = float4(prev_pos, 0);
			r.prev_scale = float4(prev_scale, 0);
		#else
			const uint offset = pullInstanceAddress(input.instance_id, 96);
			r.rot = asfloat(buffer.Load4(offset));
			r.pos_lod = asfloat(buffer.Load4(offset + 16));
			r.scale = asfloat(buffer.Load4(offset + 32));
			r.prev_rot = asfloat(buffer.Load4(offset + 48));
			r.prev_pos_lod = asfloat(buffer.Load4(offset + 64));
			r.base_vertex = asuint(r.prev_pos_lod.w);
			const uint4 last = buffer.Load4(offset + 80);
			r.prev_scale = float4(asfloat(last.xyz), 0);
			r.material_index = last.w;
			r.moved = true;
		#endif
		return r;
	}
#elif defined SKINNED
	// 96 bytes, the layout of MESH_INSTANCE: the word after the scale is the offset of the bones of the instance in bytes (u_bones_buffer)
	struct Instance {
		uint material_index;
		uint bones_offset;
		float3 pos;
		float4 rot;
		float3 scale;
		float3 prev_pos;
		float4 prev_rot;
		float3 prev_scale;
		uint base_vertex;		// the word after prev_pos, for POOLED_VERTICES
	};

	Instance loadInstance(VSInput input) {
		ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
		Instance r;
		#ifdef INDIRECT_INSTANCE
			const uint offset = pullInstanceAddress(input.instance_id, DRAW_INSTANCE_STRIDE);
			const uint4 rec = buffer.Load4(offset + DRAW_INSTANCE_SLOT);
			r.base_vertex = rec.w;
			r.bones_offset = buffer.Load(offset + DRAW_INSTANCE_BONES);
			r.material_index = rec.z;
			#ifdef DEPTH
				const bool load_prev = false;
			#else
				const bool load_prev = true;
			#endif
			pullSceneTransform(rec.x, rec.y, load_prev, r.rot, r.pos, r.scale, r.prev_rot, r.prev_pos, r.prev_scale);
		#else
			const uint offset = pullInstanceAddress(input.instance_id, 96);
			r.rot = asfloat(buffer.Load4(offset));
			r.base_vertex = buffer.Load(offset + 76);
			r.pos = asfloat(buffer.Load3(offset + 16));
			const uint4 scale = buffer.Load4(offset + 32);
			r.scale = asfloat(scale.xyz);
			r.bones_offset = scale.w;
			r.prev_rot = asfloat(buffer.Load4(offset + 48));
			r.prev_pos = asfloat(buffer.Load3(offset + 64));
			const uint4 last = buffer.Load4(offset + 80);
			r.prev_scale = asfloat(last.xyz);
			r.material_index = last.w;
		#endif
		return r;
	}

	float2x4 getBones(Instance inst, uint bone_index) {
		uint offset = inst.bones_offset + bone_index * 32;
		float4 a = asfloat(bindless_buffers[u_bones_buffer].Load4(offset));
		float4 b = asfloat(bindless_buffers[u_bones_buffer].Load4(offset + 16));
		return float2x4(a, b);
	}
#endif

// Depth and shadow passes read only the position (plus UV for alpha cutout), skipping normals, tangents, AO and previous-frame transforms
#ifdef DEPTH
	#define DEPTH_ONLY_VS
#endif

// a surface shader can change the pulled vertex: #define CUSTOM_VERTEX before including this file and implement customVertex after it
#ifdef CUSTOM_VERTEX
	void customVertex(inout VSVertex vtx, VSInput input);
#endif

VSOutput mainVS(VSInput input) {
	#if defined MESH_PARTICLE || defined GRASS || defined MESH_INSTANCE || defined SKINNED
		Instance inst = loadInstance(input);
	#endif
	#ifdef CLUSTER_MESH
		VSVertex vtx = pullVertex(input.vertex_id);
	#elif defined POOLED_VERTICES
		// the vertices of the meshes are in one buffer, the vertex id of a draw does not have the base vertex of its command: it is in the instance
		VSVertex vtx = pullVertex(input.vertex_id + inst.base_vertex);
	#else
		VSVertex vtx = pullVertex(input.vertex_id);
	#endif
	#ifdef CUSTOM_VERTEX
		customVertex(vtx, input);
	#endif
	#ifdef DEPTH_ONLY_VS
		VSOutput output = (VSOutput)0;
	#else
		VSOutput output;
	#endif
	#ifdef HAS_MATERIAL_INDEX_ATTR
		output.i_material_index = inst.material_index;
	#endif
	#ifndef DEPTH_ONLY_VS
		#ifdef AO_ATTR
			output.ao = vtx.ao;
		#endif
		#ifdef UV0_ATTR
			output.uv = vtx.uv;
		#endif
	#elif defined ALPHA_CUTOUT && defined UV0_ATTR
		output.uv = vtx.uv;
	#endif
	#if defined MESH_PARTICLE
		float4 rot_quat = float4(inst.rot_lod.xyz, 0);
		rot_quat.w = sqrt(saturate(1 - dot(rot_quat.xyz, rot_quat.xyz)));
		#ifndef DEPTH_ONLY_VS
			output.normal = rotateByQuat(rot_quat, vtx.normal);
			#ifdef TANGENT_ATTR
				output.tangent = rotateByQuat(rot_quat, vtx.tangent);
			#endif
		#endif
		float3 p = vtx.position * inst.pos_scale.w;
		#ifdef MESH_PARTICLE
			output.pos_ws = position + inst.pos_scale.xyz + rotateByQuat(rot_quat, p);
		#else
			output.pos_ws = inst.pos_scale.xyz + rotateByQuat(rot_quat, p);
		#endif
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
	#elif defined GRASS
		#ifndef DEPTH_ONLY_VS
			output.normal = rotateByQuat(inst.rot, vtx.normal);
			#ifdef TANGENT_ATTR
				output.tangent = rotateByQuat(inst.rot, vtx.tangent);
			#endif
		#endif
		float3 p = vtx.position;
		output.pos_y = p.y;
		output.pos_ws = inst.pos_scale.xyz + rotateByQuat(inst.rot, vtx.position * inst.pos_scale.w);
		output.pos_ws += u_grass_origin;
		#ifdef COLOR0_ATTR
			//output.color = input.color;
		#endif
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
	#elif defined MESH_INSTANCE
		#ifdef CLUSTER_DEBUG
			output.cluster_id = (uint)inst.pos_lod.w; // written by nanite.hlsl
		#endif
		#ifndef DEPTH_ONLY_VS
			output.normal = rotateByQuat(inst.rot, vtx.normal);
			#ifdef TANGENT_ATTR
				output.tangent = rotateByQuat(inst.rot, vtx.tangent);
			#endif
		#endif
		output.pos_ws = inst.pos_lod.xyz + rotateByQuat(inst.rot, vtx.position * inst.scale.xyz);
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
		#ifndef DEPTH_ONLY_VS
			const float3 prev_pos_ws = inst.moved
				? (inst.prev_pos_lod.xyz + rotateByQuat(inst.prev_rot, vtx.position * inst.prev_scale.xyz))
				: output.pos_ws;
			output.prev_ndcpos_no_jitter = mul(float4(prev_pos_ws, 1), Global_ws_to_prev_ndc_no_jitter);
		#endif
	#elif defined SKINNED
		float2x4 dq = mul(getBones(inst, vtx.indices.x), vtx.weights.x);
		float w = dot(getBones(inst, vtx.indices.y)[0], getBones(inst, vtx.indices.x)[0]) < 0 ? -vtx.weights.y : vtx.weights.y;
		dq += mul(getBones(inst, vtx.indices.y), w);
		w = dot(getBones(inst, vtx.indices.z)[0], getBones(inst, vtx.indices.x)[0]) < 0 ? -vtx.weights.z : vtx.weights.z;
		dq += mul(getBones(inst, vtx.indices.z), w);
		w = dot(getBones(inst, vtx.indices.w)[0], getBones(inst, vtx.indices.x)[0]) < 0 ? -vtx.weights.w : vtx.weights.w;
		dq += mul(getBones(inst, vtx.indices.w), w);
	
		dq *= 1 / length(dq[0]);

		#ifndef DEPTH_ONLY_VS
			output.normal = rotateByQuat(inst.rot, rotateByQuat(dq[0], vtx.normal));
			#ifdef TANGENT_ATTR
				output.tangent = rotateByQuat(inst.rot, rotateByQuat(dq[0], vtx.tangent));
			#endif
		#endif
		float3 mpos;
		mpos = vtx.position;
		output.pos_ws = rotateByQuat(inst.rot, transformByDualQuat(dq, mpos)) * inst.scale + inst.pos;
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
		// TODO previous frame bone positions
		#ifndef DEPTH_ONLY_VS
			float3 prevpos = rotateByQuat(inst.prev_rot, transformByDualQuat(dq, mpos)) * inst.prev_scale + inst.prev_pos;
			output.prev_ndcpos_no_jitter = mul(float4(prevpos, 1), Global_ws_to_prev_ndc_no_jitter);
		#endif
	#endif
	return output;
}

#ifdef CLUSTER_DEBUG_TRIANGLES
	static uint g_primitive_id; // SV_PrimitiveID, set by mainPS
#endif

Surface getSurfaceEx(VSOutput input) {
	#ifdef HAS_MATERIAL_INDEX_ATTR
		Surface data = getSurface(input, input.i_material_index);
	#else
		Surface data = getSurface(input, material_index);
	#endif
	#ifdef CLUSTER_DEBUG
		// random color per cluster, or per triangle of a cluster
		uint h = input.cluster_id * 747796405u + 2891336453u;
		#ifdef CLUSTER_DEBUG_TRIANGLES
			h += g_primitive_id * 2654435761u;
		#endif
		h = ((h >>((h >> 28u) + 4u)) ^ h) * 277803737u;
		h = (h >> 22u) ^ h;
		data.albedo = float3(h & 255u, (h >> 8) & 255u, (h >> 16) & 255u) / 255.0 * 0.8 + 0.1;
	#endif
	float4 p = mul(float4(input.pos_ws, 1), Global_ws_to_ndc_no_jitter);
	#if defined HAS_MATERIAL_INDEX_ATTR && !defined DEPTH
		float2 prev_pos_projected = input.prev_ndcpos_no_jitter.xy / input.prev_ndcpos_no_jitter.w;
		data.motion = prev_pos_projected.xy - p.xy / p.w;
	#else
		data.motion = computeStaticObjectMotionVector(input.pos_ws.xyz);
	#endif
	data.V = normalize(-input.pos_ws.xyz);
	return data;
}

#ifdef DEPTH
	void mainPS(VSOutput input) {
		#ifdef ALPHA_CUTOUT
			#ifdef HAS_MATERIAL_INDEX_ATTR
				Surface surface = getSurface(input, input.i_material_index);
			#else
				Surface surface = getSurface(input, material_index);
			#endif
			if(surface.alpha < 0.5) discard;
		#endif
	}
#elif defined DEFERRED || defined GRASS
	#ifdef CLUSTER_DEBUG_TRIANGLES
		// the debug view does not flip normals: the compiler does not accept SV_IsFrontFace next to SV_PrimitiveID
		GBufferOutput mainPS(VSOutput input, uint primitive_id : SV_PrimitiveID) {
			g_primitive_id = primitive_id;
			bool is_front = true;
	#else
		GBufferOutput mainPS(VSOutput input, bool is_front : SV_IsFrontFace) {
	#endif
		Surface surface = getSurfaceEx(input);
		#ifndef GRASS
			// double sided materials: the normal of a back face points away from the viewer
			if (!is_front) surface.N = -surface.N;
		#endif
		#ifdef ALPHA_CUTOUT
			if(surface.alpha < 0.5) discard;
		#endif

		return packSurface(surface);
	}
#else
	cbuffer Drawcall2 : register(b5) {
		TextureHandle u_shadowmap;
		TextureHandle u_shadow_atlas;
		TextureCubeArrayHandle u_reflection_probes;
	};

	float4 mainPS(VSOutput input, bool is_front : SV_IsFrontFace) : SV_TARGET{
		Surface data = getSurfaceEx(input);
		// double sided materials: the normal of a back face points away from the viewer
		if (!is_front) data.N = -data.N;
		data.pos_ws = input.pos_ws.xyz;
	
		float linear_depth = dot(data.pos_ws.xyz, Pass_view_dir.xyz);
		Cluster cluster = getClusterLinearDepth(linear_depth, input.position.xy);
		float4 result;
		result.rgb = computeLighting(cluster, data, Global_light_dir.xyz, Global_light_color.rgb * Global_light_intensity, u_shadowmap, u_shadow_atlas, u_reflection_probes, input.position.xy);

		#ifdef ALPHA_CUTOUT
			if(data.alpha < 0.5) discard;
		#endif
		result.a = data.alpha;
		return result;
	}
#endif
