//@surface

struct VSInput {
	float2 pos : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float4 color : TEXCOORD2;
};

cbuffer ImGuiState : register(b4) {
	float2 c_scale;
	float2 c_offset;
	uint c_texture;
	float c_time;
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
	float2 p = input.pos * c_scale + c_offset;
	output.position = float4(p.xy, 0, 1);
	return output;
}

float4 mainPS(VSOutput input) : SV_Target {		
	// Converted from ShaderToy, original author Xor
	// https://www.shadertoy.com/view/Wcc3z2
	// https://www.shadertoy.com/terms
	// License: Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.
	// tweaked for better performance
	
	float4 o = 0;
	float z = 0;
	float2 uv = float2(2 * input.color.x, 2 - 2 * input.color.y);
	float3 dir = normalize(float3(uv, 0) - float3(1.0, 1.0, 1.0));
	float t = c_time * 2;

	for(float i = 0; i < 20.0; ++i) {
		float3 p = z * dir;
		
		p.y += 1.0;
		float r = max(-p.y, 0.0);
		p.y += r + r;
		
		[unroll]
		for (float j = 1.0; j < 5.0; j += j) {
			p.y += cos(p.x * j + t * cos(j) + z) / j;
		}
		
		float d = p.z + 7.0;
		d = (0.1 * r + abs(p.y - 1.0) / (1.0 + r + r + r * r) + max(d, -d * 0.1)) / 8.0;
		z += d * 2;

		o += (cos(z * 0.5 + t + float4(0, 2, 4, 3)) + 1.3) / d;
	}

	// o gets too high that we get black pixels without this clamp
	o = clamp(o, 0, 9000);
	
	return tanh(o / 700.0);
}