#include "pipelines/common.hlsli"

cbuffer Data : register(b4) {
	TextureHandle u_input;
	RWTextureHandle u_output;
};

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	float4 v = bindless_textures[u_input][thread_id.xy];
	v = float4(ACESFilm(v.rgb), 1);
	bindless_rw_textures[u_output][thread_id.xy] = v;
}