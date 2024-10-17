//@surface
#include "shaders/common.hlsli"

cbuffer Drawcall : register(b4) {
	float4 u_inv_sm_size;
	TextureHandle u_input;
};

struct VSOutput {
	float2 tc0 : TEXCOORD0;
	float4 tc1 : TEXCOORD1;
	float4 tc2 : TEXCOORD2;
	float4 position : SV_POSITION;
};

VSOutput mainVS(uint vertex_id : SV_VertexID) {
	VSOutput output;
	float2 uv;
	output.position = fullscreenQuad(vertex_id, uv);
	output.tc0 = uv;
	#ifdef BLUR_H
		output.tc1 = float4(
			uv.x + 1.3846153846 * u_inv_sm_size.x, uv.y, 
			uv.x + 3.2307692308 * u_inv_sm_size.x, uv.y
		);
		output.tc2 = float4(
			uv.x - 1.3846153846 * u_inv_sm_size.x, uv.y, 
			uv.x - 3.2307692308 * u_inv_sm_size.x, uv.y
		);
	#else
		output.tc1 = float4(
			uv.x, uv.y + 1.3846153846 * u_inv_sm_size.y,
			uv.x, uv.y + 3.2307692308 * u_inv_sm_size.y
		);
		output.tc2 = float4(
			uv.x, uv.y - 1.3846153846 * u_inv_sm_size.y, 
			uv.x, uv.y - 3.2307692308 * u_inv_sm_size.y
		);
	#endif
	return output;
}

float4 mainPS(VSOutput input) : SV_Target {
	float2 uv0 = input.tc0.xy;
	float2 uv1 = input.tc1.xy;
	float2 uv2 = input.tc1.zw;
	float2 uv3 = input.tc2.xy;
	float2 uv4 = input.tc2.zw;
	float4 c0 = sampleBindless(LinearSamplerClamp, u_input, uv0);
	float4 c1 = sampleBindless(LinearSamplerClamp, u_input, uv1);
	float4 c2 = sampleBindless(LinearSamplerClamp, u_input, uv2);
	float4 c3 = sampleBindless(LinearSamplerClamp, u_input, uv3);
	float4 c4 = sampleBindless(LinearSamplerClamp, u_input, uv4);
	return c0 * 0.2270270270
	+ c1 * 0.3162162162
	+ c2 * 0.0702702703
	+ c3 * 0.3162162162
	+ c4 * 0.0702702703;
}