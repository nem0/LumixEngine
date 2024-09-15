//@surface
//@include "pipelines/common.hlsli"

cbuffer Model : register(b4) {
	float4x4 u_model;
};

struct Output {
	float4 color : TEXCOORD0;
	float4 position : SV_POSITION;
};

struct VSInput {
	float3 position : TEXCOORD0;
	float4 color : TEXCOORD1;
};

Output mainVS(VSInput input) {
	Output output;
	output.color = float4(pow(abs(input.color.rgb), 2.2f.xxx), input.color.a);
	output.position = mul(float4(input.position, 1), mul(u_model, Pass_view_projection));
	return output;
}

struct Input {
	float4 color : TEXCOORD0;
};

float4 mainPS(Input input) : SV_TARGET {
	return input.color;
}
