//@surface
#include "shaders/common.hlsli"

struct VSOutput {
	float2 uv : TEXCOORD0;
	float4 position : SV_POSITION;
};

VSOutput mainVS(uint vertex_id : SV_VertexID) {
	VSOutput output;
	output.uv = float2(vertex_id & 1, vertex_id >> 1) * 1000 - 500;
	float3 pos_ls = float3(output.uv.x, 0, output.uv.y);
	float3 p = pos_ls - Global_camera_world_pos.xyz;
	output.position = transformPosition(p, Global_ws_to_vs, Global_vs_to_ndc_no_jitter);
	return output;
}

// https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
// https://gist.github.com/bgolus/d49651f52b1dcf82f70421ba922ed064
// grid by Ben Golus
float PristineGrid(float2 uv, float2 lineWidth) {
	float4 uvDDXY = float4(ddx(uv), ddy(uv));
	float2 uvDeriv = float2(length(uvDDXY.xz), length(uvDDXY.yw));
	bool2 invertLine = lineWidth < 0.5;
	float2 targetWidth = lerp(1.0 - lineWidth, lineWidth, invertLine);
	float2 drawWidth = clamp(targetWidth, uvDeriv, 0.5);
	float2 lineAA = uvDeriv * 1.5;
	float2 gridUV = abs(frac(uv) * 2.0 - 1.0);
	gridUV = lerp(gridUV, 1.0 - gridUV, invertLine);
	float2 grid2 = smoothstep(drawWidth + lineAA, drawWidth - lineAA, gridUV);
	grid2 *= saturate(targetWidth / drawWidth);
	grid2 = lerp(grid2, targetWidth, saturate(uvDeriv * 2.0 - 1.0));
	grid2 = lerp(1.0 - grid2, grid2, invertLine);
	return lerp(grid2.x, 1.0, grid2.y);
}

float4 mainPS(float2 uv : TEXCOORD0) : SV_TARGET {
	float grid = PristineGrid(uv, 0.02);
	if (grid < 0.0) discard;
	return float4(0, 0, 0, grid);
}