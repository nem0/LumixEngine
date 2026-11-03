//@surface
//@texture_slot "Texture", "engine/textures/white.tga"
//@uniform "Material color", "color", {1, 1, 1, 1}

#include "engine/shaders/common.hlsli"
#include "engine/shaders/vertex_pulling.hlsli"

// vertices of the cube and instances are pulled, see vertex_pulling.hlsli
struct VSInput {
	uint vertex_id : SV_VertexID;
	uint instance_id : SV_InstanceID;
};

// 52 bytes
struct Instance {
	float3 pos_ws;
	float4 rot;
	float3 half_extents;
	float2 uv_scale;
	uint material_index;
};

Instance loadInstance(uint instance_id) {
	ByteAddressBuffer buffer = bindless_buffers[u_instance_buffer];
	const uint offset = pullInstanceAddress(instance_id, 52);
	Instance r;
	r.pos_ws = asfloat(buffer.Load3(offset));
	r.rot = asfloat(buffer.Load4(offset + 12));
	r.half_extents = asfloat(buffer.Load3(offset + 28));
	r.uv_scale = asfloat(buffer.Load2(offset + 40));
	r.material_index = buffer.Load(offset + 48);
	return r;
}

struct VSOutput {
	float3 half_extents : TEXCOORD0;
	float3 i_pos_ws : TEXCOORD1;
	float4 rot : TEXCOORD2;
	float2 uv_scale : TEXCOORD3;
	uint i_material_index : TEXCOORD4;
	float4 position : SV_POSITION;
};

cbuffer DC : register(b4) {
	TextureHandle u_gbuffer_depth;
};

VSOutput mainVS(VSInput input) {
	const Instance inst = loadInstance(input.instance_id);
	const float3 position = pullVertex(input.vertex_id).position;
	VSOutput output;
	output.i_pos_ws = inst.pos_ws;
	output.i_material_index = inst.material_index;
	output.rot = inst.rot;
	output.rot.w = -output.rot.w;
	output.half_extents = inst.half_extents;
	float3 pos_ws = rotateByQuat(inst.rot, position * inst.half_extents);
	pos_ws += inst.pos_ws;
	output.uv_scale = inst.uv_scale;
	output.position = transformPosition(pos_ws, Global_ws_to_ndc);
	return output;
}

GBufferOutput mainPS(VSOutput input) {
	MaterialData material = getMaterialData(input.i_material_index);
	float2 screen_uv = input.position.xy * Global_rcp_framebuffer_size;
	float3 pos_ws = getPositionWS(u_gbuffer_depth, screen_uv);
	float3 pos_ls = rotateByQuat(input.rot, pos_ws - input.i_pos_ws);

	bool is_in_decal_volume = any(abs(pos_ls) > input.half_extents);
	if (is_in_decal_volume) discard;

	float2 uv = (pos_ls.xz / input.half_extents.xz * 0.5 + 0.5) * input.uv_scale;	
	float4 color = sampleBindless(LinearSampler, material.t_texture, uv);
	//if (color.a < 0.01) discard;
	color.rgb *= material.u_material_color.rgb;

	GBufferOutput output;
	output.gbuffer0 = float4(color.rgb, color.a);
	output.gbuffer1 = float4(0, 0, 0, 0);
	output.gbuffer2 = float4(0, 0, 0, 0);
	output.gbuffer3 = float4(0, 0, 0, 0);
	return output;
}
