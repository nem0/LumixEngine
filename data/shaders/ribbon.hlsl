//@surface
////@texture_slot "Texture", "engine/textures/white.tga"

#include "engine/shaders/common.hlsli"

struct VSInput {
	uint4 i_data : TEXCOORD0;
	uint vertex_id : SV_VertexID;
};

struct VSOutput {
	float4 color : TEXCOORD1;
	float emission : TEXCOORD2;
	float4 position : SV_POSITION;
};

cbuffer Model : register(b4) {
	float4x4 u_model;
	uint u_material_index;
}

struct PointData {
	float3 position;
	float scale;
	float4 color;
	float emission;
};

PointData getPointData(uint buffer_index, uint buffer_offset, uint point_idx) {
	uint offset = buffer_offset + point_idx * 36;
	float4 d0 = asfloat(bindless_buffers[buffer_index].Load4(offset));
	float4 d1 = asfloat(bindless_buffers[buffer_index].Load4(offset + 16));
	float d2 = asfloat(bindless_buffers[buffer_index].Load(offset + 32));
	PointData pd;
	pd.position = d0.xyz;
	pd.scale = d0.w;
	pd.color = d1;
	pd.emission = d2;
	return pd;
}

VSOutput mainVS(VSInput input) {
	//MaterialData material = getMaterialData(u_material_index);
	float2 pos = float2((input.vertex_id & 1) * 2.0 - 1.0, 0);
	uint buffer_idx = input.i_data.x;
	uint buffer_offset = input.i_data.y;
	uint ribbon_offset = input.i_data.z;
	uint ribbon_max_length = input.i_data.w;
	uint point_index = (ribbon_offset + (input.vertex_id >> 1)) % ribbon_max_length;
	PointData pd = getPointData(buffer_idx, buffer_offset, point_index);
	
	VSOutput output;

	pos *= pd.scale;
	
	output.color = pd.color;
	output.emission = pd.emission;
	float4 pos_vs = transformPosition(pd.position, u_model, Pass_ws_to_vs) + float4(pos, 0, 0);
	output.position = transformPosition(pos_vs, Pass_vs_to_ndc);
	return output;
}

float4 mainPS(VSOutput input) : SV_TARGET {
	//MaterialData material = getMaterialData(u_material_index);
	Surface data;
	data.N = 0;
	data.V = 0;
	data.pos_ws = 0;
	data.albedo = input.color.rgb;
	data.alpha = input.color.a;
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
