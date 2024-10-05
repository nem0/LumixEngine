#include "shaders/common.hlsli"

cbuffer Drawcall : register(b4) {
	float u_distance;
	float u_range;
	float u_max_blur_size;
	float u_sharp_range;
	TextureHandle u_texture;
	TextureHandle u_depth;
	RWTextureHandle u_output;
};

static const float GOLDEN_ANGLE = 2.39996323;
static const float RAD_SCALE = 0.5; 

float getBlurSize(float depth, float focus_point) {
	float d = depth - focus_point;
	d = abs(d) < u_sharp_range ? 0 : d - u_sharp_range;
	float coc = clamp(d / u_range, -1.0, 1.0);
	return abs(coc) * u_max_blur_size;
}

// https://blog.voxagon.se/2018/05/04/bokeh-depth-of-field-in-single-pass.html
float3 depthOfField(float2 tex_coord, float focus_point) {
	float ndc_depth = sampleBindlessLod(LinearSamplerClamp, u_depth, tex_coord, 0).r;
	float center_depth = toLinearDepth(ndc_depth);
	float center_size = getBlurSize(center_depth, focus_point);
	float3 color = sampleBindlessLod(LinearSamplerClamp, u_texture, tex_coord, 0).rgb;
	float tot = 1.0;
	float radius = RAD_SCALE;
	for (float ang = 0.0; radius < u_max_blur_size; ang += GOLDEN_ANGLE) {
		float2 tc = tex_coord + float2(cos(ang), sin(ang)) * Global_rcp_framebuffer_size * radius;
		float3 sample_color = sampleBindlessLod(LinearSamplerClamp, u_texture, tc, 0).rgb;
		float sample_depth = toLinearDepth(sampleBindlessLod(LinearSamplerClamp, u_depth, tc, 0).r);
		float sample_size = getBlurSize(sample_depth, focus_point);
		if (sample_depth > center_depth)
			sample_size = clamp(sample_size, 0.0, center_size * 2.0);
		float m = smoothstep(radius - 0.5, radius + 0.5, sample_size);
		color += lerp(color / tot, sample_color, m);
		tot += 1.0;
		radius += RAD_SCALE / radius;
	}
	return color /= tot;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 uv = thread_id.xy * Global_rcp_framebuffer_size;
	float3 dof = depthOfField(uv, u_distance);
	bindless_rw_textures[u_output][thread_id.xy] = float4(dof, 1.0);
}
