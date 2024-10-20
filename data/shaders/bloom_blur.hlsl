//@surface
#include "shaders/common.hlsli"

cbuffer Drawcall : register(b4) {
	float4 u_rcp_size;
	TextureHandle u_big;
	TextureHandle u_small;
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
			uv.x + 1.3846153846 * u_rcp_size.x, uv.y, 
			uv.x + 3.2307692308 * u_rcp_size.x, uv.y
		);
		output.tc2 = float4(
			uv.x - 1.3846153846 * u_rcp_size.x, uv.y, 
			uv.x - 3.2307692308 * u_rcp_size.x, uv.y
		);
	#else
		output.tc1 = float4(
			uv.x, uv.y + 1.3846153846 * u_rcp_size.y,
			uv.x, uv.y + 3.2307692308 * u_rcp_size.y
		);
		output.tc2 = float4(
			uv.x, uv.y - 1.3846153846 * u_rcp_size.y, 
			uv.x, uv.y - 3.2307692308 * u_rcp_size.y
		);
	#endif
	return output;
}

// blur bigger in one axis and merge with smaller (already blurred from previous step)
float4 mainPS(VSOutput input) : SV_Target {
	return sampleBindless(LinearSamplerClamp, u_small, input.tc0.xy) * 0.5
		+	((sampleBindless(LinearSamplerClamp, u_big, input.tc0.xy)) * 0.2270270270
			+ (sampleBindless(LinearSamplerClamp, u_big, input.tc1.xy)) * 0.3162162162
			+ (sampleBindless(LinearSamplerClamp, u_big, input.tc1.zw)) * 0.0702702703
			+ (sampleBindless(LinearSamplerClamp, u_big, input.tc2.xy)) * 0.3162162162
			+ (sampleBindless(LinearSamplerClamp, u_big, input.tc2.zw)) * 0.0702702703) * 0.5;
}