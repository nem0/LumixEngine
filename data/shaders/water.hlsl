//@surface
//@texture_slot "Normal", "textures/common/default_normal.tga"
//@texture_slot "Noise", "textures/common/white.tga"
//@texture_slot "Foam", "", "HAS_FOAM"
//@texture_slot "Clutter", "", "HAS_CLUTTER"

//@uniform "UV scale", "float", 1
//@uniform "Specular power", "float", 512
//@uniform "R0", "float", 0.04
//@uniform "Normal strength", "float", 0.29
//@uniform "Reflection multiplier", "float", 2
//@uniform "Specular multiplier", "float", 8
//@uniform "Flow dir", "float2", {0.06, 0.04}
//@uniform "Water color", "color", {0, 0.866, 0.854, 0}
//@uniform "Ground tint", "color", {0, 0.64, 0.507, 0}
//@uniform "Water scattering", "float", 8
//@uniform "Refraction distortion", "float", 0.4
//@define "SSR"

#include "shaders/common.hlsli"

#define ATTR(X) TEXCOORD##X

struct VSInput {
	float3 position : TEXCOORD0;
	#ifdef UV0_ATTR
		float2 uv : ATTR(UV0_ATTR);
	#endif
	#ifdef _HAS_ATTR2 
		//layout(location = 2) in float2 a_masks;
	#endif
	#if defined AUTOINSTANCED
		float4 i_rot : ATTR(INSTANCE0_ATTR);
		float4 i_pos_lod : ATTR(INSTANCE1_ATTR);
		float4 i_scale : ATTR(INSTANCE2_ATTR);
		uint i_material_index : ATTR(INSTANCE3_ATTR);
	#endif
};

struct VSOutput {
	float2 uv : TEXCOORD0;
	float3 normal : TEXCOORD1;
	float3 tangent : TEXCOORD2;
	float3 pos_ws : TEXCOORD3;
	uint i_material_index : TEXCOORD4;
	#ifdef _HAS_ATTR2 
		//float2 masks : TEXCOORD4;
	#endif
	float4 position : SV_POSITION;
};

#ifndef AUTOINSTANCED
	cbuffer Model : register(b4) {
		float4x4 u_ls_to_ws;
	};
#endif

VSOutput mainVS(VSInput input) {
	static const float3 normal = float3(0, 1, 0);
	static const float3 tangent = float3(1, 0, 0);

	VSOutput output;
	output.uv = input.uv;
	output.i_material_index = input.i_material_index;
	#if defined AUTOINSTANCED
		output.normal = rotateByQuat(input.i_rot, normal);
		output.tangent = rotateByQuat(input.i_rot, tangent);
		float3 p = input.position * input.i_scale.xyz;
		output.pos_ws = input.i_pos_lod.xyz + rotateByQuat(input.i_rot, p);
	#elif defined GRASS
		#error TODO
	#else
		output.normal = float3x3(u_ls_to_ws) * normal;
		output.tangent = float3x3(u_ls_to_ws) * tangent;
		output.pos_ws = transformPosition(input.position, u_ls_to_ws).xyz;
	#endif
	
	#ifdef _HAS_ATTR2 
		//output.masks = a_masks;
	#endif
	output.position = transformPosition(output.pos_ws, Pass_ws_to_ndc);
	return output;
}

cbuffer Textures : register(b5) {
	TextureHandle u_shadowmap;
	TextureHandle u_depthbuffer;
	TextureHandle u_reflection_probes;
	TextureHandle u_bg;
};

float2 raycast(float3 ray_origin, float3 ray_dir, float stride, float jitter) {
	float3 ray_end_point = ray_origin + abs(ray_origin.z * 0.1) * ray_dir;

	float4 H0 = transformPosition(ray_origin, Global_vs_to_ndc);
	float4 H1 = transformPosition(ray_end_point, Global_vs_to_ndc);

	float k0 = 1 / H0.w, k1 = 1 / H1.w;

	float2 P0 = toScreenUV(H0.xy * k0 * 0.5 + 0.5) * Global_framebuffer_size;
	float2 P1 = toScreenUV(H1.xy * k1 * 0.5 + 0.5) * Global_framebuffer_size;

	float2 delta = P1 - P0;
	bool permute = abs(delta.x) < abs(delta.y);
	if (permute) {
		P0 = P0.yx;
		P1 = P1.yx;
		delta = delta.yx;
	}

	float step_dir = sign(delta.x);
	float invdx = step_dir / delta.x;

	float dk = ((k1 - k0) * invdx) * stride;
	float2  dP = (float2(step_dir, delta.y * invdx)) * stride;

	float2 P = P0;
	float k = k0;
	
	uint max_steps = 64 >> 2;
	for (uint j = 0; j < 4; ++j) {
		P += dP * jitter / stride;
		k += dk * jitter / stride;
		for (uint i = 0; i < max_steps; ++i) {
			float ray_z_far = 1 / k;

			float2 p = permute ? P.yx : P;
			if (any(p < 0.0)) break;
			if (any(p > float2(Global_framebuffer_size))) break;

			float depth_ndc = sampleBindless(LinearSamplerClamp, u_depthbuffer, p * Global_rcp_framebuffer_size).x;
			float depth_linear = toLinearDepth(depth_ndc);
			
			float dif = ray_z_far - depth_linear;
			if (dif > 1e-3) {
				return p;
			}

			P += dP;
			k += dk;
		}
		dP *= 2;
		dk *= 2;
	}
	return -1.0;
}

float3 getReflectionColor(float3 view, float3 normal, float dist, float3 pos_ws)
{
	#ifdef SSR
		float4 pos_vs = transformPosition(pos_ws, Global_ws_to_vs);
		float3 ray_dir = mul(reflect(-view, normal), (float3x3)Global_ws_to_vs);
		float jitter = hash(pos_ws.xy + Global_random_float2_normalized);
		float2 hit = raycast(pos_vs.xyz, ray_dir, 4, jitter);
		if (hit.x >= 0) {
			return sampleBindless(LinearSamplerClamp, u_bg, hit.xy * Global_rcp_framebuffer_size).rgb;
		}
	#endif

	float3 reflection = reflect(-view, normal);
	float4 radiance_rgbm = sampleCubeArrayBindlessLod(LinearSampler, u_reflection_probes, float4(reflection, 0), min(3, dist * 0.1));
	return radiance_rgbm.rgb * radiance_rgbm.a * 4;
}

float getWaterDepth(float3 pos_ws, float3 view, float3 normal)
{
	float4 screen_pos = transformPosition(pos_ws, Global_ws_to_ndc);
	screen_pos /= screen_pos.w;
	screen_pos.xy = toScreenUV(screen_pos.xy * 0.5 + 0.5);
	float depth = sampleBindless(LinearSamplerClamp, u_depthbuffer, screen_pos.xy).x;
	return toLinearDepth(depth) - toLinearDepth(screen_pos.z);
}

float4 getRefraction(float3 pos_ws)
{
	float4 screen_pos = transformPosition(pos_ws, Global_ws_to_ndc);
	screen_pos /= screen_pos.w;
	screen_pos.xy = toScreenUV(screen_pos.xy * 0.5 + 0.5);
	return sampleBindless(LinearSamplerClamp, u_bg, screen_pos.xy);
}

float getHeight(float2 uv, MaterialData material) {
	return 
		(cos(sampleBindless(LinearSampler, material.t_normal, uv).x * 2 * M_PI + Global_time * 5) 
		+ sin(sampleBindless(LinearSampler, material.t_normal, -uv.yx * 2 + 0.1).x * M_PI - Global_time * 3)) * 0.5 + 0.5
;
}

// TODO try less texture fetches
float3 getSurfaceNormal(MaterialData material, float2 uv, float normal_strength, out float h00)
{
	float2 d = float2(0.01, 0);
	uv *= material.u_uv_scale;
	uv += material.u_flow_dir * Global_time;
	// TODO optimize
	h00 = getHeight(uv - d.xy, material) * normal_strength;
	float h10 = getHeight(uv + d.xy, material) * normal_strength;
	float h01 = getHeight(uv - d.yx, material) * normal_strength;
	float h11 = getHeight(uv + d.yx, material) * normal_strength;

	float3 N;
	N.x = h00 - h10;
	N.z = h00 - h10;
	N.y = sqrt(saturate(1 - dot(N.xz, N.xz))); 
	return N;
}

float4 mainPS(VSOutput input) : SV_TARGET
{
	MaterialData material = getMaterialData(input.i_material_index);
	float3 V = normalize(-input.pos_ws);
	float3 L = Global_light_dir.xyz;
	
	float3x3 tbn = float3x3(
		normalize(input.tangent),
		normalize(input.normal),
		normalize(cross(input.normal, input.tangent))
	);

	float normal_strength = material.u_normal_strength;
	#ifdef _HAS_ATTR2 
		//normal_strength *= 1 - v_masks.g;
	#endif

	float h;
	float3 wnormal = getSurfaceNormal(material, input.uv, normal_strength, h);
	wnormal = normalize(mul(wnormal, tbn));

	//float shadow = getShadow(u_shadowmap, input.pos_ws, wnormal);

	float dist = length(input.pos_ws);
	float3 view = -normalize(input.pos_ws);
	float3 refl_color = getReflectionColor(view, wnormal, dist * normal_strength, input.pos_ws) * material.u_reflection_multiplier;
	float water_depth = getWaterDepth(input.pos_ws, view, wnormal)- saturate(h * 0.4);
	

	float3 halfvec = normalize(view + Global_light_dir.xyz);
	float spec_strength = pow(saturate(dot(halfvec, wnormal)), material.u_specular_power);
	
	float3 R = reflect(-normalize(view), normalize(wnormal));
	spec_strength = pow(saturate(dot(R, normalize(Global_light_dir.xyz))), material.u_specular_power);

	float3 spec_color = Global_light_color.rgb * spec_strength * material.u_specular_multiplier;
	float fresnel = material.u_r0 + (1.0 - material.u_r0) * pow(saturate(1.0 - dot(normalize(view), wnormal)), 5);

	float3 water_color = pow(material.u_water_color.rgb, 2.2); // TODO do not do this in shader
	float3 transmittance = saturate(exp(-water_depth * material.u_water_scattering * (1.0f - water_color)));

	float t = saturate(water_depth * 5); // no hard edge

	float3 refraction = getRefraction(input.pos_ws + float3(wnormal.xz, 0) * material.u_refraction_distortion * t).rgb;

	refraction *= lerp(1.0, material.u_ground_tint.rgb, t);
	refraction *= transmittance;

	float3 reflection = refl_color + spec_color;

	float4 o_color;
	o_color.rgb = lerp(refraction, reflection, fresnel);
	o_color.rgb = lerp(refraction, o_color.rgb, t);
	
	float noise = sampleBindless(LinearSampler, material.t_noise, 1 - input.uv * 0.1 + material.u_flow_dir * Global_time * 0.3).x;
	/*#if defined HAS_FOAM && defined _HAS_ATTR2
		float4 foam = sampleBindless(LinearSampler, t_foam, input.uv + u_flow_dir * Global_time * 3);
		o_color.rgb = lerp(o_color.rgb, foam.rgb, saturate(v_masks.r - noise) * t);
	#endif

	#if defined HAS_CLUTTER && defined _HAS_ATTR2
		float4 clutter = sampleBindless(LinearSampler, t_clutter, input.uv + wnormal.xz * 0.3 + float2(noise * 0.03, 0));
		o_color.rgb = lerp(o_color.rgb, clutter.rgb, saturate(clutter.a * v_masks.g * t));
	#endif*/
	
	o_color.a = 1;
	return o_color;
}
