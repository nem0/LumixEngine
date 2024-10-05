//@surface
#include "shaders/common.hlsli"

cbuffer DC : register(b4) {
	float4x4 u_matrix;
	TextureHandle u_texture;
};

struct VSInput {
	float2 position : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float4 color : TEXCOORD2;
};

struct VSOutput {
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float4 position : SV_POSITION;
};

VSOutput mainVS(VSInput input) {
	VSOutput output;
	output.color = input.color;
	output.uv = input.uv;
	output.position = transformPosition(float3(input.position, 0), u_matrix);
	return output;
}

float4 mainPS(VSOutput input) :SV_TARGET {
	return input.color * sampleBindlessLod(LinearSampler, u_texture, input.uv, 0);
}