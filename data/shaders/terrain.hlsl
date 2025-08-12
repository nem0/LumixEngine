//@surface
#include "shaders/common.hlsli"
//@texture_slot "Heightmap", "textures/common/white.tga"
//@texture_slot "Detail albedo", "textures/common/white.tga"
//@texture_slot "Detail normal", "textures/common/white.tga"
//@texture_slot "Splatmap", "textures/common/white.tga"
//@texture_slot "Satellite", "textures/common/white.tga"
//@texture_slot "Noise", "textures/common/blue_noise.tga"

//@uniform "Roughness", "normalized_float", 1
//@uniform "Metallic", "normalized_float", 0
//@uniform "Emission", "float", 0
//@uniform "Detail distance", "float", 50
//@uniform "Detail scale", "float", 1
//@uniform "Noise UV scale", "float", 1
//@uniform "Detail diffusion", "float", 1
//@uniform "Detail power", "float", 1

cbuffer Drawcall : register(b4) {
	int4 u_from_to;
	int4 u_from_to_sup;
	float4 u_position;
	float4 u_rel_camera_pos;
	float4 u_terrain_scale;
	float2 u_hm_size;
	float u_cell_size;
	uint u_material_index;
};

struct VSOutput {
	#ifndef DEPTH
		float2 uv : TEXCOORD0;
		float dist2 : TEXCOORD1;
		float3 pos_ws : TEXCOORD2;
	#endif
	float4 position : SV_POSITION;
};


VSOutput mainVS(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
	MaterialData material = getMaterialData(u_material_index);
	int2 ij = u_from_to.xy + int2((vertex_id >> 1), instance_id + (vertex_id & 1));

	float3 v = 0;
	v.xz = ij * u_cell_size;
	int mask = ~1;
	float3 npos = 0;
	npos.xz = (ij & mask) * u_cell_size;

	float2 size = float2(u_from_to_sup.zw - u_from_to_sup.xy);
	float2 rel = (ij - u_from_to_sup.xy) / size;
	
	rel = saturate(abs(rel - 0.5) * 10 - 4);
	v.xz = lerp(v.xz, npos.xz, rel.yx);
	v.xz = clamp(v.xz, 0, u_hm_size);

	float2 hm_uv = (v.xz + 0.5 * u_terrain_scale.x) / (u_hm_size + u_terrain_scale.x);
	VSOutput output;
	float h = sampleBindlessLod(LinearSamplerClamp, material.t_heightmap, hm_uv, 0).x * u_terrain_scale.y;

	float3 pos_ws = u_position.xyz + v + float3(0, h, 0);		
	#ifndef DEPTH
		output.uv = v.xz / u_hm_size;
		output.pos_ws = pos_ws;
	#endif
	#ifndef DEPTH
		output.dist2 = dot(pos_ws.xyz, pos_ws.xyz);
	#endif
	output.position = transformPosition(pos_ws, Pass_ws_to_ndc);
	return output;
}

#ifndef DEPTH

	float rgbSum(float4 v) { return dot(v, float4(1, 1, 1, 0)); }

	float3x3 getTBN(float2 uv, MaterialData material) {
		float hscale = u_terrain_scale.y / u_terrain_scale.x;
		float s01 = sampleBindlessLodOffset(LinearSamplerClamp, material.t_heightmap, uv, 0, int2(-1, 0)).x;
		float s21 = sampleBindlessLodOffset(LinearSamplerClamp, material.t_heightmap, uv, 0, int2(1, 0)).x;
		float s10 = sampleBindlessLodOffset(LinearSamplerClamp, material.t_heightmap, uv, 0, int2(0, -1)).x;
		float s12 = sampleBindlessLodOffset(LinearSamplerClamp, material.t_heightmap, uv, 0, int2(0, 1)).x;
		float3 va = normalize(float3(1.0, (s21-s01) * hscale, 0.0));
		float3 vb = normalize(float3(0.0, (s12-s10) * hscale, 1.0));
		float3 N = normalize(cross(vb,va));
		float3 T = normalize(cross(N, vb));
		return float3x3(
			T,
			N,
			normalize(cross(T, N))
		);
	}

	struct Detail {
		float4 albedo;
		float3 normal;
	};

	Detail select(bool cond, Detail a, Detail b) {
		if (cond) return a;
		return b;
	}

	Detail textureNoTile(float dist2, float2 x, int layer, float2 dPdx, float2 dPdy, MaterialData material) {
		Detail detail;

		detail.normal.xy = bindless_2D_arrays[material.t_detail_normal].SampleGrad(LinearSampler, float3(x, layer), dPdx, dPdy).xy * 2 - 1;
		detail.albedo = bindless_2D_arrays[material.t_detail_albedo].SampleGrad(LinearSampler, float3(x, layer), dPdx, dPdy);

		const float blend_start_sqr = 10 * 10;
		if (dist2 > blend_start_sqr) {
			float2 N;
			float3 uv = float3(x * 0.1, layer);
			dPdx *= 0.1; 
			dPdy *= 0.1; 
			float4 albedo = bindless_2D_arrays[material.t_detail_albedo].SampleGrad(LinearSampler, uv, dPdx, dPdy);
			N = bindless_2D_arrays[material.t_detail_normal].SampleGrad(LinearSampler, uv, dPdx, dPdy).xy * 2 - 1;
			float t = saturate((dist2 - blend_start_sqr) / 10000);
			detail.normal.xy = lerp(detail.normal.xy, N, t);
			detail.albedo = lerp(detail.albedo, albedo, t);
		}
		detail.normal.z = sqrt(saturate(1 - dot(detail.normal.xy, detail.normal.xy))); 

		return detail;
	}

	float2 power(float2 v, float2 a) {
		float2 t = pow(v, a);
		return t / (t + pow(1.0 - v, a));
	}

	Surface getSurface(VSOutput input) {
		MaterialData material = getMaterialData(u_material_index);
		Surface surface;
		if(input.dist2 < material.u_detail_distance * material.u_detail_distance) {
			float2 uv_norm = input.uv; // [0 - 1]

			float2 grid_size = u_hm_size / u_terrain_scale.xz;
			float2 resolution = grid_size + 1;

			float2 r = float2(sampleBindless(LinearSampler, material.t_noise, uv_norm * material.u_noise_uv_scale * grid_size).x,
							sampleBindless(LinearSampler, material.t_noise, uv_norm.yx * material.u_noise_uv_scale * grid_size).x);
			r = r * material.u_detail_diffusion * 2 - material.u_detail_diffusion;
			uv_norm += r / u_hm_size;

			float2 uv = uv_norm * grid_size;
			float2 uv_ratio = power(frac(uv), material.u_detail_power.xx);
			float2 uv_opposite = 1.0 - uv_ratio;

			float4 bicoef = float4(
				uv_opposite.x * uv_opposite.y,
				uv_opposite.x * uv_ratio.y,
				uv_ratio.x * uv_opposite.y,
				uv_ratio.x * uv_ratio.y
			);

			float2 uv_grid = uv / resolution;
			// todo textureGather
			float4 splat00 = bindless_textures[material.t_splatmap].SampleLevel(LinearSampler, uv_grid, 0, int2(0, 0));
			float4 splat10 = bindless_textures[material.t_splatmap].SampleLevel(LinearSampler, uv_grid, 0, int2(1, 0));
			float4 splat01 = bindless_textures[material.t_splatmap].SampleLevel(LinearSampler, uv_grid, 0, int2(0, 1));
			float4 splat11 = bindless_textures[material.t_splatmap].SampleLevel(LinearSampler, uv_grid, 0, int2(1, 1));

			float2 uv_detail = material.u_detail_scale * input.uv * u_hm_size;

			int4 indices = int4(float4(splat00.x, splat01.x, splat10.x, splat11.x) * 255.0 + 0.5);

			float2 dPdx = ddx(uv_detail);
			float2 dPdy = ddy(uv_detail);
			Detail c00 = textureNoTile(input.dist2, uv_detail, indices.x, dPdx, dPdy, material);
			Detail c01 = select(indices.x == indices.y, c00, textureNoTile(input.dist2, uv_detail, indices.y, dPdx, dPdy, material));
			Detail c10 = select(indices.x == indices.z, c00, 
				select(indices.y == indices.z, c01, textureNoTile(input.dist2, uv_detail, indices.z, dPdx, dPdy, material)));
			Detail c11 = select(indices.x == indices.w, c00, 
				select(indices.y == indices.w, c01,
					select(indices.z == indices.w, c10, textureNoTile(input.dist2, uv_detail, indices.w, dPdx, dPdy, material))));
		
			surface.albedo = (c00.albedo * bicoef.x + c01.albedo * bicoef.y + c10.albedo * bicoef.z + c11.albedo * bicoef.w).rgb;
			float3 n = (c00.normal * bicoef.x + c01.normal * bicoef.y + c10.normal * bicoef.z + c11.normal * bicoef.w).xzy;

			surface.N = normalize(mul(n, getTBN(input.uv, material)));
			surface.alpha = 1;

			// blend between detail and satellite
			float sat_blend_start = material.u_detail_distance * 0.05;
			float sat_blend_start_sqr = sat_blend_start * sat_blend_start;
			if (input.dist2 > sat_blend_start_sqr) {
				float m = (input.dist2 - sat_blend_start_sqr) / (material.u_detail_distance * material.u_detail_distance);
				m = saturate((m - 0.8) * 5);
				float3 sat_color = sampleBindless(LinearSamplerClamp, material.t_satellite, input.uv).rgb;
				sat_color *= saturate(0.5 + sampleBindless(LinearSampler, material.t_noise, input.uv * 10).r 
					* sampleBindless(LinearSampler, material.t_noise, input.uv * 29).r * 0.5);
				surface.albedo = lerp(surface.albedo, sat_color, m);
			}
		}
		else {
			surface.N = getTBN(input.uv, material)[1];
			surface.albedo = sampleBindless(LinearSamplerClamp, material.t_satellite, input.uv).rgb;
			surface.albedo *= saturate(0.5 + sampleBindless(LinearSampler, material.t_noise, input.uv * 10).r * sampleBindless(LinearSampler, material.t_noise, input.uv * 29).r * 0.5);
			surface.alpha = 1;
		}

		#ifndef DEPTH
			surface.motion = computeStaticObjectMotionVector(input.pos_ws);
		#endif

		surface.pos_ws = 0;
		surface.roughness = material.u_roughness;
		surface.metallic  = material.u_metallic;
		surface.emission = material.u_emission;
		surface.translucency = 0;
		surface.ao = 0.9;
		surface.shadow = saturate(Global_light_dir.y * 3) * 0.9;
		return surface;
	}
#endif

#if defined DEFERRED
	GBufferOutput mainPS(VSOutput input) {
		return packSurface(getSurface(input));
	}
#elif defined DEPTH
	void mainPS() {}
#endif
