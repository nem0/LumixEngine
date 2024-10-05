//@surface

//@texture_slot "Albedo", "textures/common/white.tga"
//@texture_slot "Normal", "", "HAS_NORMAL"
//@texture_slot "Self shadow", "", "HAS_SELFSHADOW"
//@texture_slot "Depth", "", "HAS_DEPTHMAP"

//@define "ALPHA_CUTOUT"

//@uniform "Material color", "color", {1, 1, 1, 1}
//@uniform "Roughness", "normalized_float", 1
//@uniform "Metallic", "normalized_float", 0
//@uniform "Emission", "float", 0
//@uniform "Translucency", "normalized_float", 0
//@uniform "Center", "float3", {0, 0, 0}
//@uniform "Radius", "float", 1

#include "shaders/common.hlsli"

struct VSOutput {
	float2 uv : TEXCOORD0;
	float3 normal : TEXCOORD1;
	float3 tangent : TEXCOORD2;
	float4 pos_ws : TEXCOORD3;
	#if !defined DEPTH && defined HAS_SELFSHADOW
		float4 shadow_coefs : TEXCOORD4;
	#endif
	float lod : TEXCOORD5;
	float4 position : SV_POSITION;
};
struct VSInput {
	float3 position : TEXCOORD0;
	float2 uv  : TEXCOORD1;
	#define ATTR(X) TEXCOORD##X
	#ifdef INSTANCED
		float4 i_rot_lod : ATTR(INSTANCE0_ATTR);
		float4 i_pos_scale : ATTR(INSTANCE1_ATTR);
	#elif defined AUTOINSTANCED
		float4 i_rot : ATTR(INSTANCE0_ATTR);
		float4 i_pos_lod : ATTR(INSTANCE1_ATTR);
		float4 i_scale : ATTR(INSTANCE2_ATTR);
	#endif
};

#ifdef INSTANCED
#elif defined AUTOINSTANCED
#else
	#define USE_MATRIX
	cbuffer ModelState : register(b4) {
		row_major float4x4 model_mtx;
	};
#endif

float2 dirToGrid(float3 vec) {
	vec.y = min(vec.y, -0.001);
	vec = normalize(vec);
	vec.xz /= dot(1.0, abs(vec) );
	return float2(vec.x + vec.z, vec.x - vec.z) * 0.5 + 0.5;
}


#ifdef USE_MATRIX
	VSOutput mainVS(VSInput input) {
		VSOutput output;
		float3x3 to_model_space = (float3x3)model_mtx;
		#ifdef DEPTH
			float3 N = normalize(Global_light_dir.xyz);
		#else
			float3 instance_pos = mul(float4(0, 0, 0, 1), model_mtx).xyz;
			float3 N = normalize(instance_pos);
		#endif
		
		float3x3 tangent_space;
		tangent_space[0] = normalize(cross(N, float3(0, 1, 0)));
		tangent_space[1] = normalize(cross(tangent_space[0], N));
		tangent_space[2] = cross(tangent_space[0], tangent_space[1]);

		float3 vd = float3(N.x, N.y, N.z);
		vd = mul(vd, to_model_space);
		#if !defined DEPTH && defined HAS_SELFSHADOW
			float3 ld = mul(-Global_light_dir.xyz, to_model_space);
			output.shadow_coefs = max(float4(ld.x, -ld.z, -ld.x, ld.z), 0);
			output.shadow_coefs /= dot(output.shadow_coefs, 1);
		#endif
		float2 grid = dirToGrid(normalize(vd));
		output.uv = input.uv / 9 + int2(grid * 9) / 9.0;

		#ifdef DEPTH
			// move to avoid selfshadow
			float3 p = u_center.xyz + mul(input.position + float3(0, 0, u_center.y) - u_center.xyz, tangent_space);
		#else
			float3 p = u_center.xyz + mul(input.position - u_center.xyz, tangent_space);
		#endif
		p = transformPosition(p, model_mtx).xyz;
		
		output.lod = 1;
		output.tangent = tangent_space[0];
		output.normal = tangent_space[2];
		output.pos_ws = float4(p, 1);

		output.position = mul(output.pos_ws, Pass_ws_to_ndc);
		return output;
	}
#else
	VSOutput mainVS(VSInput input) {
		VSOutput output;
		float3x3 tangent_space;

		#if defined INSTANCED
			float3 instance_pos = input.i_pos_scale.xyz;
			float scale = input.i_pos_scale.w;
			float4 to_model_space = float4(input.i_rot_lod.xyz, sqrt(1 - dot(input.i_rot_lod.xyz, input.i_rot_lod.xyz)));
			output.lod = input.i_rot_lod.w;
		#else
			float3 instance_pos = input.i_pos_lod.xyz;
			float3 scale = input.i_scale.xyz;
			float4 to_model_space = input.i_rot * float4(1, 1, 1, -1);
			output.lod = input.i_pos_lod.w;
		#endif

		#ifdef DEPTH
			float3 N = normalize(Global_light_dir.xyz);
		#else
			float3 N = normalize(instance_pos);
		#endif
		tangent_space[0] = normalize(cross(N, float3(0, 1, 0)));
		tangent_space[1] = normalize(cross(tangent_space[0], N));
		tangent_space[2] = cross(tangent_space[0], tangent_space[1]);

		float3 vd = float3(N.x, N.y, N.z);
		vd = rotateByQuat(to_model_space, vd);
		#if !defined DEPTH && defined HAS_SELFSHADOW
			float3 ld = rotateByQuat(to_model_space, -Global_light_dir.xyz);
			output.shadow_coefs = max(float4(ld.x, -ld.z, -ld.x, ld.z), 0);
			output.shadow_coefs /= dot(output.shadow_coefs, 1);
		#endif
		float2 grid = dirToGrid(normalize(vd));
		output.uv = input.uv / 9 + int2(grid * 9) / 9.0;

		#ifdef DEPTH
			// move to avoid selfshadow
			float3 p = u_center.xyz + mul(input.position + float3(0, 0, u_center.y) - u_center.xyz, tangent_space);
		#else
			float3 p = u_center.xyz + mul(input.position - u_center.xyz, tangent_space);
		#endif
		p *= scale;
		output.tangent = tangent_space[0];
		output.normal = tangent_space[2];
		output.pos_ws = float4(instance_pos + p, 1);

		output.position = mul(output.pos_ws, Pass_ws_to_ndc);
		return output;
	}
#endif

Surface getSurface(VSOutput input) {
	Surface data;
	float4 c = sampleBindless(LinearSampler, t_albedo, input.uv) * u_material_color;
	data.albedo = c.rgb;
	data.alpha = c.a;
	#ifdef ALPHA_CUTOUT
		if(data.alpha < 0.5) discard;
	#endif
	float3x3 tbn = float3x3(
		normalize(input.tangent),
		normalize(input.normal),
		normalize(cross(input.normal, input.tangent))
	);
	
	data.pos_ws = input.pos_ws.xyz;
	data.V = normalize(-data.pos_ws);
	data.roughness = u_roughness;
	data.metallic = u_metallic;
	#ifdef HAS_NORMAL
		data.N.xz = sampleBindless(LinearSampler, t_normal, input.uv).xy * 2 - 1;
		data.N.y = sqrt(saturate(1 - dot(data.N.xz, data.N.xz))); 		
		data.N = mul(data.N, tbn);
	#else
		data.N = Global_light_dir.xyz;
	#endif
	data.emission = u_emission;
	data.translucency = u_translucency;
	data.ao = 1;
	data.motion = computeStaticObjectMotionVector(input.pos_ws.xyz);
	
	#if !defined DEPTH && defined HAS_SELFSHADOW
		float4 self_shadow = sampleBindless(LinearSampler, t_self_shadow, input.uv);
		data.shadow = saturate(dot(self_shadow, input.shadow_coefs));
		data.shadow *= data.shadow;
		//data.shadow = 1;
		data.ao = dot(self_shadow, 1) * 0.25;
	#else
		data.shadow = 1;
	#endif
	
	return data;
}

#ifdef DEPTH
	void mainPS(VSOutput input) {
		if (ditherLOD(input.lod, input.position.xy)) discard;
		#ifdef ALPHA_CUTOUT
			float4 c = sampleBindless(LinearSampler, t_albedo, input.uv);
			if(c.a < 0.5) discard;
		#endif
	}
#elif defined DEFERRED
	struct Output {
		float4 gbuffer0 : SV_Target0;
		float4 gbuffer1 : SV_Target1;
		float4 gbuffer2 : SV_Target2;
		float4 gbuffer3 : SV_Target3;
		#ifdef HAS_DEPTHMAP
			float depth : SV_Depth;
		#endif
	};

	Output mainPS(VSOutput input) {
		Surface data = getSurface(input);
		GBufferOutput gb = packSurface(data);
		Output output;
		output.gbuffer0 = gb.gbuffer0;
		output.gbuffer1 = gb.gbuffer1;
		output.gbuffer2 = gb.gbuffer2;
		output.gbuffer3 = gb.gbuffer3;
		#ifdef HAS_DEPTHMAP
			float depth = sampleBindless(LinearSampler, t_depth, input.uv).x;
			float linear_z = toLinearDepth(input.position.z);
			output.depth = 0.1 / (linear_z + (depth - 0.5) * u_radius); // TODO remove hardcoded near plane 0.1
			output.depth = input.position.z;
		#endif

		return output;
	}
#else
	float mainPS(VSOutput input) : SV_TARGET
	{
		if (ditherLOD(v_lod)) discard;

		Surface surface = getSurface(input);
		float3 res = computeDirectLight(surface
			, Global_light_dir.xyz
			, Global_light_color.rgb * Global_light_intensity * surface.shadow);
		res += surface.emission * surface.albedo;

		float linear_depth = dot(surface.pos_ws.xyz, Pass_view_dir.xyz);
		Cluster cluster = getClusterLinearDepth(linear_depth);
		//res += pointLightsLighting(cluster, surface, shadow_atlas);
		res += envProbesLighting(cluster, surface);
		res += reflProbesLighting(cluster, surface, u_reflection_probes);

		return float4(res, surface.alpha);
	}
#endif
