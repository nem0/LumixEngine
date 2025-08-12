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
	float4 pos_ws : TEXCOORD0;
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
	cbuffer ModelState : register(b4) {
		float fur_scale;
		float fur_gravity;
		float layers;
		uint material_index;
		row_major float4x4 mtx;
		row_major float4x4 prev_matrix;
		row_major float2x4 bones[255];
	}
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

VSOutput mainVS(VSInput input) {
	VSOutput output;
	#ifdef HAS_MATERIAL_INDEX_ATTR
		output.i_material_index = input.i_material_index;
	#endif
	#ifdef HAS_LOD
		output.lod = 0;
	#endif
	#ifdef TANGENT_ATTR
		output.tangent = input.tangent;
	#endif
	#ifdef AO_ATTR
		output.ao = input.ao;
	#endif
	#ifdef UV0_ATTR
		output.uv = input.uv;
	#endif
	#ifdef AUTOINSTANCED
		float3 p = input.position.xyz * input.i_scale.xyz;
		output.pos_ws = float4(input.i_pos_lod.xyz + rotateByQuat(input.i_rot, p), 1);
		output.position = mul(output.pos_ws, Pass_ws_to_ndc);
		output.normal = rotateByQuat(input.i_rot, input.normal);
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
		output.pos_ws = float4(input.i_pos_scale.xyz + rotateByQuat(rot_quat, p), 1);
		output.position = mul(output.pos_ws, Pass_ws_to_ndc);
	#elif defined GRASS
		output.normal = rotateByQuat(input.i_rot, input.normal);
		#ifdef TANGENT_ATTR
			output.tangent = rotateByQuat(input.i_rot, input.tangent);
		#endif
		float3 p = input.position;
		output.pos_y = p.y;
		output.pos_ws = float4(input.i_pos_scale.xyz + rotateByQuat(input.i_rot, input.position * input.i_pos_scale.w), 1);
		output.pos_ws.xyz += u_grass_origin;
		#ifdef COLOR0_ATTR
			//output.color = input.color;
		#endif
		output.position = mul(output.pos_ws, Pass_ws_to_ndc);
	#elif defined DYNAMIC
		output.normal = rotateByQuat(input.i_rot, input.normal);
		#ifdef TANGENT_ATTR
			output.tangent = rotateByQuat(input.i_rot, input.tangent);
		#endif
		output.pos_ws = float4(input.i_pos_lod.xyz + rotateByQuat(input.i_rot, input.position * input.i_scale.xyz), 1);
		output.position = mul(output.pos_ws, Pass_ws_to_ndc);
		output.prev_ndcpos_no_jitter = float4(input.i_prev_pos_lod.xyz + rotateByQuat(input.i_prev_rot, input.position * input.i_prev_scale.xyz), 1);
		output.prev_ndcpos_no_jitter = mul(output.prev_ndcpos_no_jitter, mul(Global_ws_to_ndc_no_jitter, Global_reprojection));
	#elif defined SKINNED
		float2x4 dq = mul(bones[input.indices.x], input.weights.x);
		float w = dot(bones[input.indices.y][0], bones[input.indices.x][0]) < 0 ? -input.weights.y : input.weights.y;
		dq += mul(bones[input.indices.y], w);
		w = dot(bones[input.indices.z][0], bones[input.indices.x][0]) < 0 ? -input.weights.z : input.weights.z;
		dq += mul(bones[input.indices.z], w);
		w = dot(bones[input.indices.w][0], bones[input.indices.x][0]) < 0 ? -input.weights.w : input.weights.w;
		dq += mul(bones[input.indices.w], w);
	
		dq *= 1 / length(dq[0]);

		float3x3 m = (float3x3)mtx;
		output.normal = mul(rotateByQuat(dq[0], input.normal), m);
		#ifdef TANGENT_ATTR
			output.tangent = mul(rotateByQuat(dq[0], input.tangent), m);
		#endif
		float3 mpos;
		#ifdef FUR
			v_fur_layer = gl_InstanceID / layers;
			mpos = input.position + (input.normal + float3(0, -fur_gravity * input.fur_layer, 0)) * input.fur_layer * fur_scale;
		#else
			mpos = input.position;
		#endif
		output.pos_ws = transformPosition(transformByDualQuat(dq, mpos), mtx);
		output.position = mul(output.pos_ws, Pass_ws_to_ndc);
		// TODO previous frame bone positions
		output.prev_ndcpos_no_jitter = transformPosition(transformByDualQuat(dq, mpos), prev_matrix);
		output.prev_ndcpos_no_jitter = mul(output.prev_ndcpos_no_jitter, mul(Global_ws_to_ndc_no_jitter, Global_reprojection));
	#else
		float3x3 rot_mtx = (float3x3)model_mtx;
		output.normal =  mul(input.normal, rot_mtx);
		#ifdef TANGENT_ATTR
			output.tangent = mul(input.tangent, rot_mtx);
		#endif
		output.pos_ws = transformPosition(input.position, model_mtx);
		output.position = mul(output.pos_ws, Pass_ws_to_ndc);
	#endif
	return output;
}

Surface getSurfaceEx(VSOutput input) {
	#ifdef HAS_MATERIAL_INDEX_ATTR
		Surface data = getSurface(input, input.i_material_index);
	#else
		Surface data = getSurface(input, material_index);
	#endif
	float4 p = mul(input.pos_ws, Global_ws_to_ndc_no_jitter);
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
