#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	TextureHandle u_input;
	RWTextureHandle u_output0;
	RWTextureHandle u_output1;
	RWTextureHandle u_output2;
	RWTextureHandle u_output3;
	RWTextureHandle u_output4;
};

groupshared float scratch_depths[8][8];

float downsampleFunc(float depth00, float depth10, float depth11, float depth01) {
#if 0	
	return (depth00 + depth10 + depth11 + depth01) * 0.25;
#else
	return max(depth00, max(depth10, max(depth11, depth01)));
#endif
}

[numthreads(8, 8, 1)]
void main(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID) {
	float depth00 = bindless_textures[u_input][thread_id.xy * 2].r;
	float depth10 = bindless_textures[u_input][thread_id.xy * 2 + int2(1, 0)].r;
	float depth11 = bindless_textures[u_input][thread_id.xy * 2 + int2(1, 1)].r;
	float depth01 = bindless_textures[u_input][thread_id.xy * 2 + int2(0, 1)].r;
	
	// mip0
	bindless_rw_textures[u_output0][thread_id.xy * 2] = depth00;
	bindless_rw_textures[u_output0][thread_id.xy * 2 + int2(1, 0)] = depth10;
	bindless_rw_textures[u_output0][thread_id.xy * 2 + int2(1, 1)] = depth11;
	bindless_rw_textures[u_output0][thread_id.xy * 2 + int2(0, 1)] = depth01;

	// mip 1
	float d = downsampleFunc(depth00, depth10, depth11, depth01);
	bindless_rw_textures[u_output1][thread_id.xy] = d;
	scratch_depths[group_thread_id.y][group_thread_id.x] = d;

	GroupMemoryBarrierWithGroupSync();

	// mip 2
	if (all(group_thread_id % 2 == 0)) {
		float d00 = scratch_depths[group_thread_id.y][group_thread_id.x];
		float d10 = scratch_depths[group_thread_id.y + 1][group_thread_id.x];
		float d01 = scratch_depths[group_thread_id.y][group_thread_id.x + 1];
		float d11 = scratch_depths[group_thread_id.y + 1][group_thread_id.x + 1];
		float d = downsampleFunc(d00, d10, d11, d01);
		bindless_rw_textures[u_output2][thread_id.xy >> 1] = d;
		scratch_depths[group_thread_id.y][group_thread_id.x] = d;
	}
	
	GroupMemoryBarrierWithGroupSync();

	// mip 3
	if (all(group_thread_id % 4 == 0)) {
		float d00 = scratch_depths[group_thread_id.y][group_thread_id.x];
		float d10 = scratch_depths[group_thread_id.y + 2][group_thread_id.x];
		float d01 = scratch_depths[group_thread_id.y][group_thread_id.x + 2];
		float d11 = scratch_depths[group_thread_id.y + 2][group_thread_id.x + 2];
		bindless_rw_textures[u_output3][thread_id.xy >> 2] = downsampleFunc(d00, d10, d11, d01);
		scratch_depths[group_thread_id.y][group_thread_id.x] = d;
	}
	
	GroupMemoryBarrierWithGroupSync();

	// mip 4
	if (all(group_thread_id % 8 == 0)) {
		float d00 = scratch_depths[group_thread_id.y][group_thread_id.x];
		float d10 = scratch_depths[group_thread_id.y + 4][group_thread_id.x];
		float d01 = scratch_depths[group_thread_id.y][group_thread_id.x + 4];
		float d11 = scratch_depths[group_thread_id.y + 4][group_thread_id.x + 4];
		bindless_rw_textures[u_output4][thread_id.xy >> 3] = downsampleFunc(d00, d10, d11, d01);
	}
}
