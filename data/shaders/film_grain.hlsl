#include "shaders/common.hlsli"

cbuffer Drawcall : register(b4) {
	float u_intensity;
	float u_lumamount;
	RWTextureHandle u_in_out;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	
	float4 in_color = bindless_rw_textures[u_in_out][thread_id.xy];
	float _luminance = lerp(0.0, luminance(in_color.rgb), u_lumamount);
	float lum = smoothstep(0.2, 0.0, _luminance) + _luminance;
	lum += _luminance;

	float2 noise_uv = (thread_id.xy + Global_random_float2_normalized) * 0.001;
	float3 noise = hash(noise_uv) * 2 -1;
	noise = lerp(0, pow(lum, 4.0), noise);

	bindless_rw_textures[u_in_out][thread_id.xy] = float4(in_color.rgb + noise * u_intensity, in_color.a);
}
