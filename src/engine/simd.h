#pragma once


#include "engine/lumix.h"


#ifdef _WIN32
	#include <xmmintrin.h>
#else
	#include <cmath>
#endif

namespace Lumix
{


#ifdef _WIN32
	typedef __m128 float4;


	LUMIX_FORCE_INLINE float4 f4LoadUnaligned(const void* src)
	{
		return _mm_loadu_ps((const float*)(src));
	}


	LUMIX_FORCE_INLINE float4 f4Load(const void* src)
	{
		return _mm_load_ps((const float*)(src));
	}


	LUMIX_FORCE_INLINE float4 f4Splat(float value)
	{
		return _mm_set_ps1(value);
	}


	LUMIX_FORCE_INLINE void f4Store(void* dest, float4 src)
	{
		_mm_store_ps((float*)dest, src);
	}


	LUMIX_FORCE_INLINE float4 f4Add(float4 a, float4 b)
	{
		return _mm_add_ps(a, b);
	}


	LUMIX_FORCE_INLINE float4 f4Sub(float4 a, float4 b)
	{
		return _mm_sub_ps(a, b);
	}


	LUMIX_FORCE_INLINE float4 f4Mul(float4 a, float4 b)
	{
		return _mm_mul_ps(a, b);
	}


	LUMIX_FORCE_INLINE float4 f4Div(float4 a, float4 b)
	{
		return _mm_div_ps(a, b);
	}


	LUMIX_FORCE_INLINE float4 f4Rcp(float4 a)
	{
		return _mm_rcp_ps(a);
	}


	LUMIX_FORCE_INLINE float4 f4Sqrt(float4 a)
	{
		return _mm_sqrt_ps(a);
	}


	LUMIX_FORCE_INLINE float4 f4Rsqrt(float4 a)
	{
		return _mm_rsqrt_ps(a);
	}


	LUMIX_FORCE_INLINE float4 f4Min(float4 a, float4 b)
	{
		return _mm_min_ps(a, b);
	}


	LUMIX_FORCE_INLINE float4 f4Max(float4 a, float4 b)
	{
		return _mm_max_ps(a, b);
	}

#else 
	struct float4
	{
		float x, y, z, w;
	};


	LUMIX_FORCE_INLINE float4 f4LoadUnaligned(const void* src)
	{
		return *(const float4*)src;
	}


	LUMIX_FORCE_INLINE float4 f4Load(const void* src)
	{
		return *(const float4*)src;
	}


	LUMIX_FORCE_INLINE float4 f4Splat(float value)
	{
		return {value, value, value, value};
	}


	LUMIX_FORCE_INLINE void f4Store(void* dest, float4 src)
	{
		(*(float4*)dest) = src;
	}


	LUMIX_FORCE_INLINE float4 f4Add(float4 a, float4 b)
	{
		return{
			a.x + b.x,
			a.y + b.y,
			a.z + b.z,
			a.w + b.w
		};
	}


	LUMIX_FORCE_INLINE float4 f4Sub(float4 a, float4 b)
	{
		return{
			a.x - b.x,
			a.y - b.y,
			a.z - b.z,
			a.w - b.w
		};
	}


	LUMIX_FORCE_INLINE float4 f4Mul(float4 a, float4 b)
	{
		return{
			a.x * b.x,
			a.y * b.y,
			a.z * b.z,
			a.w * b.w
		};
	}


	LUMIX_FORCE_INLINE float4 f4Div(float4 a, float4 b)
	{
		return{
			a.x / b.x,
			a.y / b.y,
			a.z / b.z,
			a.w / b.w
		};
	}


	LUMIX_FORCE_INLINE float4 f4Rcp(float4 a)
	{
		return{
			1 / a.x,
			1 / a.y,
			1 / a.z,
			1 / a.w
		};
	}


	LUMIX_FORCE_INLINE float4 f4Sqrt(float4 a)
	{
		return{
			sqrt(a.x),
			sqrt(a.y),
			sqrt(a.z),
			sqrt(a.w)
		};
	}


	LUMIX_FORCE_INLINE float4 f4Rsqrt(float4 a)
	{
		return{
			1 / sqrt(a.x),
			1 / sqrt(a.y),
			1 / sqrt(a.z),
			1 / sqrt(a.w)
		};
	}


	LUMIX_FORCE_INLINE float4 f4Min(float4 a, float4 b)
	{
		return{
			a.x < b.x ? a.x : b.x,
			a.y < b.y ? a.y : b.y,
			a.z < b.z ? a.z : b.z,
			a.w < b.w ? a.w : b.w
		};
	}


	LUMIX_FORCE_INLINE float4 f4Max(float4 a, float4 b)
	{
		return{
			a.x > b.x ? a.x : b.x,
			a.y > b.y ? a.y : b.y,
			a.z > b.z ? a.z : b.z,
			a.w > b.w ? a.w : b.w
		};
	}

#endif



} // namespace Lumix