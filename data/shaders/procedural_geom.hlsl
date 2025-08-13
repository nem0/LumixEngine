//@surface
#include "shaders/common.hlsli"

//@uniform "Material color", "color", {1, 1, 1, 1}
//@uniform "Roughness", "normalized_float", 1
//@uniform "Metallic", "normalized_float", 0
//@uniform "Emission", "float", 0
//@uniform "Translucency", "normalized_float", 0

//@texture_slot "Albedo", "textures/common/white.tga"
//@texture_slot "Normal", "textures/common/default_normal.tga"
//@texture_slot "Roughness", "textures/common/white.tga"
//@texture_slot "Metallic", "", "HAS_METALLICMAP"
//@texture_slot "Ambient occlusion", "", "HAS_AMBIENT_OCCLUSION_TEX"

cbuffer Drawcall : register(b4) {
	float4x4 u_ls_to_ws;
	uint u_material_index;
};

struct VSOutput {
	float3 normal : TEXCOORD0;
	float3 tangent : TEXCOORD1;
	float2 uv : TEXCOORD2;
	float3 pos_ws : TEXCOORD3;
	float4 position : SV_POSITION;
};

#define ATTR(X) TEXCOORD##X
struct VSInput {
	float3 position : ATTR(0);
	float2 uv : ATTR(1);
	float3 normal : ATTR(2);
	float3 tangent : ATTR(3);
};

VSOutput mainVS(VSInput input) {
	VSOutput output;
	output.pos_ws = transformPosition(input.position, u_ls_to_ws).xyz;
	output.position = transformPosition(output.pos_ws, Pass_ws_to_ndc);
	output.normal = mul(input.normal, (float3x3)u_ls_to_ws);
	output.tangent = mul(input.tangent, (float3x3)u_ls_to_ws);
	output.uv = input.uv;
	return output;
}

Surface getSurface(VSOutput input) {
	MaterialData material = getMaterialData(u_material_index);
	Surface surface;
	surface.albedo = sampleBindless(LinearSampler, material.t_albedo, input.uv).rgb;

	float3x3 tbn = float3x3(
		input.tangent, 
		input.normal, 
		normalize(cross(input.normal, input.tangent))
	);
	
	surface.N.xz = sampleBindless(LinearSampler, material.t_normal, input.uv).xy * 2 - 1;
	surface.N.y = sqrt(saturate(1 - dot(surface.N.xz, surface.N.xz))); 
	surface.N = mul(surface.N, tbn);
	surface.roughness = material.u_roughness * sampleBindless(LinearSampler, material.t_roughness, input.uv).r;

	surface.metallic = material.u_metallic;
	#ifdef HAS_METALLICMAP
		surface.metallic *= sampleBindless(LinearSampler, material.t_metallic, input.uv).r;
	#endif
	#ifdef HAS_AMBIENT_OCCLUSION_TEX
		surface.ao = sampleBindless(LinearSampler, material.t_ambient_occlusion, input.uv).r;
	#else
		surface.ao = 1;
	#endif
	surface.shadow = 1;
	surface.alpha = 1;
	surface.emission = material.u_emission;
	surface.translucency = material.u_translucency;
	surface.pos_ws = input.pos_ws;
	surface.V = normalize(-surface.pos_ws);
	surface.motion = computeStaticObjectMotionVector(input.pos_ws.xyz);
	return surface;
}

#ifdef DEPTH
	void mainPS() {
	}
#elif defined DEFERRED || defined GRASS
	GBufferOutput mainPS(VSOutput input) {
		Surface surface = getSurface(input);
		GBufferOutput output = packSurface(surface);
		return output;
	}
#else 
	float4 mainPS(VSOutput input) : SV_TARGET {
		Surface surface = getSurface();

		float linear_depth = dot(surface.pos_ws.xyz, Pass_view_dir.xyz);
		Cluster cluster = getClusterLinearDepth(linear_depth, input.position.xy);
		float4 result;
		result.rgb = computeLighting(cluster, surface, Global_light_dir.xyz, Global_light_color.rgb * Global_light_intensity, u_shadowmap, u_shadow_atlas, u_reflection_probes, input.position.xy);

		result.a = surface.alpha;
		return result;
	}
#endif
