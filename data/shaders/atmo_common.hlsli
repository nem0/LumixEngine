// Shared atmosphere and fog helpers used by atmo_sky.hlsl and lighting.hlsl.

struct AtmoParams {
	float bot;
	float top;
	float distribution_rayleigh;
	float distribution_mie;
	float4 scatter_rayleigh;
	float4 scatter_mie;
	float4 absorb_mie;
	float4 sunlight;
	float4 resolution;
	float4 fog_scattering;
	float fog_top;
	float fog_enabled;
	float godrays_enabled;
	float clouds_enabled;
	float clouds_top;
	float clouds_bottom;
	RWTextureHandle output;
	TextureHandle optical_depth;
	TextureHandle depth_buffer;
	TextureHandle inscatter;
};

// mie - Schlick approximation phase function of Henyey-Greenstein
float miePhase(float g, float cos_theta) {
	float k = 1.55 * g - 0.55 * g * g * g;
	float tmp = 1 + k * cos_theta;
	return (1 - k * k) / (4 * M_PI * tmp * tmp);
}

float rayleighPhase(float cos_theta) {
	return 3 / (16.0 * M_PI) * (1 + cos_theta * cos_theta);
}

float phase(float alpha, float g) {
	float a = 3.0 * (1.0 - g * g);
	float b = 2.0 * (2.0 + g * g);
	float c = 1.0 + alpha * alpha;
	float x = abs(1.0 + g * g - 2.0 * g * alpha);
	float d = x * sqrt(x);
	return (a / b) * (c / d);
}

// Evaluates volumetric fog along [Global_camera_world_pos, Global_camera_world_pos + eyedir * dist].
// Returns transmittance in `out_transmittance` and adds fog inscatter into `inout_atmo`.
void applyAtmoFog(
	AtmoParams u,
	float3 eyedir,
	float dist,
	float cos_theta,
	float2 pixel_pos,
	float3 sunlight,
	bool allow_godrays,
	inout float3 inout_atmo,
	out float3 out_transmittance)
{
	out_transmittance = 1;
	float3 p0 = Global_camera_world_pos.xyz;
	float3 p1 = Global_camera_world_pos.xyz + eyedir * dist;
	makeAscending(p0, p1);

	const bool is_in_fog = p0.y < u.fog_top;
	if (!is_in_fog) return;

	float seg_len = dist;
	if (p1.y > u.fog_top) {
		float dy = p1.y - p0.y;
		float clip_t = (u.fog_top - p0.y) / (abs(dy) < 1e-5 ? 1e-5 : dy);
		p1 = lerp(p0, p1, clip_t);
		seg_len = dist * clip_t;
	}

	out_transmittance = exp(-seg_len * u.fog_scattering.rgb);

	// p0 and p1 are clipped to y <= u.fog_top (with p0.y <= p1.y),
	// so both camera and sun optical depth vary linearly along the segment.
	float rcp_light_y = Global_light_dir.y > 1e-5 ? 1.0 / Global_light_dir.y : 0.0;
	float od_sun_0 = rcp_light_y > 0.0 ? min(1e5, (u.fog_top - p0.y) * rcp_light_y) : 1e5;
	float od_sun_1 = rcp_light_y > 0.0 ? min(1e5, (u.fog_top - p1.y) * rcp_light_y) : 1e5;
	float od0 = od_sun_0 + (eyedir.y >= 0 ? 0.0 : seg_len);
	float od1 = od_sun_1 + (eyedir.y >= 0 ? seg_len : 0.0);

	float3 inscatter = 0;
	if (allow_godrays && u.godrays_enabled > 0) {
		// March in the direction of increasing optical depth so trans starts at its
		// maximum value and decays monotonically (trans_step <= 1), avoiding underflow.
		float3 start_p = p0;
		float3 end_p = p1;
		if (od1 < od0) {
			float tmp_od = od0; od0 = od1; od1 = tmp_od;
			start_p = p1; end_p = p0;
		}
		const int STEP_COUNT = 32;
		const float inv_steps = 1.0 / (STEP_COUNT + 1);
		float step_len = seg_len * inv_steps;
		float offset = hash(pixel_pos * 0.05);
		float f0 = offset / STEP_COUNT;
		float3 p = lerp(start_p, end_p, f0);
		float3 p_step = (end_p - start_p) * inv_steps;
		float od_step = (od1 - od0) * inv_steps;
		float3 trans = exp(-(od0 + (od1 - od0) * f0) * u.fog_scattering.rgb);
		float3 trans_step = exp(-od_step * u.fog_scattering.rgb);
		for (int i = 0; i < STEP_COUNT + 1; ++i) {
			if (f0 + i * inv_steps >= 1.0) break;
			float shadow = getShadowSimple(Global_shadowmap, p - Global_camera_world_pos.xyz);
			inscatter += trans * shadow;
			p += p_step;
			trans *= trans_step;
		}
		inscatter *= step_len;
	}
	else {
		// Constant fog density on [p0, p1] with linear optical depth: exact closed-form integral.
		float3 a = od0 * u.fog_scattering.rgb;
		float3 b = od1 * u.fog_scattering.rgb;
		float3 d = b - a;
		float3 exp_a = exp(-a);
		float3 exp_b = exp(-b);
		float3 safe_d = select(abs(d) > 1e-5, d, float3(1, 1, 1));
		float3 integral = select(abs(d) > 1e-5, (exp_a - exp_b) / safe_d, exp_a);
		inscatter = seg_len * integral;
	}

	inout_atmo += inscatter * u.fog_scattering.rgb * sunlight * miePhase(0.25, -cos_theta);
}
