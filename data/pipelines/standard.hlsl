//@surface
#include "pipelines/common.hlsli"
#include "pipelines/surface_base.hlsli"

//@uniform "Material color", "color", {1, 1, 1, 1}
//@uniform "Roughness", "normalized_float", 1
//@uniform "Metallic", "normalized_float", 0
//@uniform "Emission", "float", 0
//@uniform "Translucency", "normalized_float", 0

//@texture_slot "Albedo", "textures/common/white.tga"
//@texture_slot "Normal", "textures/common/default_normal.tga"
//@texture_slot "Roughness", "textures/common/white.tga"
//@texture_slot "Metallic", "", "HAS_METALLICMAP"
//@texture_slot "Ambient occlusion", "", "HAS_AMBIENT_OCCLUSION_TEX"

Surface getSurface(VSOutput input) {
	Surface data;
	// TODO mip offset
	#ifdef UV0_ATTR
		float2 uv = input.uv;
	#else
		float2 uv = 0;
	#endif

	#ifdef AO_ATTR
		data.ao = input.ao;
	#else
		data.ao = 1;
	#endif

	float4 c = sampleBindless(LinearSampler, t_albedo, uv/*, -1*/) * u_material_color;
	data.albedo = c.rgb;
	data.alpha = c.a;
	#ifdef COLOR0_ATTR
		data.albedo.rgb *= input.color.rgb;
	#endif

	#ifdef HAS_AMBIENT_OCCLUSION_TEX
		data.ao *= sampleBindless(LinearSampler, t_ambient_occlusion, uv).r;
	#endif

	#ifdef FUR 
		data.alpha = saturate(data.alpha - input.fur_layer);
	#endif

	data.roughness = sampleBindless(LinearSampler, t_roughness, uv).g * u_roughness;
	#ifdef HAS_METALLICMAP
		data.metallic = sampleBindless(LinearSampler, t_metallic, uv).b * u_metallic;
	#else
		data.metallic = u_metallic;
	#endif
	data.N.xz = sampleBindless(LinearSampler, t_normal, uv).xy * 2 - 1;
	data.N.y = sqrt(saturate(1 - dot(data.N.xz, data.N.xz))); 

	#ifdef TANGENT_ATTR
		float3 tangent = normalize(input.tangent);
		float3 N = normalize(input.normal);

		float3x3 tbn = float3x3(
			tangent,
			N,
			normalize(cross(tangent, input.normal))
		);
		data.N = mul(data.N, tbn);
	#else
		data.N = normalize(input.normal);
	#endif
	data.emission = u_emission;
	data.translucency = u_translucency;
	data.shadow = 1;

	#ifndef ALPHA_CUTOUT
		float ndotv = abs(dot(data.N , data.V)) + 1e-5f;
		data.alpha = lerp(data.alpha, 1, pow(saturate(1 - ndotv), 5));
	#endif
	return data;
}