//@surface

#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	TextureHandle u_input;
};

float4 mainVS(uint vertexID : SV_VertexID) : SV_POSITION{
	float2 uv;
	return fullscreenQuad(vertexID, uv);
}

float4 mainPS(float4 frag_coord : SV_POSITION) : SV_TARGET {
	uint2 ifrag_coord = uint2(frag_coord.xy);

	float4 v = bindless_textures[u_input][ifrag_coord];
	return float4(ACESFilm(v.rgb), 1);
}