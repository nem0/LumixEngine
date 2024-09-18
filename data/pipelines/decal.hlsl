//@surface
//@include "pipelines/common.hlsli"
//@texture_slot "Texture", "textures/common/white.tga"
//@uniform "Material color", "color", {1, 1, 1, 1}

struct VSInput {
	float3 position : TEXCOORD0;
	float3 i_pos_ws : TEXCOORD1;
	float4 i_rot : TEXCOORD2;
	float3 i_half_extents : TEXCOORD3;
	float2 i_uv_scale : TEXCOORD4;
};

struct VSOutput {
	float3 half_extents : TEXCOORD0;
	float3 pos_ws : TEXCOORD1;
	float4 rot : TEXCOORD2;
	float2 uv_scale : TEXCOORD3;
	float4 position : SV_POSITION;
};

cbuffer DC : register(b4) {
	uint u_gbuffer_depth;
};

VSOutput mainVS(VSInput input) {
	VSOutput output;
	output.pos_ws = input.i_pos_ws;
	output.rot = input.i_rot;
	output.rot.w = -output.rot.w;
	output.half_extents = input.i_half_extents;
	float3 pos_ws = rotateByQuat(input.i_rot, input.position * input.i_half_extents);
	pos_ws += input.i_pos_ws;
	output.uv_scale = input.i_uv_scale;
	output.position = mul(float4(pos_ws, 1), mul(Global_view, Global_projection));
	return output;
}

GBufferOutput mainPS(VSOutput input) {
	float2 screen_uv = input.position.xy / Global_framebuffer_size;
	float3 pos_ws = getPositionWS(u_gbuffer_depth, screen_uv);
	float3 pos_ls = rotateByQuat(input.rot, pos_ws - input.pos_ws);

	bool is_in_decal_volume = any(abs(pos_ls) > input.half_extents);
	if (is_in_decal_volume) discard;

	float2 uv = (pos_ls.xz / input.half_extents.xz * 0.5 + 0.5) * input.uv_scale;	
	float4 color = sampleBindless(LinearSampler, t_texture, uv);
	//if (color.a < 0.01) discard;
	color.rgb *= u_material_color.rgb;

	GBufferOutput output;
	output.gbuffer0 = float4(color.rgb, color.a);
	output.gbuffer1 = float4(0, 0, 0, 0);
	output.gbuffer2 = float4(0, 0, 0, 0);
	output.gbuffer3 = float4(0, 0, 0, 0);
	return output;
}
