#include "shaders/common.hlsli"

cbuffer Data : register(b4) {
	float2 u_size;
	TextureHandle u_history;
	TextureHandle u_current;
	TextureHandle u_motion_vectors;
	RWTextureHandle u_output;
};

// https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
float4 catmullRom(uint tex, float2 uv, float2 texSize) {
	float2 samplePos = uv * texSize;
	float2 texPos1 = floor(samplePos - 0.5) + 0.5;

	float2 f = samplePos - texPos1;

	float2 w0 = f * (-0.5 + f * (1.0f - 0.5 * f));
	float2 w1 = 1.0f + f * f * (-2.5 + 1.5 * f);
	float2 w2 = f * (0.5 + f * (2.0f - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);

	float2 w12 = w1 + w2;
	float2 offset12 = w2 / (w1 + w2);

	float2 texPos0 = texPos1 - 1;
	float2 texPos3 = texPos1 + 2;
	float2 texPos12 = texPos1 + offset12;

	texPos0 /= texSize;
	texPos3 /= texSize;
	texPos12 /= texSize;

	float4 result = 0;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
	result += sampleBindlessLod(LinearSamplerClamp, tex, float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

	return result;
}

// https://www.iquilezles.org/www/articles/texture/texture.htm
float4 getTexel(uint tex, float2 p, float2 res) {
	#if 1
		return catmullRom(tex, p, res);
	#else
		p = p * res + 0.5;

		float2 i = floor(p);
		float2 f = p - i;
		f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
		p = i + f;

		p = (p - 0.5) / res;
		return sampleBindlessLod(LinearSamplerClamp, tex, p, 0);
	#endif
}

// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID) {
	int2 ij = int2(thread_id.xy);

	float2 uv = (float2(ij) + 0.5) / u_size.xy;
	float2 motionvec = sampleBindlessLod(LinearSamplerClamp, u_motion_vectors, uv, 0).xy;
	#ifdef _ORIGIN_BOTTOM_LEFT
		motionvec *= 0.5;
	#else
		motionvec *= float2(0.5, -0.5);
	#endif

	float2 uv_prev = uv + motionvec;

	float4 current = sampleBindlessLod(LinearSamplerClamp, u_current, uv /*- Global.pixel_jitter*/, 0);
	if (all(uv_prev < 1) && all(uv_prev > 0)) {
		float4 prev = getTexel(u_history, uv_prev, u_size);

		#if 1 // color clamping
			float4 minColor = 9001.0;
			float4 maxColor = -9001.0;

			#define ITER(x, y) \
			{ \
				float4 color = sampleBindlessLodOffset(LinearSamplerClamp, u_current, uv, 0, int2(x, y)); \
				minColor = min(minColor, color); \
				maxColor = max(maxColor, color); \
			}				
			ITER(-1, -1)
			ITER(-1, 0)
			ITER(-1, 1)
			ITER(0, -1)
			ITER(0, 1)
			ITER(1, -1)
			ITER(1, 0)
			ITER(1, 1)
			prev = clamp(prev, minColor, maxColor);
		#endif

		float lum0 = luminance(current.rgb);
		float lum1 = luminance(prev.rgb);

		float d = 1 - abs(lum0 - lum1) / max(lum0, max(lum1, 0.1));
		float k_feedback = lerp(0.9, 0.99, d * d);

		current = lerp(current, prev, k_feedback);
	}

	bindless_rw_textures[u_output][ij] = current;
}
