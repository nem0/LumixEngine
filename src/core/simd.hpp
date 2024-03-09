#pragma once


#include "core/core.hpp"


#if defined _WIN32 && !defined __clang__
	#include <intrin.h>
	#include <xmmintrin.h>
#else
	#include <math.h>
	#include <string.h>
#endif

namespace Lumix
{


#if defined _WIN32 && !defined __clang__
	using float4 = __m128;


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

	LUMIX_FORCE_INLINE float f4GetX(float4 v)
	{
		return _mm_cvtss_f32(v);
	}

	LUMIX_FORCE_INLINE float f4GetY(float4 v)
	{
		float4 r = _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 1));
		return _mm_cvtss_f32(r);
	}

	LUMIX_FORCE_INLINE float f4GetZ(float4 v)
	{
		float4 r = _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 2));
		return _mm_cvtss_f32(r);
	}

	LUMIX_FORCE_INLINE float f4GetW(float4 v)
	{
		float4 r = _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 3));
		return _mm_cvtss_f32(r);
	}

	LUMIX_FORCE_INLINE void f4Store(void* dest, float4 src)
	{
		_mm_store_ps((float*)dest, src);
	}

	LUMIX_FORCE_INLINE float4 f4Blend(float4 false_val, float4 true_val, float4 mask)
	{
		return _mm_blendv_ps(false_val, true_val, mask);
	}

	LUMIX_FORCE_INLINE float4 f4CmpGT(float4 a, float4 b)
	{
		return _mm_cmpgt_ps(a, b);
	}

	LUMIX_FORCE_INLINE float4 f4CmpLT(float4 a, float4 b)
	{
		return _mm_cmplt_ps(a, b);
	}

	LUMIX_FORCE_INLINE float4 f4Or(float4 a, float4 b)
	{
		return _mm_or_ps(a, b);
	}

	LUMIX_FORCE_INLINE float4 f4And(float4 a, float4 b)
	{
		return _mm_and_ps(a, b);
	}

	LUMIX_FORCE_INLINE int f4MoveMask(float4 a)
	{
		return _mm_movemask_ps(a);
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

	LUMIX_FORCE_INLINE float4 operator +(float4 a, float4 b) {
		return _mm_add_ps(a, b);
	}

	LUMIX_FORCE_INLINE float4 operator -(float4 a, float4 b) {
		return _mm_sub_ps(a, b);
	}

	LUMIX_FORCE_INLINE float4 operator *(float4 a, float4 b) {
		return _mm_mul_ps(a, b);
	}

#else 
	struct float4
	{
		// TODO union with u32
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


	LUMIX_FORCE_INLINE float f4GetX(float4 v)
	{
		return v.x;
	}


	LUMIX_FORCE_INLINE float f4GetY(float4 v)
	{
		return v.y;
	}


	LUMIX_FORCE_INLINE float f4GetZ(float4 v)
	{
		return v.z;
	}


	LUMIX_FORCE_INLINE float f4GetW(float4 v)
	{
		return v.w;
	}


	LUMIX_FORCE_INLINE void f4Store(void* dest, float4 src)
	{
		(*(float4*)dest) = src;
	}

	LUMIX_FORCE_INLINE float4 f4And(float4 a, float4 b)
	{
		static const float true_val = [](){
			u32 u = 0xffFFffFF;
			float f;
			memcpy(&f, &u, sizeof(f));
			return f;
		}();
		return {
			a.x && b.x ? true_val : 0,
			a.y && b.y ? true_val : 0,
			a.z && b.z ? true_val : 0,
			a.w && b.w ? true_val : 0
		};
	}

	LUMIX_FORCE_INLINE float4 f4Or(float4 a, float4 b)
	{
		static const float true_val = [](){
			u32 u = 0xffFFffFF;
			float f;
			memcpy(&f, &u, sizeof(f));
			return f;
		}();
		return {
			a.x || b.x ? true_val : 0,
			a.y || b.y ? true_val : 0,
			a.z || b.z ? true_val : 0,
			a.w || b.w ? true_val : 0
		};
	}

	LUMIX_FORCE_INLINE float4 f4Blend(float4 false_val, float4 true_val, float4 mask) {
		u32 umask[4];
		memcpy(umask, &mask, sizeof(mask));
		
		float4 res;
		res.x = umask[0] & (1 << 31) ? true_val.x : false_val.x;
		res.y = umask[1] & (1 << 31) ? true_val.y : false_val.y;
		res.z = umask[2] & (1 << 31) ? true_val.z : false_val.z;
		res.w = umask[3] & (1 << 31) ? true_val.w : false_val.w;
		return res;
	}

	LUMIX_FORCE_INLINE float4 f4CmpGT(float4 a, float4 b)
	{
		static const float gt = [](){
			u32 u = 0xffFFffFF;
			float f;
			memcpy(&f, &u, sizeof(f));
			return f;
		}();
		return {
			a.x > b.x ? gt : 0,
			a.y > b.y ? gt : 0,
			a.z > b.z ? gt : 0,
			a.w > b.w ? gt : 0
		};
	}
	
	LUMIX_FORCE_INLINE float4 f4CmpLT(float4 a, float4 b)
	{
		static const float lt = [](){
			u32 u = 0xffFFffFF;
			float f;
			memcpy(&f, &u, sizeof(f));
			return f;
		}();
		return {
			a.x < b.x ? lt : 0,
			a.y < b.y ? lt : 0,
			a.z < b.z ? lt : 0,
			a.w < b.w ? lt : 0
		};
	}
	LUMIX_FORCE_INLINE int f4MoveMask(float4 a)
	{
		return (a.w < 0 ? (1 << 3) : 0) | 
			(a.z < 0 ? (1 << 2) : 0) | 
			(a.y < 0 ? (1 << 1) : 0) | 
			(a.x < 0 ? 1 : 0);
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
			(float)sqrt(a.x),
			(float)sqrt(a.y),
			(float)sqrt(a.z),
			(float)sqrt(a.w)
		};
	}


	LUMIX_FORCE_INLINE float4 f4Rsqrt(float4 a)
	{
		return{
			1 / (float)sqrt(a.x),
			1 / (float)sqrt(a.y),
			1 / (float)sqrt(a.z),
			1 / (float)sqrt(a.w)
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

	LUMIX_FORCE_INLINE float4 operator +(float4 a, float4 b) {
		return f4Add(a, b);
	}

	LUMIX_FORCE_INLINE float4 operator -(float4 a, float4 b) {
		return f4Sub(a, b);
	}

	LUMIX_FORCE_INLINE float4 operator *(float4 a, float4 b) {
		return f4Mul(a, b);
	}

#endif



} // namespace Lumix