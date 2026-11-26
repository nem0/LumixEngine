#include "engine/shaders/common.hlsli"

cbuffer Data : register(b4) {
	float2 u_size;
	TextureHandle u_history;
	TextureHandle u_current;
	TextureHandle u_motion_vectors;
	RWTextureHandle u_output;
	RWTextureHandle u_history_out;
};

// 5-tap Catmull-Rom (drops the 4 tiny-weight corner taps and renormalizes).
float4 catmullRom5(TextureHandle tex, float2 uv, float2 texSize) {
	float2 samplePos = uv * texSize;
	float2 texPos1 = floor(samplePos - 0.5) + 0.5;

	float2 f = samplePos - texPos1;

	float2 w0 = f * (-0.5 + f * (1.0f - 0.5 * f));
	float2 w1 = 1.0f + f * f * (-2.5 + 1.5 * f);
	float2 w2 = f * (0.5 + f * (2.0f - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);

	float2 w12 = w1 + w2;
	float2 offset12 = w2 / w12;

	float2 invSize = 1.0 / texSize;
	float2 texPos0 = (texPos1 - 1.0) * invSize;
	float2 texPos3 = (texPos1 + 2.0) * invSize;
	float2 texPos12 = (texPos1 + offset12) * invSize;

	float wt0 = w12.x * w0.y;
	float wt1 = w0.x * w12.y;
	float wt2 = w12.x * w12.y;
	float wt3 = w3.x * w12.y;
	float wt4 = w12.x * w3.y;

	float4 result = 0;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos12.x, texPos0.y), 0.0f) * wt0;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos0.x, texPos12.y), 0.0f) * wt1;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos12.x, texPos12.y), 0.0f) * wt2;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos3.x, texPos12.y), 0.0f) * wt3;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos12.x, texPos3.y), 0.0f) * wt4;

	return result / (wt0 + wt1 + wt2 + wt3 + wt4);
}

groupshared float4 s_current_tile[18][18];

// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID, uint3 local_id : SV_GroupThreadID, uint local_idx : SV_GroupIndex) {
	int2 size_i = int2(u_size);
	int2 max_coord = size_i - 1;
	int2 tile_origin = int2(group_id.xy) * 16 - 1;

	// Cooperatively load the 18x18 neighborhood of u_current into groupshared memory (1.27 point loads/thread).
	for (uint idx = local_idx; idx < 18 * 18; idx += 256) {
		int ly = int(idx / 18);
		int lx = int(idx - uint(ly) * 18);
		int2 coord = clamp(tile_origin + int2(lx, ly), int2(0, 0), max_coord);
		s_current_tile[ly][lx] = bindless_textures[u_current][coord];
	}
	GroupMemoryBarrierWithGroupSync();

	int2 ij = int2(thread_id.xy);
	if (ij.x >= size_i.x || ij.y >= size_i.y) return;

	float2 uv = (float2(ij) + 0.5) / u_size.xy;
	float2 motionvec = bindless_textures[u_motion_vectors][ij].xy;
	#ifdef _ORIGIN_BOTTOM_LEFT
		motionvec *= 0.5;
	#else
		motionvec *= float2(0.5, -0.5);
	#endif

	float2 uv_prev = uv + motionvec;

	int lx = int(local_id.x) + 1;
	int ly = int(local_id.y) + 1;
	float4 current = s_current_tile[ly][lx];

	if (all(uv_prev < 1) && all(uv_prev > 0)) {
		float4 prev = catmullRom5(u_history, uv_prev, u_size);

		#if 1 // color clamping from groupshared 3x3 neighborhood
			float4 minColor = current;
			float4 maxColor = current;

			[unroll]
			for (int dy = -1; dy <= 1; ++dy) {
				[unroll]
				for (int dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dy == 0) continue;
					float4 color = s_current_tile[ly + dy][lx + dx];
					minColor = min(minColor, color);
					maxColor = max(maxColor, color);
				}
			}
			prev = clamp(prev, minColor, maxColor);
		#endif

		float lum0 = luminance(current.rgb);
		float lum1 = luminance(prev.rgb);

		float d = 1 - abs(lum0 - lum1) / max(lum0, max(lum1, 0.1));
		float k_feedback = lerp(0.9, 0.99, d * d);

		current = lerp(current, prev, k_feedback);
	}

	// Sub-LSB temporal dithering prevents green/magenta quantization drift in R11G11B10F (6/6/5 mantissa bits).
	float dither = hash(float2(ij) + frac(Global_time) * 61.0) - 0.5;
	current.rgb = max(0.0, current.rgb + current.rgb * (dither * float3(1.0 / 64.0, 1.0 / 64.0, 1.0 / 32.0)));

	bindless_rw_textures[u_output][ij] = current;
	bindless_rw_textures[u_history_out][ij] = current;
}
