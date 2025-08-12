//@surface
//@texture_slot "Albedo", "textures/common/white.tga"

#include "shaders/common.hlsli"

cbuffer Drawcall : register(b4) {
	float4x4 u_ls_to_ws;
	uint u_material_index;
};

cbuffer Drawcall2 : register(b5) {
	TextureHandle u_gbuffer_depth;
};

#define ATTR(X) TEXCOORD##X
struct VSInput {
	float3 position : TEXCOORD0;
	float3 normal : ATTR(NORMAL_ATTR);
	#ifdef UV0_ATTR
		float2 uv : ATTR(UV0_ATTR);
	#endif
};

struct VSOutput {
	float2 uv : TEXCOORD0;
	float distance_squared : TEXCOORD1;
	float3 normal : TEXCOORD2;
	float4 position : SV_POSITION;
};

VSOutput mainVS(VSInput input) {
	VSOutput output;
	#ifdef UV0_ATTR
		output.uv = input.uv;
	#else
		output.uv = 0;
	#endif
	float4 pos_vs = transformPosition(input.position, u_ls_to_ws, Global_ws_to_vs);
	output.distance_squared = dot(pos_vs.xyz, pos_vs.xyz);
	output.normal = input.normal;
	output.position = transformPosition(pos_vs, Global_vs_to_ndc_no_jitter);
	return output;
}

float4 mainPS(VSOutput input) : SV_TARGET {
	MaterialData material = getMaterialData(u_material_index);
	float2 screen_uv = input.position.xy * Global_rcp_framebuffer_size;
	float3 pos_ws = getPositionWS(u_gbuffer_depth, screen_uv);
	float4 albedo = bindless_textures[material.t_albedo].Sample(LinearSampler, input.uv);
	#ifdef ALPHA_CUTOUT
		if (albedo.a < 0.5) discard;
	#endif
	// just some fake shading to make 3d icons look better
	float NdotL = dot(input.normal, 1);
	float shading = saturate(max(0, -NdotL) + 0.25 * max(0, NdotL) + 0.25);
	float3 output = albedo.rgb * shading;
	
	float distance_squared = dot(pos_ws, pos_ws);
	bool is_behind = distance_squared < input.distance_squared;
	if (is_behind) output *= 0.25;
	
	return float4(output, 1);
}