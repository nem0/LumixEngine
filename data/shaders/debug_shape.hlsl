//@surface
#include "shaders/common.hlsli"

cbuffer Model : register(b4) {
	float4x4 u_ls_to_ws;
};

struct VSOutput {
	float4 color : TEXCOORD0;
	float4 position : SV_POSITION;
};

struct VSInput {
	float3 position : TEXCOORD0;
	float4 color : TEXCOORD1;
};

VSOutput mainVS(VSInput input) {
	VSOutput output;
	float3 color_srgb = pow(abs(input.color.rgb), 2.2);
	output.color = float4(color_srgb, input.color.a);
	output.position = transformPosition(input.position, u_ls_to_ws, Pass_ws_to_ndc);
	return output;
}

float4 mainPS(VSOutput input) : SV_TARGET {
	return input.color;
}
