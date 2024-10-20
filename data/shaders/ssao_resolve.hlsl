#include "shaders/common.hlsli"

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout (binding=0) uniform sampler2D u_depthbuf;
layout (binding=1) uniform sampler2D u_history;
layout (binding=2, r8) uniform image2D u_current;

layout(std140, binding = 4) uniform Data {
	vec4 u_size;
	float u_current_frame_weight;
	float u_disocclusion_strength;
};

void main()
{
	ivec2 ij = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(ij, ivec2(u_size.xy)))) return;

	vec2 uv = (vec2(ij) + 0.5) / u_size.xy;
	float depth = texture(u_depthbuf, uv).x;
	
	vec2 uv_prev = cameraReproject(uv, depth);
	float prev_depth = texture(u_depthbuf, uv_prev).x;

	if (all(lessThan(uv_prev, vec2(1))) && all(greaterThan(uv_prev, vec2(0)))) {
		float current = imageLoad(u_current, ij).x;
		float prev = texture(u_history, uv_prev).x;
		float disocclusion = abs(prev_depth - depth);
		disocclusion = saturate(disocclusion * u_disocclusion_strength);
		vec4 v = vec4(mix(prev, current, u_current_frame_weight));
		v = mix(v, vec4(1), disocclusion);
		imageStore(u_current, ij, v);
	}
	else {
		float current = imageLoad(u_current, ij).x;
		float v = mix(1, current, u_current_frame_weight);
		imageStore(u_current, ij, vec4(v));
	}
}

