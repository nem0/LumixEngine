#pragma once


#include "lumix.h"


#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif


namespace Lumix
{
struct Vec3;

namespace Math
{
const float PI = 3.14159265f;
const float SQRT2 = 1.41421356237f;

LUMIX_ENGINE_API bool getRayPlaneIntersecion(const Vec3& origin,
											 const Vec3& dir,
											 const Vec3& plane_point,
											 const Vec3& normal,
											 float& out);
LUMIX_ENGINE_API bool getRaySphereIntersection(const Vec3& origin,
											   const Vec3& dir,
											   const Vec3& center,
											   float radius,
											   Vec3& out);
LUMIX_ENGINE_API bool getRayAABBIntersection(const Vec3& origin,
											 const Vec3& dir,
											 const Vec3& min,
											 const Vec3& size,
											 Vec3& out);

template <typename T> LUMIX_FORCE_INLINE T min(T a, T b)
{
	return a < b ? a : b;
}

template <typename T> LUMIX_FORCE_INLINE T minValue(T a, T b)
{
	return a < b ? a : b;
}

template <typename T> LUMIX_FORCE_INLINE T max(T a, T b)
{
	return a < b ? b : a;
}

template <typename T> LUMIX_FORCE_INLINE T maxValue(T a, T b)
{
	return a < b ? b : a;
}

template <typename T> LUMIX_FORCE_INLINE T abs(T a)
{
	return a > 0 ? a : -a;
}

template <typename T> LUMIX_FORCE_INLINE T signum(T a)
{
	return a > 0 ? (T)1 : (a < 0 ? (T)-1 : 0);
}

template <typename T>
LUMIX_FORCE_INLINE T clamp(T value, T min_value, T max_value)
{
	return min(max(value, min_value), max_value);
}

template <typename T> bool isPowOfTwo(T n)
{
	return (n) && !(n & (n - 1));
}

LUMIX_FORCE_INLINE float degreesToRadians(float angle)
{
	return angle * PI / 180.0f;
}

LUMIX_FORCE_INLINE float radiansToDegrees(float angle)
{
	return angle / PI * 180.0f;
}

inline float easeInOut(float t)
{
	float scaled_t = t * 2;
	if (scaled_t < 1)
	{
		return 0.5f * scaled_t * scaled_t;
	}
	--scaled_t;
	return -0.5f * (scaled_t * (scaled_t - 2) - 1);
}
}
}