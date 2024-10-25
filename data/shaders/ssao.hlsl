#include "shaders/common.hlsli"

// possible future optimizations https://software.intel.com/content/www/us/en/develop/articles/adaptive-screen-space-ambient-occlusion.html
// inspired by https://github.com/tobspr/RenderPipeline/blob/master/rpplugins/ao/shader/ue4ao.kernel.glsl

cbuffer UB : register(b4) {
	float2 u_rcp_size;
	float u_radius;
	float u_intensity;
	uint u_downscale;
	TextureHandle u_normal_buffer;
	TextureHandle u_depth_buffer;
	TextureHandle u_history; // valid if temporal ssao is enabled
	TextureHandle u_motion_vectors;
	RWTextureHandle u_output;
};

#define PATTERN_SCALE 0.15
#define M 1.5
static const float2 PATTERN[4] = {
  float2( 0, PATTERN_SCALE ),
  float2( PATTERN_SCALE * M, 0 ),
  float2( 0, -PATTERN_SCALE * M * M ),
  float2( -PATTERN_SCALE * M * M * M, 0 )
};

// https://www.iquilezles.org/www/articles/texture/texture.htm
float4 getTexel(uint tex, float2 p, float2 rcp_size) {
	p = p / rcp_size + 0.5;

	float2 i = floor(p);
	float2 f = p - i;
	f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	p = i + f;

	p = (p - 0.5) * rcp_size;
	return sampleBindlessLod(LinearSamplerClamp, tex, p, 0);
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 screen_uv = (thread_id.xy + 0.5) * u_rcp_size;
	
	float ndc_depth;
	float3 pos_ws = getPositionWS(u_depth_buffer, screen_uv, ndc_depth);
	float3 normal_ws = bindless_textures[u_normal_buffer][thread_id.xy << u_downscale].xyz * 2 - 1;
	float occlusion = 0;
	float weight_sum = 0;

	float random_angle = 2 * M_PI * hash(float2(thread_id.xy) * 0.01 
		#ifdef TEMPORAL
			+ Global_random_float2_normalized
		#endif
	);
	float depth_scale = u_radius / length(pos_ws);

	float c = cos(random_angle);
	float s = sin(random_angle);
	float2x2 rot_scale = float2x2(c, s, -s, c);
	rot_scale *= saturate(depth_scale);

	for (int i = 0; i < 4; ++i) {
		float2 s = mul(PATTERN[i], rot_scale);

		float3 to_sample = getPositionWS(u_depth_buffer, screen_uv + s) - pos_ws;
		float3 to_sample_dir = normalize(to_sample);

		float dist_squared = dot(to_sample, to_sample);
		float cos_angle = dot(to_sample_dir, normal_ws);
		cos_angle = saturate(1.02 * cos_angle - 0.02);

		float w = saturate(0.03 / dist_squared);
		occlusion = occlusion + cos_angle * w;
		weight_sum += w;
	}

	occlusion /= max(1.0, weight_sum);
	float value = 1 - occlusion * u_intensity;

	#ifdef TEMPORAL
		float2 motionvec = sampleBindlessLod(LinearSamplerClamp, u_motion_vectors, screen_uv, 0).xy;
		#ifdef _ORIGIN_BOTTOM_LEFT
			motionvec *= 0.5;
		#else
			motionvec *= float2(0.5, -0.5);
		#endif
		float2 uv_prev = screen_uv + motionvec;

		if (all(uv_prev < 1) && all(uv_prev > 0)) {
			// TODO depth-based rejection
			float prev_frame_value = getTexel(u_history, uv_prev, u_rcp_size).r;
			value = lerp(prev_frame_value, value, 0.1);
		}
	#endif

	bindless_rw_textures[u_output][thread_id.xy] = value;
}

