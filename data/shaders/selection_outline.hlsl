#include "shaders/common.hlsli"

cbuffer DC : register(b4) {
	TextureHandle u_mask;
	RWTextureHandle u_output;
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	if (bindless_textures[u_mask][thread_id.xy].r > 0) return;
	
	int c = 0;
	for(int i = 0; i < 3; ++i) {
		if (bindless_textures[u_mask][thread_id.xy + int2(i, 0)].r == 0) ++c;
		if (bindless_textures[u_mask][thread_id.xy + int2(-i, 0)].r == 0) ++c;
		if (bindless_textures[u_mask][thread_id.xy + int2(0, i)].r == 0) ++c;
		if (bindless_textures[u_mask][thread_id.xy + int2(0, -i)].r == 0) ++c;
	}

	if (c != 12) {
		bindless_rw_textures[u_output][thread_id.xy] = float4(1, 0.5, 0, 1.0f);
	}
}
