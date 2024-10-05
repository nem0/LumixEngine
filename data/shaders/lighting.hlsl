//@surface
#include "shaders/common.hlsli"

struct VSOutput {
	float2 uv : TEXCOORD0;
	float4 position : SV_POSITION;
};

cbuffer Textures : register(b4) {
	TextureHandle u_gbuffer0;
	TextureHandle u_gbuffer1;
	TextureHandle u_gbuffer2;
	TextureHandle u_gbuffer3;
	TextureHandle u_gbuffer_depth;
	TextureHandle u_shadowmap;
	TextureHandle u_shadow_atlas;
	TextureHandle u_reflection_probes;
};

VSOutput mainVS(uint vertexID : SV_VertexID) {
	VSOutput output;
	output.position = fullscreenQuad(vertexID, output.uv);
	return output;
}

float4 mainPS(VSOutput input) : SV_Target {
	float ndc_depth;
	Surface surface = unpackSurface(input.uv, u_gbuffer0, u_gbuffer1, u_gbuffer2, u_gbuffer3, u_gbuffer_depth, ndc_depth);
	Cluster cluster = getCluster(ndc_depth, input.position.xy);
	
	float4 res;
	res.rgb = computeLighting(cluster
		, surface
		, Global_light_dir.xyz
		, Global_light_color.rgb * Global_light_intensity
		, u_shadowmap
		, u_shadow_atlas
		, u_reflection_probes
		, input.position.xy);
	res.a = 1;
	return res;
}
