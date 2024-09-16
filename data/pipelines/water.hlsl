//@surface
//@include "pipelines/common.hlsli"
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
	#endif
};

struct VSOutput {
	float2 uv : TEXCOORD0;
	float3 normal : TEXCOORD1;
	float3 tangent : TEXCOORD2;
	float4 wpos : TEXCOORD3;
	#ifdef _HAS_ATTR2 
		//float2 masks : TEXCOORD4;
	#endif
	float4 position : SV_POSITION;
};

#ifndef AUTOINSTANCED
	cbuffer Model : register(b4) {
		float4x4 u_model;
	};
#endif

VSOutput mainVS(VSInput input) {
	static const float3 normal = float3(0, 1, 0);
	static const float3 tangent = float3(1, 0, 0);

	VSOutput output;
	output.uv = input.uv;
	#if defined AUTOINSTANCED
		output.normal = rotateByQuat(input.i_rot, normal);
		output.tangent = rotateByQuat(input.i_rot, tangent);
		float3 p = input.position * input.i_scale.xyz;
		output.wpos = float4(input.i_pos_lod.xyz + rotateByQuat(input.i_rot, p), 1);
	#elif defined GRASS
		#error TODO
	#else 
		float4x4 model_mtx = u_model;
		output.normal = float3x3(model_mtx) * normal;
		output.tangent = float3x3(model_mtx) * tangent;
		output.wpos = mul(float4(input.position,  1), model_mtx);
	#endif
	
	#ifdef _HAS_ATTR2 
		//output.masks = a_masks;
	#endif
	output.position = mul(output.wpos, Pass_view_projection);
	return output;
}

cbuffer Textures : register(b5) {
	uint u_shadowmap;
	uint u_depthbuffer;
	uint u_reflection_probes;
	uint u_bg;
};

float2 raycast(float3 csOrig, float3 csDir, float stride, float jitter) {
	float3 csEndPoint = csOrig + abs(csOrig.z * 0.1) * csDir;

	float4 H0 = mul(float4(csOrig, 1), Global_projection);
	float4 H1 = mul(float4(csEndPoint, 1), Global_projection);

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

	float stepDir = sign(delta.x);
	float invdx = stepDir / delta.x;

	float dk = ((k1 - k0) * invdx) * stride;
	float2  dP = (float2(stepDir, delta.y * invdx)) * stride;

	float2 P = P0;
	float k = k0;
	
	uint max_steps = 64 >> 2;
	for (uint j = 0; j < 4; ++j) {
		P += dP * jitter / stride;
		k += dk * jitter / stride;
		for (uint i = 0; i < max_steps; ++i) {
			float rayZFar = 1 / k;

			float2 p = permute ? P.yx : P;
			if (any(p < 0.0f.xx)) break;
			if (any(p > float2(Global_framebuffer_size))) break;

			float depth = sampleBindless(LinearSamplerClamp, u_depthbuffer, p / Global_framebuffer_size).x;
			depth = toLinearDepth(Global_inv_projection, depth);
			
			float dif = rayZFar - depth;
			if (dif > 1e-3) {
				return p;
			}

			P += dP;
			k += dk;
		}
		dP *= 2;
		dk *= 2;
	}
	return -1.0f.xx;
}

float3 getReflectionColor(float3 view, float3 normal, float dist, float3 wpos)
{
	#ifdef SSR
		float4 o = mul(float4(wpos, 1), Global_view);
		float3 d = mul(reflect(-view, normal), float3x3(Global_view));
		float2 hit = raycast(o.xyz, d, 4, hash(wpos.xy + Global_time));
		if (hit.x >= 0) {
			return sampleBindless(LinearSamplerClamp, u_bg, hit.xy / Global_framebuffer_size.xy).rgb;
		}
	#endif

	float3 reflection = reflect(-view, normal);
	float4 radiance_rgbm = sampleCubeArrayBindlessLod(LinearSampler, u_reflection_probes, float4(reflection, 0), min(3, dist * 0.1));
	return radiance_rgbm.rgb * radiance_rgbm.a * 4;
}

float getWaterDepth(float3 wpos, float3 view, float3 normal)
{
	float4 screen_pos = mul(float4(wpos, 1), Global_view_projection);
	screen_pos /= screen_pos.w;
	float depth = sampleBindless(LinearSamplerClamp, u_depthbuffer, toScreenUV(screen_pos.xy * 0.5 + 0.5)).x;
	return toLinearDepth(Global_inv_projection, depth) - toLinearDepth(Global_inv_projection, screen_pos.z);
}

float4 getRefraction(float3 wpos)
{
	float4 screen_pos = mul(float4(wpos, 1), Global_view_projection);
	screen_pos /= screen_pos.w;
	return sampleBindless(LinearSamplerClamp, u_bg, toScreenUV(screen_pos.xy * 0.5 + 0.5));
}

float getHeight(float2 uv) {
	return 
			(cos(sampleBindless(LinearSampler, t_normal, uv).x * 2 * M_PI + Global_time * 5) 
			+ sin(sampleBindless(LinearSampler, t_normal, -uv.yx * 2 + 0.1).x * M_PI - Global_time * 3)) * 0.5 + 0.5
;
}

// TODO try less texture fetches
float3 getSurfaceNormal(float2 uv, float normal_strength, out float h00)
{
	float2 d = float2(0.01, 0);
	uv *= u_uv_scale;
	uv += u_flow_dir * Global_time;
	// TODO optimize
	h00 = getHeight(uv - d.xy) * normal_strength;
	float h10 = getHeight(uv + d.xy) * normal_strength;
	float h01 = getHeight(uv - d.yx) * normal_strength;
	float h11 = getHeight(uv + d.yx) * normal_strength;

	float3 N;
	N.x = h00 - h10;
	N.z = h00 - h10;
	N.y = sqrt(saturate(1 - dot(N.xz, N.xz))); 
	return N;
}

float4 mainPS(VSOutput input) : SV_TARGET
{
	float3 V = normalize(-input.wpos.xyz);
	float3 L = Global_light_dir.xyz;
	
	float3x3 tbn = float3x3(
		normalize(input.tangent),
		normalize(input.normal),
		normalize(cross(input.normal, input.tangent))
	);

	float normal_strength = u_normal_strength;
	#ifdef _HAS_ATTR2 
		//normal_strength *= 1 - v_masks.g;
	#endif

	float h;
	float3 wnormal = getSurfaceNormal(input.uv, normal_strength, h);
	wnormal = normalize(mul(wnormal, tbn));

	//float shadow = getShadow(u_shadowmap, input.wpos.xyz, wnormal);

	float dist = length(input.wpos.xyz);
	float3 view = normalize(-input.wpos.xyz);
	float3 refl_color = getReflectionColor(view, wnormal, dist * normal_strength, input.wpos.xyz) * u_reflection_multiplier;
	float water_depth = getWaterDepth(input.wpos.xyz, view, wnormal)- saturate(h * 0.4);
	

	float3 halfvec = normalize(view + Global_light_dir.xyz);
	float spec_strength = pow(saturate(dot(halfvec, wnormal)), u_specular_power);
	
	float3 R = reflect(-normalize(view), normalize(wnormal));
	spec_strength = pow(saturate(dot(R, normalize(Global_light_dir.xyz))), u_specular_power);

	float3 spec_color = Global_light_color.rgb * spec_strength * u_specular_multiplier;
	float fresnel = u_r0 + (1.0 - u_r0) * pow(saturate(1.0 - dot(normalize(view), wnormal)), 5);

	float3 water_color = pow(u_water_color.rgb, 2.2f.xxx); // TODO do not do this in shader
	float3 transmittance = saturate(exp(-water_depth * u_water_scattering * (1.0f.xxx - water_color)));

	float t = saturate(water_depth * 5); // no hard edge

	float refraction_distortion = u_refraction_distortion;

	float3 refraction = getRefraction(input.wpos.xyz + float3(wnormal.xz, 0) * u_refraction_distortion * t).rgb;

	refraction *= lerp(1.0f.xxx, u_ground_tint.rgb, t);
	refraction *= transmittance;

	float3 reflection = refl_color + spec_color;

	float4 o_color;
	o_color.rgb = lerp(refraction, reflection, fresnel);
	o_color.rgb = lerp(refraction, o_color.rgb, t);
	
	float noise = sampleBindless(LinearSampler, t_noise, 1 - input.uv * 0.1 + u_flow_dir * Global_time * 0.3).x;
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
