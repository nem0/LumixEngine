#include "shaders/common.hlsli"

cbuffer Data : register (b4) {
	float u_accomodation_speed;
	TextureHandle u_image;
	uint u_histogram;
};

groupshared uint histogram[256];

uint luminanceToBin(float lum, float min, float inv_range) {
	if (lum < 1e-3) return 0;
	float logLum = clamp((log2(lum) - min) * inv_range, 0.0, 1.0);
	return uint(logLum * 254.0 + 1.0);
}

float binToLuminance(float bin, float min, float range) {
	if (bin <= 1e-5) return 0;
	float t = saturate((bin - 1) / 254.0);
	return exp2(t * range + min);
}

#if defined PASS2 || defined PASS0
	[numthreads(256, 1, 1)]
#else
	[numthreads(16, 16, 1)]
#endif
void main(uint3 local_thread_id : SV_GroupThreadID, uint3 thread_id : SV_DispatchThreadID) {
	const float range = 12.0;
	const float min = -6;
	#ifdef PASS0
		bindless_rw_buffers[u_histogram].Store(local_thread_id.x * 4, 0);
	#elif !defined PASS2
		uint idx = local_thread_id.x + local_thread_id.y * 16;
		histogram[idx] = 0;

		AllMemoryBarrier();
		GroupMemoryBarrierWithGroupSync();

		if (all(thread_id.xy < uint2(Global_framebuffer_size + 0.5))) {
			float _luminance = luminance(sampleBindlessLod(LinearSamplerClamp, u_image, thread_id.xy * Global_rcp_framebuffer_size, 0).rgb);
			uint bin = luminanceToBin(_luminance, min, 1 / range);
			InterlockedAdd(histogram[bin], 1);
		}
		AllMemoryBarrier();
		GroupMemoryBarrierWithGroupSync();
		
		uint dummy;
		bindless_rw_buffers[u_histogram].InterlockedAdd(idx * 4, histogram[idx], dummy);
	#else
		uint idx = local_thread_id.x;
		histogram[idx] = bindless_rw_buffers[u_histogram].Load(idx * 4) * idx;
		
		AllMemoryBarrier();
		GroupMemoryBarrierWithGroupSync();

		for (uint offset = 256 >> 1; offset > 0; offset >>= 1) {
			if (idx < offset) {
				histogram[idx] += histogram[idx + offset];
			}
			AllMemoryBarrier();
			GroupMemoryBarrierWithGroupSync();
		}

		if (idx == 0) {
			uint sum = histogram[0];
			float avg_bin = sum / float(Global_framebuffer_size.x * Global_framebuffer_size.y);
			float avg_lum = binToLuminance(avg_bin, min, range);
			float accumulator = asfloat(bindless_rw_buffers[u_histogram].Load(256 * 4));
			accumulator += (avg_lum - accumulator) * (1 - exp(-Global_frame_time_delta * u_accomodation_speed));

			bindless_rw_buffers[u_histogram].Store(256 * 4, asuint(accumulator));
		}
	#endif
}

