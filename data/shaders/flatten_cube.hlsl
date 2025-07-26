#include "shaders/common.hlsli"

cbuffer Drawcall : register(b4) {
	uint u_x;
	uint u_y;
	uint u_w;
	uint u_h;
	uint u_face;
	TextureHandle u_src;
	RWTextureHandle u_dst;
};

uint dirToFace(float3 dir, out float2 face_uv) {
	float3 abs_dir = abs(dir);
	if (abs_dir.x > abs_dir.y && abs_dir.x > abs_dir.z) {
		face_uv = float2(dir.x > 0 ? -dir.z : dir.z, -dir.y) / abs_dir.x;
		face_uv = face_uv * 0.5 + 0.5;
		return dir.x > 0 ? 0 : 1;
	}
	if (abs_dir.y > abs_dir.z) {
		face_uv = float2(dir.x, dir.y > 0 ? dir.z : -dir.z) / abs_dir.y;
		face_uv = face_uv * 0.5 + 0.5;
		return dir.y > 0 ? 2 : 3;
	}
	face_uv = float2(dir.z > 0 ? dir.x : -dir.x, -dir.y) / abs_dir.z;
	face_uv = face_uv * 0.5 + 0.5;
	return dir.z > 0 ? 4 : 5;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	if (thread_id.x >= u_w || thread_id.y >= u_h) return;
	
	// TODO half pixel off?
	float2 uv = float2(thread_id.xy + 0.5) / float2(u_w, u_h);
	float3 dir = fromOctahedral(uv);
	dir.z *= -1;

	float2 face_uv;
	uint face = dirToFace(dir, face_uv);

	// TODO all faces at once 
	if (face != u_face) return;

	float4 value = bindless_textures[u_src].SampleLevel(LinearSamplerClamp, face_uv, 0);

	bindless_rw_textures[u_dst][thread_id.xy + uint2(u_x, u_y)] = value;
}