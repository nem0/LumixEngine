//@include "pipelines/common.hlsli"

cbuffer Drawcall : register(b4) {
	float u_intensity;
	float u_lumamount;
	uint u_in_out;
	uint u_noise;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float4 in_color = bindless_rw_textures[u_in_out][thread_id.xy];
	
	int2 texture_size = int2(textureSize(bindless_textures[u_noise], 0));
	uint2 ij = (thread_id.xy + Global_random_uint2) % texture_size;
	float3 noise = bindless_textures[u_noise][ij].xyz;
	float _luminance = lerp(0.0, luminance(in_color.rgb), u_lumamount);
	float lum = smoothstep(0.2, 0.0, _luminance) + _luminance;
	lum += _luminance;
	noise = lerp(0, pow(lum, 4.0), noise);

	bindless_rw_textures[u_in_out][thread_id.xy] = float4(in_color.rgb + noise * u_intensity, in_color.a);
}
