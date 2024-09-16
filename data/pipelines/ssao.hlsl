//@include "pipelines/common.hlsli"

// possible future optimizations https://software.intel.com/content/www/us/en/develop/articles/adaptive-screen-space-ambient-occlusion.html
// inspired by https://github.com/tobspr/RenderPipeline/blob/master/rpplugins/ao/shader/ue4ao.kernel.glsl

cbuffer UB : register(b4) {
	float u_radius;
	float u_intensity;
	float u_width;
	float u_height;
	uint u_normal_buffer;
	uint u_depth_buffer;
	uint u_output;
};

// get normal in view space
float3 getViewNormal(float2 tex_coord) {
	float3 wnormal = sampleBindlessLod(LinearSamplerClamp, u_normal_buffer, tex_coord, 0).xyz * 2 - 1;
	float4 vnormal = mul(float4(wnormal, 0), Global_view);
	return vnormal.xyz;
}	

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	if (any(thread_id.xy > uint2(u_width, u_height))) return;
	
	float2 uv = thread_id.xy / float2(u_width, u_height);
	float3 view_pos = getViewPosition(u_depth_buffer, Global_inv_projection, uv);
	float3 view_normal = getViewNormal(uv);
	float occlusion = 0;
	float occlusion_count = 0;


	float c = hash(view_pos.xyz + frac(Global_time) * 0.001) * 2 - 1;
	float depth_scale = u_radius / view_pos.z * (c * 2 + 0.1);
	float s = sqrt(1 - c * c); 
	float2x2 rot = float2x2(c, s, -s, c); 
	rot *= depth_scale;

	for (int i = 0; i < 4; ++i) {
		float2 poisson = POISSON_DISK_4[i];
		float2 s = mul(poisson, rot);
		
		float3 vpos_a = getViewPosition(u_depth_buffer, Global_inv_projection, uv + s) - view_pos;
		float3 vpos_b = getViewPosition(u_depth_buffer, Global_inv_projection, uv - s) - view_pos;

		float3 sample_vec_a = normalize(vpos_a);
		float3 sample_vec_b = normalize(vpos_b);

		float dist_a = length(vpos_a);
		float dist_b = length(vpos_b);

		float valid_a = step(dist_a, 1);
		float valid_b = step(dist_b, 1);

		float angle_a = saturate(dot(sample_vec_a, view_normal));
		float angle_b = saturate(dot(sample_vec_b, view_normal));

		if (valid_a + valid_b > 1) {
			occlusion += (angle_a + angle_b) * (0.5 - 0.25 * (dist_a + dist_b));
			occlusion_count += 1.0;
		}
	}

	occlusion /= max(1.0, occlusion_count);
	float value = 1 - occlusion * u_intensity;

	bindless_rw_textures[u_output][thread_id.xy] = value;
}

