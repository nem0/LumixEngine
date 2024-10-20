//@surface
//@texture_slot "Texture", "textures/common/white.tga"
//@uniform "Material color", "color", {1, 1, 1, 1}

#include "shaders/common.hlsli"

struct VSOutput {
	float3 half_extents : TEXCOORD0;
	float3 pos : TEXCOORD1;
	float4 rot : TEXCOORD2;
	float2 uv_scale : TEXCOORD3;
	float4 bezier : TEXCOORD4;
	float4 position : SV_POSITION;
};

struct VSInput {
	float3 position : TEXCOORD0;
	float3 i_pos : TEXCOORD1;
	float4 i_rot : TEXCOORD2;
	float3 i_half_extents : TEXCOORD3;
	float2 i_uv_scale : TEXCOORD4;
	float4 i_bezier : TEXCOORD5;
};

VSOutput mainVS(VSInput input) {
	VSOutput output;
	output.pos = input.i_pos;
	output.rot = input.i_rot;
	output.half_extents = input.i_half_extents;
	float3 pos_ws = rotateByQuat(input.i_rot, input.position * input.i_half_extents);
	pos_ws += input.i_pos;
	output.uv_scale = input.i_uv_scale;
	output.bezier = input.i_bezier;
	output.position = transformPosition(pos_ws, Global_ws_to_ndc);
	return output;
}

float cross2(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }

// from shadertoy by iq
float2 sdBezier(float2 pos, float2 A, float2 B, float2 C) {    
	float2 a = B - A;
	float2 b = A - 2.0*B + C;
	float2 c = a * 2.0;
	float2 d = A - pos;

	float kk = 1.0 / dot(b, b);
	float kx = kk * dot(a, b);
	float ky = kk * (2.0 * dot(a, a) + dot(d, b)) / 3.0;
	float kz = kk * dot(d, a);

	float res = 0.0;
	float sgn = 0.0;

	float p = ky - kx * kx;
	float p3 = p * p * p;
	float q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
	float h = q * q + 4.0 * p3;
	float res_t;

	if (h >= 0.0) { // 1 root
		h = sqrt(h);
		float2 x = (float2(h, -h) - q) / 2.0;
		float2 uv = sign(x) * pow(abs(x), 1.0 / 3.0);
		float t = saturate(uv.x + uv.y - kx);
		float2 q = d + (c + b * t) * t;
		res = dot(q, q);
		sgn = cross2(c + 2.0 * b * t, q);
		res_t = t;
	}
	else { // 3 roots
		float z = sqrt(-p);
		float v = acos(q / (p * z * 2.0)) / 3.0;
		float m = cos(v);
		float n = sin(v) * 1.732050808;
		float3 t = saturate(float3(m + m, -n - m, n - m) * z - kx);
		float2 qx = d + (c + b * t.x) * t.x;
		float dx = dot(qx, qx), sx = cross2(c + 2.0 * b * t.x, qx);
		float2 qy = d + (c + b * t.y) * t.y;
		float dy = dot(qy, qy), sy = cross2(c + 2.0 * b * t.y, qy);
		if (dx < dy) {
			res = dx;
			sgn = sx;
			res_t = t.x;
		} else {
			res = dy;
			sgn = sy;
			res_t = t.y;
		}
	}

	return float2(sqrt(res) * sign(sgn), res_t);
}

cbuffer Dc : register(b4) {
	TextureHandle u_gbuffer_depth;
};

GBufferOutput mainPS(VSOutput input) {
	float2 screen_uv = input.position.xy * Global_rcp_framebuffer_size;
	float3 pos_ws = getPositionWS(u_gbuffer_depth, screen_uv);
	
	float4 r = input.rot;
	r.w = -r.w;
	float3 lpos = rotateByQuat(r, pos_ws - input.pos);
	if (any(abs(lpos) > input.half_extents)) discard;
	
	float2 bezier_dist = sdBezier(lpos.xz, input.bezier.xy, 0, input.bezier.zw);
	if (abs(bezier_dist.x) > 0.5 * input.uv_scale.x) discard;
	if (abs(bezier_dist.y - 0.5) > 0.499) discard;
	bezier_dist.x += 0.5 * input.uv_scale.x;
	bezier_dist.x /= input.uv_scale.x;
	bezier_dist.y *= input.uv_scale.y;
	float4 color = sampleBindless(LinearSampler, t_texture, bezier_dist.yx);
	if (color.a < 0.5) discard;
	color.rgb *= u_material_color.rgb;

	GBufferOutput o;
	o.gbuffer0 = float4(color.rgb, 0.9);
	o.gbuffer1 = float4(0, 0, 0, 0);
	o.gbuffer2 = float4(0, 0, 0, 0);
	o.gbuffer3 = float4(0, 0, 0, 0);
	return o;
}