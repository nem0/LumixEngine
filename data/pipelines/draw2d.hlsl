//@surface
//@include "pipelines/common.hlsli"

cbuffer DC : register(b4) {
	float4x4 u_matrix;
	uint u_texture;
};

struct VSInput {
	float2 position : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float4 color : TEXCOORD2;
};

struct Output {
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float4 position : SV_POSITION;
};

Output mainVS(VSInput input) {
	Output output;
	output.color = input.color;
	output.uv = input.uv;
	output.position = mul(float4(input.position, 0, 1), u_matrix);
	return output;
}

struct Input {
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
};

float4 mainPS(Input input) :SV_TARGET {
	return input.color * sampleBindlessLod(LinearSampler, u_texture, input.uv, 0);
}