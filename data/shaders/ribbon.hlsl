//@surface
//@define "WORLD_SPACE"
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
	uint buffer_idx = input.i_data.x;
	uint buffer_offset = input.i_data.y;
	uint ribbon_offset = input.i_data.z;
	uint ribbon_max_length = input.i_data.w;
	uint pidx = ribbon_offset + (input.vertex_id >> 1);
	uint prev_pidx = pidx > 0 ? pidx - 1 : 0;
	PointData prev_pd = getPointData(buffer_idx, buffer_offset, prev_pidx % ribbon_max_length);
	PointData pd = getPointData(buffer_idx, buffer_offset, pidx % ribbon_max_length);
	
	float3 dir;
	if (pidx == ribbon_max_length - 1) {
		dir = pd.position - prev_pd.position;
	}
	else {
		PointData next_pd = getPointData(buffer_idx, buffer_offset, (pidx + 1) % ribbon_max_length);
		dir = next_pd.position - prev_pd.position;
	}
	
	VSOutput output;
	if (dot(dir, dir) < 0.01) {
		dir = float3(0, 0, 1);
	}
	else {
		dir = normalize(dir);
	}
	dir = mul(float4(dir, 0), u_model);
	
	float3 right = normalize(cross(Global_view_dir.xyz, dir));
	float3 offset = right * pd.scale * (input.vertex_id & 1 ? 1 : -1);

	output.color = pd.color;
	output.emission = pd.emission;
	#ifdef WORLD_SPACE
		float4 pos_vs = transformPosition(pd.position - Global_camera_world_pos.xyz + offset, Pass_ws_to_vs);
	#else
		float4 pos_vs = transformPosition(transformPosition(pd.position, u_model) + offset, Pass_ws_to_vs);
	#endif
	output.position = transformPosition(pos_vs, Pass_vs_to_ndc);
	return output;
}

float4 mainPS(VSOutput input) : SV_TARGET {
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

// TODO DEFERRED
	float linear_depth = dot(data.pos_ws.xyz, Pass_view_dir.xyz);
	Cluster cluster = getClusterLinearDepth(linear_depth, input.position.xy);
	float4 o_color;
	o_color.rgb = computeLighting(cluster, data, Global_light_dir.xyz, Global_light_color.rgb * Global_light_intensity, Global_shadowmap, Global_shadow_atlas, Global_reflection_probes, input.position.xy);
	o_color.a = data.alpha;
	return o_color;
}	
