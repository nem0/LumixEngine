//@surface
#include "engine/shaders/common.hlsli"
#include "engine/shaders/atmo_common.hlsli"

struct VSOutput {
	float2 uv : TEXCOORD0;
	float4 position : SV_POSITION;
};

cbuffer Textures : register(b4) {
	TextureHandle u_gbuffer0;
	TextureHandle u_gbuffer1;
	TextureHandle u_gbuffer2;
	TextureHandle u_gbuffer3;
	TextureHandle u_gbuffer_depth;
	TextureHandle u_shadowmap;
	TextureHandle u_shadow_atlas;
	TextureCubeArrayHandle u_reflection_probes;
	AtmoParams u_atmo;
};

VSOutput mainVS(uint vertexID : SV_VertexID) {
	VSOutput output;
	output.position = fullscreenQuad(vertexID, output.uv);
	return output;
}

float4 mainPS(VSOutput input) : SV_Target {
	float ndc_depth;
	Surface surface = unpackSurface(input.uv, u_gbuffer0, u_gbuffer1, u_gbuffer2, u_gbuffer3, u_gbuffer_depth, ndc_depth);
	Cluster cluster = getCluster(ndc_depth, input.position.xy);
	
	float4 res;
	res.rgb = computeLighting(cluster
		, surface
		, Global_light_dir.xyz
		, Global_light_color.rgb * Global_light_intensity
		, u_shadowmap
		, u_shadow_atlas
		, u_reflection_probes
		, input.position.xy);

	#if defined(ATMO) || defined(ATMO_FOG) || defined(ATMO_FULL)
	{
		float3 eyedir = -surface.V;
		float cos_theta = dot(eyedir, Global_light_dir.xyz);
		float3 sunlight = u_atmo.sunlight.rgb * u_atmo.sunlight.a;
		float linear_depth = toLinearDepth(ndc_depth);
		float2 v = float2(
			saturate(linear_depth / 50e3),
			max(0, eyedir.y)
		);
		float4 insc = sampleBindlessLod(LinearSamplerClamp, u_atmo.inscatter, v, 0);
		float3 atmo_rgb =
			insc.aaa * miePhase(0.75, -cos_theta) * sunlight * u_atmo.scatter_mie.rgb
			+ insc.rgb * rayleighPhase(-cos_theta) * sunlight * u_atmo.scatter_rayleigh.rgb;

		#if defined(ATMO_FOG) || defined(ATMO_FULL)
		if (u_atmo.fog_enabled > 0) {
			float dist = dot(surface.pos_ws, eyedir);
			float3 fog_trans;
			#ifdef ATMO_FULL
			const bool allow_godrays = true;
			#else
			const bool allow_godrays = false;
			#endif
			applyAtmoFog(u_atmo, eyedir, dist, cos_theta, input.position.xy, sunlight, allow_godrays, atmo_rgb, fog_trans);
			res.rgb *= fog_trans;
		}
		#endif
		res.rgb += atmo_rgb;
	}
	#endif

	res.a = 1;
	return res;
}
