//@surface
#include "shaders/common.hlsli"

struct VSOutput {
	float2 uv : TEXCOORD0;
	float4 position : SV_POSITION;
};

cbuffer Drawcall : register(b4) {
	float u_intensity;
	TextureHandle u_sky;
};

// TODO use this
float getFogFactorSky(float cam_height, float3 view_vector, float fog_density, float fog_bottom, float fog_height) {
	if (view_vector.y == 0) return 1.0;
	float to_top = max(0, (fog_bottom + fog_height) - cam_height);

	float avg_y = (fog_bottom + fog_height + cam_height) * 0.5;
	float avg_density = fog_density * saturate(1 - (avg_y - fog_bottom) / fog_height);
	float res = exp(-pow(avg_density * to_top / view_vector.y, 2));
	res =  1 - saturate(res - (1-min(0.2, view_vector.y)*5));
	return res;
}

VSOutput mainVS(uint vertex_id : SV_VertexID) {
	VSOutput output;
	output.position = fullscreenQuad(vertex_id, output.uv);
	return output;
}

float4 mainPS(VSOutput input) : SV_TARGET {
	float3 view_vector = getViewDirection(input.uv);
	return float4(sampleCubeBindless(LinearSampler, u_sky, view_vector).rgb * u_intensity, 1);
}
