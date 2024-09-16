//@surface
//@include "pipelines/common.hlsli"

cbuffer Model : register(b4) {
	float4x4 u_model;
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
	output.color = float4(pow(abs(input.color.rgb), 2.2f.xxx), input.color.a);
	output.position = mul(float4(input.position, 1), mul(u_model, Pass_view_projection));
	return output;
}

float4 mainPS(VSOutput input) : SV_TARGET {
	return input.color;
}
