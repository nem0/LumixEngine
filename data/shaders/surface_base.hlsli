// you can include this file and implement getSurface function for custom surface shaders

#if !defined GRASS
	#define HAS_LOD
#endif

#include "engine/shaders/vertex_pulling.hlsli"

// Vertices and instances are pulled by the vertex shader, see vertex_pulling.hlsli
struct VSInput {
	uint vertex_id : SV_VertexID;
	#if defined AUTOINSTANCED || defined INSTANCED || defined MESH_PARTICLE || defined DYNAMIC || defined SKINNED || defined GRASS
		uint instance_id : SV_InstanceID;
	#endif
	#if defined AUTOINSTANCED || defined DYNAMIC || defined SKINNED
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
	#if defined DYNAMIC || defined SKINNED
		float4 prev_ndcpos_no_jitter : TEXCOORD4;
	#endif
	#ifdef HAS_LOD
		float lod : TEXCOORD5;
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

#ifdef SKINNED
#elif defined MESH_PARTICLE
	cbuffer ModelState : register(b4) {
		float3 position;
		uint material_index;
		uint u_particle_stride;	// in bytes
	};
#elif defined INSTANCED
	cbuffer ModelState : register(b4) {
		uint material_index;
	};
#elif defined AUTOINSTANCED
#elif defined GRASS
	cbuffer ModelState : register(b4) {
		float3 u_grass_origin;
		float u_distance;
		uint material_index;
	};
#elif defined DYNAMIC
#else
	cbuffer ModelState : register(b4) {
		row_major float4x4 model_mtx;
		uint material_index;
	};
#endif

Surface getSurface(VSOutput input, uint material_index);

// Instance data of the draw is pulled by the instance index, see vertex_pulling.hlsli.
#if defined AUTOINSTANCED
	// 48 bytes
	struct Instance {
		float4 rot;
		float4 pos_lod;
		float4 scale;
		uint material_index;
	};

	Instance loadInstance(VSInput input) {
		ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
		const uint offset = pullInstanceAddress(input.instance_id, 48);
		Instance r;
		r.rot = asfloat(buffer.Load4(offset));
		r.pos_lod = asfloat(buffer.Load4(offset + 16));
		const uint4 last = buffer.Load4(offset + 32);
		r.scale = float4(asfloat(last.xyz), 1);
		r.material_index = last.w;
		return r;
	}
#elif defined INSTANCED
	// 32 bytes
	struct Instance {
		float4 rot_lod;
		float4 pos_scale;
	};

	Instance loadInstance(VSInput input) {
		ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
		const uint offset = pullInstanceAddress(input.instance_id, 32);
		Instance r;
		r.rot_lod = asfloat(buffer.Load4(offset));
		r.pos_scale = asfloat(buffer.Load4(offset + 16));
		return r;
	}
#elif defined MESH_PARTICLE
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
#elif defined DYNAMIC
	// 96 bytes: rotation, position and lod, scale, the same for the previous frame, material index
	struct Instance {
		float4 rot;
		float4 pos_lod;
		float4 scale;
		float4 prev_rot;
		float4 prev_pos_lod;
		float4 prev_scale;
		uint material_index;
	};

	Instance loadInstance(VSInput input) {
		ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
		const uint offset = pullInstanceAddress(input.instance_id, 96);
		Instance r;
		r.rot = asfloat(buffer.Load4(offset));
		r.pos_lod = asfloat(buffer.Load4(offset + 16));
		r.scale = asfloat(buffer.Load4(offset + 32));
		r.prev_rot = asfloat(buffer.Load4(offset + 48));
		r.prev_pos_lod = asfloat(buffer.Load4(offset + 64));
		const uint4 last = buffer.Load4(offset + 80);
		r.prev_scale = float4(asfloat(last.xyz), 0);
		r.material_index = last.w;
		return r;
	}
#elif defined SKINNED
	// 92 bytes
	struct Instance {
		uint material_index;
		uint bones_buffer;		// bindless
		uint bones_offset;
		float3 pos;
		float4 rot;
		float3 scale;
		float3 prev_pos;
		float4 prev_rot;
		float3 prev_scale;
	};

	Instance loadInstance(VSInput input) {
		ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
		const uint offset = pullInstanceAddress(input.instance_id, 92);
		Instance r;
		const uint3 ids = buffer.Load3(offset);
		r.material_index = ids.x;
		r.bones_buffer = ids.y;
		r.bones_offset = ids.z;
		r.pos = asfloat(buffer.Load3(offset + 12));
		r.rot = asfloat(buffer.Load4(offset + 24));
		r.scale = asfloat(buffer.Load3(offset + 40));
		r.prev_pos = asfloat(buffer.Load3(offset + 52));
		r.prev_rot = asfloat(buffer.Load4(offset + 64));
		r.prev_scale = asfloat(buffer.Load3(offset + 80));
		return r;
	}

	float2x4 getBones(Instance inst, uint bone_index) {
		uint offset = inst.bones_offset + bone_index * 32;
		float4 a = asfloat(bindless_buffers[inst.bones_buffer].Load4(offset));
		float4 b = asfloat(bindless_buffers[inst.bones_buffer].Load4(offset + 16));
		return float2x4(a, b);
	}
#endif

// Depth and shadow passes read only the position, the interpolants below are for the pixel shader of colour passes and alpha cutout
#if defined DEPTH && !defined ALPHA_CUTOUT
	#define DEPTH_ONLY_VS
#endif

VSOutput mainVS(VSInput input) {
	#ifdef CLUSTER_MESH
		VSVertex vtx = pullVertex(input.vertex_id, input.instance_id);
	#else
		VSVertex vtx = pullVertex(input.vertex_id);
	#endif
	#ifdef DEPTH_ONLY_VS
		VSOutput output = (VSOutput)0;
	#else
		VSOutput output;
	#endif
	#if defined AUTOINSTANCED || defined INSTANCED || defined MESH_PARTICLE || defined GRASS || defined DYNAMIC || defined SKINNED
		Instance inst = loadInstance(input);
	#endif
	#ifdef HAS_MATERIAL_INDEX_ATTR
		output.i_material_index = inst.material_index;
	#endif
	#ifdef HAS_LOD
		output.lod = 0;
	#endif
	#ifndef DEPTH_ONLY_VS
		#ifdef AO_ATTR
			output.ao = vtx.ao;
		#endif
		#ifdef UV0_ATTR
			output.uv = vtx.uv;
		#endif
	#endif
	#ifdef AUTOINSTANCED
		float3 p = vtx.position.xyz * inst.scale.xyz;
		output.pos_ws = inst.pos_lod.xyz + rotateByQuat(inst.rot, p);
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
		#ifndef DEPTH_ONLY_VS
			output.normal = rotateByQuat(inst.rot, vtx.normal);
			#ifdef TANGENT_ATTR
				output.tangent = rotateByQuat(inst.rot, vtx.tangent);
			#endif
		#endif
		
	#ifdef HAS_LOD
			output.lod = inst.pos_lod.w;
		#endif
	#elif defined INSTANCED || defined MESH_PARTICLE
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
	#elif defined DYNAMIC
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
			output.prev_ndcpos_no_jitter = float4(inst.prev_pos_lod.xyz + rotateByQuat(inst.prev_rot, vtx.position * inst.prev_scale.xyz), 1);
			output.prev_ndcpos_no_jitter = mul(output.prev_ndcpos_no_jitter, mul(Global_ws_to_ndc_no_jitter, Global_reprojection));
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
			output.prev_ndcpos_no_jitter = mul(float4(prevpos, 1), mul(Global_ws_to_ndc_no_jitter, Global_reprojection));
		#endif
	#else
		#ifndef DEPTH_ONLY_VS
			float3x3 rot_mtx = (float3x3)model_mtx;
			output.normal =  mul(vtx.normal, rot_mtx);
			#ifdef TANGENT_ATTR
				output.tangent = mul(vtx.tangent, rot_mtx);
			#endif
		#endif
		output.pos_ws = transformPosition(vtx.position, model_mtx).xyz;
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
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
	#if defined DYNAMIC || defined SKINNED
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
		#ifdef HAS_LOD
			if (ditherLOD(input.lod, input.position.xy)) discard;
		#endif
		#ifdef ALPHA_CUTOUT
			Surface surface = getSurfaceEx(input);
			if(surface.alpha < 0.5) discard;
		#endif
	}
#elif defined DEFERRED || defined GRASS
	#ifdef CLUSTER_DEBUG_TRIANGLES
		GBufferOutput mainPS(VSOutput input, uint primitive_id : SV_PrimitiveID) {
			g_primitive_id = primitive_id;
	#else
		GBufferOutput mainPS(VSOutput input) {
	#endif
		#ifdef HAS_LOD
			if (ditherLOD(input.lod, input.position.xy)) discard;
		#endif

		Surface surface = getSurfaceEx(input);
		#ifdef ALPHA_CUTOUT
			if(surface.alpha < 0.5) discard;
		#endif

		return packSurface(surface);
	}
#else
	cbuffer Drawcall2 : register(b5) {
		uint u_shadowmap;
		uint u_shadow_atlas;
		uint u_reflection_probes;
	};

	float4 mainPS(VSOutput input, float4 frag_coord : SV_POSITION) : SV_TARGET{
		#ifdef HAS_LOD
			if (ditherLOD(input.lod, input.position.xy)) discard;
		#endif
		
		Surface data = getSurfaceEx(input);
		data.pos_ws = input.pos_ws.xyz;
	
		float linear_depth = dot(data.pos_ws.xyz, Pass_view_dir.xyz);
		Cluster cluster = getClusterLinearDepth(linear_depth, frag_coord.xy);
		float4 result;
		result.rgb = computeLighting(cluster, data, Global_light_dir.xyz, Global_light_color.rgb * Global_light_intensity, u_shadowmap, u_shadow_atlas, u_reflection_probes, frag_coord.xy);

		#ifdef ALPHA_CUTOUT
			if(data.alpha < 0.5) discard;
		#endif
		result.a = data.alpha;
		return result;
	}
#endif
