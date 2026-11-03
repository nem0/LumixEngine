//@surface
#include "engine/shaders/common.hlsli"

cbuffer DC : register(b4) {
	float4x4 u_matrix;
	TextureHandle u_texture;
	uint u_vertices;		// bindless
	uint u_vertices_offset;	// in bytes
};

struct VSInput {
	uint vertex_id : SV_VertexID;
};

struct VSOutput {
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float4 position : SV_POSITION;
};

VSOutput mainVS(VSInput input) {
	// vertices are pulled: float2 position, float2 uv, RGBA8 color, 20 bytes each, see Draw2D::Vertex
	ByteAddressBuffer vb = bindless_buffers[u_vertices];
	const uint offset = u_vertices_offset + input.vertex_id * 20;
	const float4 pos_uv = asfloat(vb.Load4(offset));
	const uint packed = vb.Load(offset + 16);

	VSOutput output;
	output.color = float4(packed & 0xff, (packed >> 8) & 0xff, (packed >> 16) & 0xff, packed >> 24) / 255.0;
	output.uv = pos_uv.zw;
	output.position = transformPosition(float3(pos_uv.xy, 0), u_matrix);
	return output;
}

float4 mainPS(VSOutput input) :SV_TARGET {
	return input.color * sampleBindlessLod(LinearSampler, u_texture, input.uv, 0);
}