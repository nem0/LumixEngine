#include "pipelines/common.hlsli"

// possible future optimizations https://software.intel.com/content/www/us/en/develop/articles/adaptive-screen-space-ambient-occlusion.html
// inspired by https://github.com/tobspr/RenderPipeline/blob/master/rpplugins/ao/shader/ue4ao.kernel.glsl

cbuffer UB : register(b4) {
	float2 u_rcp_size;
	float u_radius;
	float u_intensity;
	uint u_normal_buffer;
	uint u_depth_buffer;
	uint u_output;
};

// get normal in view space
float3 getNormalVS(float2 tex_coord) {
	float3 wnormal = sampleBindlessLod(LinearSamplerClamp, u_normal_buffer, tex_coord, 0).xyz * 2 - 1;
	float4 vnormal = mul(float4(wnormal, 0), Global_ws_to_vs);
	return vnormal.xyz;
}	

// get view-space position of pixel at `screen_uv`
float3 getPositionVS(uint depth_buffer, float2 screen_uv) {
	float depth_ndc = sampleBindlessLod(LinearSamplerClamp, depth_buffer, screen_uv, 0).r;
	float4 pos_ndc = float4(toScreenUV(screen_uv) * 2 - 1, depth_ndc, 1.0);
	float4 pos_vs = mul(pos_ndc, Global_ndc_to_vs);
	return pos_vs.xyz / pos_vs.w;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float2 uv = thread_id.xy * u_rcp_size;
	float3 pos_vs = getPositionVS(u_depth_buffer, uv);
	float3 normal_vs = getNormalVS(uv);
	float occlusion = 0;
	float occlusion_count = 0;

	float c = hash(float2(thread_id.xy) * 0.01 + frac(Global_time)) * 2 - 1;
	float depth_scale = u_radius / pos_vs.z * (c * 2 + 0.1);
	float s = sqrt(1 - c * c); 
	float2x2 rot = float2x2(c, s, -s, c); 
	rot *= depth_scale;

	for (int i = 0; i < 4; ++i) {
		float2 poisson = POISSON_DISK_4[i];
		float2 s = mul(poisson, rot);
		
		float3 pos_a_vs = getPositionVS(u_depth_buffer, uv + s) - pos_vs;
		float3 pos_b_vs = getPositionVS(u_depth_buffer, uv - s) - pos_vs;

		float3 sample_vec_a = normalize(pos_a_vs);
		float3 sample_vec_b = normalize(pos_b_vs);

		float dist_a = length(pos_a_vs);
		float dist_b = length(pos_b_vs);

		float valid_a = step(dist_a, 1);
		float valid_b = step(dist_b, 1);

		float angle_a = saturate(dot(sample_vec_a, normal_vs));
		float angle_b = saturate(dot(sample_vec_b, normal_vs));

		if (valid_a + valid_b > 1) {
			occlusion += (angle_a + angle_b) * (0.5 - 0.25 * (dist_a + dist_b));
			occlusion_count += 1.0;
		}
	}

	occlusion /= max(1.0, occlusion_count);
	float value = 1 - occlusion * u_intensity;

	bindless_rw_textures[u_output][thread_id.xy] = value;
}

