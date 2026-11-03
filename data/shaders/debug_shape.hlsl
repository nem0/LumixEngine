//@surface
#include "engine/shaders/common.hlsli"

cbuffer Model : register(b4) {
	float4x4 u_ls_to_ws;
	uint u_vertices;		// bindless
	uint u_vertices_offset;	// in bytes
};

struct VSOutput {
	float4 color : TEXCOORD0;
	float4 position : SV_POSITION;
};

struct VSInput {
	uint vertex_id : SV_VertexID;
};

VSOutput mainVS(VSInput input) {
	// vertices are pulled: float3 position, RGBA8 color, 16 bytes each, see WorldView::Vertex
	ByteAddressBuffer vb = bindless_buffers[u_vertices];
	const uint offset = u_vertices_offset + input.vertex_id * 16;
	const float3 position = asfloat(vb.Load3(offset));
	const uint packed = vb.Load(offset + 12);
	const float4 color = float4(packed & 0xff, (packed >> 8) & 0xff, (packed >> 16) & 0xff, packed >> 24) / 255.0;

	VSOutput output;
	float3 color_srgb = pow(abs(color.rgb), 2.2);
	output.color = float4(color_srgb, color.a);
	output.position = transformPosition(position, u_ls_to_ws, Pass_ws_to_ndc);
	return output;
}

float4 mainPS(VSOutput input) : SV_TARGET {
	return input.color;
}
