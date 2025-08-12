//@surface
//@uniform "Frames cols", "int", 1
//@uniform "Frames rows", "int", 1
//@texture_slot "Texture", "textures/common/white.tga"

#include "shaders/common.hlsli"

struct VSInput {
	float3 i_position : TEXCOORD0;
	float i_scale : TEXCOORD1;
	float4 i_color : TEXCOORD2;
	float i_rot : TEXCOORD3;
	float i_frame : TEXCOORD4;
	float i_emission : TEXCOORD5;
	uint vertex_id : SV_VertexID;
};

struct VSOutput {
	float2 uv : TEXCOORD0;
	float4 color : TEXCOORD1;
	float emission : TEXCOORD2;
	float4 position : SV_POSITION;
};

cbuffer Model : register(b4) {
	float4x4 u_model;
	uint u_material_index;
}

VSOutput mainVS(VSInput input) {
	MaterialData material = getMaterialData(u_material_index);
	float2 pos = float2(input.vertex_id & 1, (input.vertex_id & 2) * 0.5);
	uint frame = uint(input.i_frame);
	VSOutput output;
	output.uv = (pos + float2(frame % material.u_frames_cols, frame / material.u_frames_cols)) / float2(material.u_frames_cols, material.u_frames_rows);

	float3 dir = normalize(input.i_position);

	float c = cos(input.i_rot);
	float s = sin(input.i_rot);
	float2x2 rotm = float2x2(c, s, -s, c);
	pos = mul(rotm, pos * 2 - 1);
	pos *= input.i_scale;
	
	output.color = input.i_color;
	output.emission = input.i_emission;
	float4 pos_vs = transformPosition(input.i_position, u_model, Pass_ws_to_vs) + float4(pos.xy, 0, 0);
	output.position = transformPosition(pos_vs, Pass_vs_to_ndc);
	return output;
}

float4 mainPS(VSOutput input) : SV_TARGET {
	MaterialData material = getMaterialData(u_material_index);
	Surface data;
	float4 c = sampleBindless(LinearSampler, material.t_texture, input.uv) * saturate(input.color);
	data.N = 0;
	data.V = 0;
	data.pos_ws = 0;
	data.albedo = c.rgb;
	data.alpha = c.a;
	data.emission = input.emission;
	data.shadow = 1;
	data.ao = 1;
	data.roughness = 1;
	data.metallic = 0;
	data.translucency = 0;

	float linear_depth = dot(data.pos_ws.xyz, Pass_view_dir.xyz);
	Cluster cluster = getClusterLinearDepth(linear_depth, input.position.xy);
	float4 o_color;
	o_color.rgb = computeLighting(cluster, data, Global_light_dir.xyz, Global_light_color.rgb * Global_light_intensity, Global_shadowmap, Global_shadow_atlas, Global_reflection_probes, input.position.xy);

	#if defined ALPHA_CUTOUT
		if(data.alpha < 0.5) discard;
	#endif
	o_color.a = data.alpha;
	return o_color;
}	
