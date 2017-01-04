#pragma once


#include "engine/lumix.h"


namespace Lumix
{
struct Vec3;

namespace Math
{
const float PI = 3.14159265f;
const float HALF_PI = 3.14159265f * 0.5f;
const float SQRT2 = 1.41421356237f;
const float SQRT3 = 1.73205080757f;

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
LUMIX_ENGINE_API float getLineSegmentDistance(const Vec3& origin,
	const Vec3& dir,
	const Vec3& a,
	const Vec3& b);
LUMIX_ENGINE_API bool getRayTriangleIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& a,
	const Vec3& b,
	const Vec3& c,
	float* out_t);
LUMIX_ENGINE_API bool getSphereTriangleIntersection(const Vec3& center,
	float radius,
	const Vec3& v0,
	const Vec3& v1,
	const Vec3& v2);

template <typename T> LUMIX_FORCE_INLINE void swap(T& a, T& b)
{
	T tmp = a;
	a = b;
	b = tmp;
}

template <typename T> LUMIX_FORCE_INLINE T minimum(T a, T b)
{
	return a < b ? a : b;
}

template <typename T> LUMIX_FORCE_INLINE T minimum(T a, T b, T c)
{
	return minimum(minimum(a, b), c);
}

template <typename T> LUMIX_FORCE_INLINE T minimum(T a, T b, T c, T d)
{
	return minimum(minimum(a, b, c), d);
}

template <typename T> LUMIX_FORCE_INLINE T maximum(T a, T b)
{
	return a < b ? b : a;
}

template <typename T> LUMIX_FORCE_INLINE T maximum(T a, T b, T c)
{
	return maximum(maximum(a, b), c);
}

template <typename T> LUMIX_FORCE_INLINE T maximum(T a, T b, T c, T d)
{
	return maximum(maximum(a, b, c), d);
}

LUMIX_FORCE_INLINE float floor(float f)
{
	return float(int(f));
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
	return minimum(maximum(value, min_value), max_value);
}

inline u32 nextPow2(u32 v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

inline u32 log2(u32 v)
{
	u32 r;
	u32 shift;
	r = (v > 0xffff) << 4; v >>= r;
	shift = (v > 0xff) << 3; v >>= shift; r |= shift;
	shift = (v > 0xf) << 2; v >>= shift; r |= shift;
	shift = (v > 0x3) << 1; v >>= shift; r |= shift;
	r |= (v >> 1);
	return r;
}

template <typename T> bool isPowOfTwo(T n)
{
	return (n) && !(n & (n - 1));
}

LUMIX_FORCE_INLINE float degreesToRadians(float angle)
{
	return angle * PI / 180.0f;
}

LUMIX_ENGINE_API Vec3 degreesToRadians(const Vec3& v);

LUMIX_FORCE_INLINE float radiansToDegrees(float angle)
{
	return angle / PI * 180.0f;
}

LUMIX_ENGINE_API Vec3 radiansToDegrees(const Vec3& v);

inline float angleDiff(float a, float b)
{
	float delta = a - b;
	if (delta > PI) return PI * 2 - delta;
	if (delta < -PI) return PI * 2 + delta;
	return delta;
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


LUMIX_ENGINE_API float pow(float base, float exponent);
LUMIX_ENGINE_API u64 randGUID();
LUMIX_ENGINE_API u32 rand();
LUMIX_ENGINE_API u32 rand(u32 from, u32 to);
LUMIX_ENGINE_API void seedRandom(u32 seed);
LUMIX_ENGINE_API float randFloat();
LUMIX_ENGINE_API float randFloat(float from, float to);


} // namespace Math
} // namespace Lumix