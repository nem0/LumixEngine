// you can include this file and implement getSurface function for custom surface shaders

#if !defined GRASS
	#define HAS_LOD
#endif

#define ATTR(X) TEXCOORD##X
struct VSInput {
	float3 position : ATTR(0);
	float3 normal : ATTR(NORMAL_ATTR);
	
	#ifdef UV0_ATTR
		float2 uv : ATTR(UV0_ATTR);
	#endif

	#ifdef AO_ATTR
		float ao : ATTR(AO_ATTR);
	#endif
	
	#ifdef TANGENT_ATTR
		float3 tangent : ATTR(TANGENT_ATTR);
	#endif
	
	#ifdef INDICES_ATTR
		int4 indices : ATTR(INDICES_ATTR);
		float4 weights : ATTR(WEIGHTS_ATTR);
	#endif
	
	#ifdef AUTOINSTANCED
		float4 i_rot : ATTR(INSTANCE0_ATTR);
		float4 i_pos_lod : ATTR(INSTANCE1_ATTR);
		float4 i_scale : ATTR(INSTANCE2_ATTR);
		uint i_material_index : ATTR(INSTANCE3_ATTR);
		#define HAS_MATERIAL_INDEX_ATTR
	#elif defined INSTANCED
		float4 i_rot_lod : ATTR(INSTANCE0_ATTR);
		float4 i_pos_scale : ATTR(INSTANCE1_ATTR);
	#elif defined DYNAMIC
		float4 i_rot : ATTR(INSTANCE0_ATTR);
		float4 i_pos_lod : ATTR(INSTANCE1_ATTR);
		float4 i_scale : ATTR(INSTANCE2_ATTR);
		float4 i_prev_rot : ATTR(INSTANCE3_ATTR);
		float4 i_prev_pos_lod : ATTR(INSTANCE4_ATTR);
		float4 i_prev_scale : ATTR(INSTANCE5_ATTR);
		uint i_material_index : ATTR(INSTANCE6_ATTR);
		#define HAS_MATERIAL_INDEX_ATTR
	#elif defined SKINNED
		uint3 i_uint : ATTR(INSTANCE0_ATTR);
		float3 i_pos : ATTR(INSTANCE1_ATTR);
		float4 i_rot : ATTR(INSTANCE2_ATTR);
		float3 i_scale : ATTR(INSTANCE3_ATTR);
		float3 i_prev_pos : ATTR(INSTANCE4_ATTR);
		float4 i_prev_rot : ATTR(INSTANCE5_ATTR);
		float3 i_prev_scale : ATTR(INSTANCE6_ATTR);
		#define HAS_MATERIAL_INDEX_ATTR
	#elif defined GRASS
		float4 i_pos_scale : ATTR(INSTANCE0_ATTR);
		float4 i_rot : ATTR(INSTANCE1_ATTR);
		#ifdef COLOR0_ATTR
			//float4 color : ATTR(COLOR0_ATTR)
		#endif
	#else
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
};

#ifdef SKINNED
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

#ifdef SKINNED
	float2x4 getBones(VSInput input, uint bone_index) {
		uint offset = input.i_uint.z + bone_index * 32;
		float4 a = asfloat(bindless_buffers[input.i_uint.y].Load4(offset));
		float4 b = asfloat(bindless_buffers[input.i_uint.y].Load4(offset + 16));
		return float2x4(a, b);
	}
#endif

Surface getSurface(VSOutput input, uint material_index);

VSOutput mainVS(VSInput input) {
	VSOutput output;
	#ifdef SKINNED
		output.i_material_index = input.i_uint.x;
	#elif defined HAS_MATERIAL_INDEX_ATTR
		output.i_material_index = input.i_material_index;
	#endif
	#ifdef HAS_LOD
		output.lod = 0;
	#endif
	#ifdef AO_ATTR
		output.ao = input.ao;
	#endif
	#ifdef UV0_ATTR
		output.uv = input.uv;
	#endif
	#ifdef AUTOINSTANCED
		float3 p = input.position.xyz * input.i_scale.xyz;
		output.pos_ws = input.i_pos_lod.xyz + rotateByQuat(input.i_rot, p);
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
		output.normal = rotateByQuat(input.i_rot, input.normal);
		#ifdef TANGENT_ATTR
			output.tangent = rotateByQuat(input.i_rot, input.tangent);
		#endif
		
	#ifdef HAS_LOD
			output.lod = input.i_pos_lod.w;
		#endif
	#elif defined INSTANCED
		float4 rot_quat = float4(input.i_rot_lod.xyz, 0);
		rot_quat.w = sqrt(saturate(1 - dot(rot_quat.xyz, rot_quat.xyz)));
		output.normal = rotateByQuat(rot_quat, input.normal);
		#ifdef TANGENT_ATTR
			output.tangent = rotateByQuat(rot_quat, input.tangent);
		#endif
		float3 p = input.position * input.i_pos_scale.w;
		output.pos_ws = input.i_pos_scale.xyz + rotateByQuat(rot_quat, p);
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
	#elif defined GRASS
		output.normal = rotateByQuat(input.i_rot, input.normal);
		#ifdef TANGENT_ATTR
			output.tangent = rotateByQuat(input.i_rot, input.tangent);
		#endif
		float3 p = input.position;
		output.pos_y = p.y;
		output.pos_ws = input.i_pos_scale.xyz + rotateByQuat(input.i_rot, input.position * input.i_pos_scale.w);
		output.pos_ws += u_grass_origin;
		#ifdef COLOR0_ATTR
			//output.color = input.color;
		#endif
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
	#elif defined DYNAMIC
		output.normal = rotateByQuat(input.i_rot, input.normal);
		#ifdef TANGENT_ATTR
			output.tangent = rotateByQuat(input.i_rot, input.tangent);
		#endif
		output.pos_ws = input.i_pos_lod.xyz + rotateByQuat(input.i_rot, input.position * input.i_scale.xyz);
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
		output.prev_ndcpos_no_jitter = float4(input.i_prev_pos_lod.xyz + rotateByQuat(input.i_prev_rot, input.position * input.i_prev_scale.xyz), 1);
		output.prev_ndcpos_no_jitter = mul(output.prev_ndcpos_no_jitter, mul(Global_ws_to_ndc_no_jitter, Global_reprojection));
	#elif defined SKINNED
		float2x4 dq = mul(getBones(input, input.indices.x), input.weights.x);
		float w = dot(getBones(input, input.indices.y)[0], getBones(input, input.indices.x)[0]) < 0 ? -input.weights.y : input.weights.y;
		dq += mul(getBones(input, input.indices.y), w);
		w = dot(getBones(input, input.indices.z)[0], getBones(input, input.indices.x)[0]) < 0 ? -input.weights.z : input.weights.z;
		dq += mul(getBones(input, input.indices.z), w);
		w = dot(getBones(input, input.indices.w)[0], getBones(input, input.indices.x)[0]) < 0 ? -input.weights.w : input.weights.w;
		dq += mul(getBones(input, input.indices.w), w);
	
		dq *= 1 / length(dq[0]);

		output.normal = rotateByQuat(input.i_rot, rotateByQuat(dq[0], input.normal));
		#ifdef TANGENT_ATTR
			output.tangent = rotateByQuat(input.i_rot, rotateByQuat(dq[0], input.tangent));
		#endif
		float3 mpos;
		mpos = input.position;
		output.pos_ws = rotateByQuat(input.i_rot, transformByDualQuat(dq, mpos)) * input.i_scale + input.i_pos;
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
		// TODO previous frame bone positions
		float3 prevpos = rotateByQuat(input.i_prev_rot, transformByDualQuat(dq, mpos)) * input.i_prev_scale + input.i_prev_pos;
		output.prev_ndcpos_no_jitter = mul(float4(prevpos, 1), mul(Global_ws_to_ndc_no_jitter, Global_reprojection));
	#else
		float3x3 rot_mtx = (float3x3)model_mtx;
		output.normal =  mul(input.normal, rot_mtx);
		#ifdef TANGENT_ATTR
			output.tangent = mul(input.tangent, rot_mtx);
		#endif
		output.pos_ws = transformPosition(input.position, model_mtx).xyz;
		output.position = mul(float4(output.pos_ws, 1), Pass_ws_to_ndc);
	#endif
	return output;
}

Surface getSurfaceEx(VSOutput input) {
	#ifdef HAS_MATERIAL_INDEX_ATTR
		Surface data = getSurface(input, input.i_material_index);
	#else
		Surface data = getSurface(input, material_index);
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
	GBufferOutput mainPS(VSOutput input) {
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
