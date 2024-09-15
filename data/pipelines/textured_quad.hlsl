//@surface
//@include "pipelines/common.hlsli"

cbuffer Drawcall : register(b4) {
	float4 u_offset_scale;
	float4 u_r_mask;
	float4 u_g_mask;
	float4 u_b_mask;
	float4 u_a_mask;
	float4 u_offsets;
	uint u_texture;
};

struct Output {
	float2 uv : TEXCOORD0;
	float4 position : SV_POSITION;
};

Output mainVS(uint vertex_id : SV_VertexID) {
	Output output;
	float4 pos = fullscreenQuad(vertex_id, output.uv);
	pos.xy = pos.xy * u_offset_scale.zw + u_offset_scale.xy;
	output.position = pos;
	return output;
}

struct Input {
	float2 uv : TEXCOORD0;
};

// TODO is this pixel perfect?
float4 mainPS(Input input) : SV_TARGET {
	float2 uv = input.uv;
	float4 t = sampleBindlessLod(LinearSamplerClamp, u_texture, uv, 0);
	return float4(
		dot(t, u_r_mask) + u_offsets.r,
		dot(t, u_g_mask) + u_offsets.g,
		dot(t, u_b_mask) + u_offsets.b,
		dot(t, u_a_mask) + u_offsets.a
	);
}
