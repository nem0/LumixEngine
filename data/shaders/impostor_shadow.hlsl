#include "shaders/common.hlsli"

cbuffer Data : register(b4){
	float4x4 u_projection;
	float4x4 u_proj_to_model;
	float4x4 u_inv_view;
	float4 u_center;
	int2 u_tile;
	int2 u_tile_size;
	int u_size;
	float u_radius;
	TextureHandle u_depth;
	TextureHandle u_normalmap;
	RWTextureHandle u_output;
};

float3 impostorToWorld(float2 uv) {
	uv = uv * 2 - 1;
	float3 position = float3(
		uv.x + uv.y,
		0,
		uv.x - uv.y
	) * 0.5;

	position.y = -(1.0 - abs(position.x) - abs(position.z));
	return position;
}

float2 worldToImpostor(float3 vec) {
	vec.y = min(vec.y, 0);
	vec = normalize(vec);
	vec.xz /= dot(1.0, abs(vec));
	return float2(vec.x + vec.z, vec.x - vec.z) * 0.5 + 0.5;
}

float4x4 lookAt(float3 eye, float3 at, float3 up) {
	float4x4 res = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	float3 f = normalize(eye - at);
	float3 r = normalize(cross(up, f));
	float3 u = normalize(cross(f, r));
	
	res[0].xyz = r;
	res[1].xyz = u;
	res[2].xyz = f;
	res = transpose(res);
	
	res[3].xyz = float3(-dot(r, eye), -dot(u, eye), -dot(f, eye));
	return res;
}

float getShadow(float2 tile_uv, float depth, float3 v_shadow) {
	v_shadow = normalize(v_shadow);
	if (depth == 0) return 1;

	int2 shadow_tile = int2(0.5 + worldToImpostor(v_shadow) * (u_size - 1));
	v_shadow = normalize(impostorToWorld(shadow_tile / float(u_size - 1)));

	float4x4 view = lookAt(u_center.xyz - v_shadow * u_radius, u_center.xyz, float3(0, 1, 0));

	float4x4 to_sm = mul(u_proj_to_model, mul(view, u_projection));

	float4 p = mul(float4(tile_uv * 2 - 1, depth, 1), to_sm);
	p.xyz /= p.w;
	p.xy = p.xy * 0.5 + 0.5;
	float2 uv = toScreenUV((p.xy + shadow_tile.xy) / float(u_size));
	float shadow_depth = sampleBindlessLod(LinearSamplerClamp, u_depth, uv, 0).r;
	return shadow_depth * 0.99 < p.z ? 1 : 0;
}

float softShadow(float2 tile_uv, float3 N, float depth, float3 mid, float3 delta) {
	float NdL = saturate(0.1 + 
		0.5 * saturate(dot(N, -mid)) 
		+ 0.25 * saturate(dot(N, -mid - delta)) 
		+ 0.25 * saturate(dot(N, -mid + delta))
	);
	NdL *= NdL;
	float s = 0.5 * getShadow(tile_uv, depth, mid)
		+ 0.25 * getShadow(tile_uv, depth, mid + delta)
		+ 0.25 * getShadow(tile_uv, depth, mid - delta);
	#ifdef BAKE_NORMALS
		return saturate(min(s, NdL));
	#else
		return saturate(s);
	#endif
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	if (any(thread_id.xy > u_tile_size)) return;

	float2 tile_uv = thread_id.xy / float2(u_tile_size);
	float2 uv = (u_tile + tile_uv) / float(u_size);

	float depth = sampleBindlessLod(LinearSamplerClamp, u_depth, toScreenUV(uv), 0).r;
	float3 N;
	N.xy = sampleBindlessLod(LinearSamplerClamp, u_normalmap, uv, 0).xy * 2 - 1;
	N.z = sqrt(1 - dot(N.xy, N.xy));
	N = mul(N, (float3x3)u_inv_view);

	float zenith = -1;
	float4 res = float4(
		softShadow(tile_uv, N, depth, float3(1, zenith, 0), float3(0, 0, 0.5)),
		softShadow(tile_uv, N, depth, float3(0, zenith, -1), float3(0.5, 0, 0)),
		softShadow(tile_uv, N, depth, float3(-1, zenith, 0), float3(0, 0, 0.5)),
		softShadow(tile_uv, N, depth, float3(0, zenith, 1), float3(0.5, 0, 0))
	);

	int2 uv_out = int2(thread_id.xy + u_tile * u_tile_size);
	#ifndef ORIGIN_BOTTOM_LEFT
		uv_out.y = u_tile_size.y * u_size - uv_out.y - 1;
	#endif

	bindless_rw_textures[u_output][uv_out] = res;
}
