//@surface
//@uniform "Frames cols", "int", 1
//@uniform "Frames rows", "int", 1
//@texture_slot "Texture", "engine/textures/white.tga"
//@define "WORLD_SPACE"

#include "engine/shaders/common.hlsli"

struct VSInput {
	uint vertex_id : SV_VertexID;
	uint instance_id : SV_InstanceID;
};

// a particle, outputs of the emitter in this order, it is pulled by the vertex shader
struct Particle {
	float3 position;
	float scale;
	float4 color;
	float rot;
	float frame;
	float emission;
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
	uint u_particles;			// bindless
	uint u_particles_offset;	// in bytes
	uint u_particles_stride;	// in bytes
}

Particle loadParticle(uint instance_id) {
	ByteAddressBuffer buffer = bindless_buffers[u_particles];
	const uint offset = u_particles_offset + instance_id * u_particles_stride;
	const float4 d0 = asfloat(buffer.Load4(offset));
	const float4 d1 = asfloat(buffer.Load4(offset + 16));
	const float3 d2 = asfloat(buffer.Load3(offset + 32));
	Particle p;
	p.position = d0.xyz;
	p.scale = d0.w;
	p.color = d1;
	p.rot = d2.x;
	p.frame = d2.y;
	p.emission = d2.z;
	return p;
}

VSOutput mainVS(VSInput input) {
	const Particle particle = loadParticle(input.instance_id);
	MaterialData material = getMaterialData(u_material_index);
	float2 pos = float2(input.vertex_id & 1, (input.vertex_id & 2) * 0.5);
	uint frame = uint(particle.frame);
	VSOutput output;
	output.uv = (pos + float2(frame % material.u_frames_cols, frame / material.u_frames_cols)) / float2(material.u_frames_cols, material.u_frames_rows);

	float c = cos(particle.rot);
	float s = sin(particle.rot);
	float2x2 rotm = float2x2(c, s, -s, c);
	pos = mul(rotm, pos * 2 - 1);
	pos *= particle.scale;
	
	output.color = particle.color;
	output.emission = particle.emission;
	#ifdef WORLD_SPACE
		float4 pos_vs = transformPosition(particle.position - Global_camera_world_pos.xyz, Pass_ws_to_vs);
	#else
		float4 pos_vs = transformPosition(particle.position, u_model, Pass_ws_to_vs);
	#endif
	pos_vs +=  + float4(pos.xy, 0, 0);
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
