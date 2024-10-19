#include "shaders/common.hlsli"

// possible future optimizations https://software.intel.com/content/www/us/en/develop/articles/adaptive-screen-space-ambient-occlusion.html
// inspired by https://github.com/tobspr/RenderPipeline/blob/master/rpplugins/ao/shader/ue4ao.kernel.glsl

cbuffer UB : register(b4) {
	float u_radius;
	float u_intensity;
	TextureHandle u_normal_buffer;
	TextureHandle u_depth_buffer;
	RWTextureHandle u_output;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 screen_uv = (thread_id.xy + 0.5) * Global_rcp_framebuffer_size;
	float3 pos_ws = getPositionWS(u_depth_buffer, screen_uv);
	float3 normal_ws = bindless_textures[u_normal_buffer][thread_id.xy].xyz * 2 - 1;
	float occlusion = 0;
	float occlusion_count = 0;

	float c = hash(float2(thread_id.xy) * 0.01 + Global_random_float2_normalized) * 2 - 1;
	float depth_scale = u_radius / length(pos_ws) * (c * 2 + 0.1)/**/;
	float s = sqrt(1 - c * c); 
	float2x2 rot_scale = float2x2(c, s, -s, c);
	rot_scale *= depth_scale;

	for (int i = 0; i < 4; ++i) {
		float2 poisson = POISSON_DISK_4[i];
		float2 s = mul(poisson, rot_scale);
		
	if (1) {
			float3 pos_a_ws = getPositionWS(u_depth_buffer, screen_uv + s) - pos_ws;
			float3 sample_vec_a = normalize(pos_a_ws);
			float dist_a = length(pos_a_ws);
			float angle_a = saturate(dot(sample_vec_a, normal_ws));

			float w = saturate(0.07 / dist_a);
			occlusion = occlusion + angle_a * w;
			occlusion_count += w;
	}	
	else {
		float3 pos_a_ws = getPositionWS(u_depth_buffer, screen_uv + s) - pos_ws;
		float3 pos_b_ws = getPositionWS(u_depth_buffer, screen_uv - s) - pos_ws;

		float3 sample_vec_a = normalize(pos_a_ws);
		float3 sample_vec_b = normalize(pos_b_ws);

		float dist_a = length(pos_a_ws);
		float dist_b = length(pos_b_ws);

		float valid_a = step(dist_a, 1);
		float valid_b = step(dist_b, 1);

		float angle_a = saturate(dot(sample_vec_a, normal_ws));
		float angle_b = saturate(dot(sample_vec_b, normal_ws));

		if (valid_a + valid_b > 1) {
			occlusion += (angle_a + angle_b) * (0.5 - 0.25 * (dist_a + dist_b));
			occlusion_count += 1.0;
		}
	}
	}

	occlusion /= max(1.0, occlusion_count);
	float value = 1 - occlusion * u_intensity;

	bindless_rw_textures[u_output][thread_id.xy] = value;
}

