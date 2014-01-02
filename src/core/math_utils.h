#pragma once


#include "core/lux.h"
#include "core/vec3.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif


namespace Lux
{
	namespace Math
	{
		LUX_CORE_API Vec3 getRayPlaneIntersecion(const Vec3& origin, const Vec3& dir, const Vec3& plane_point, const Vec3& normal);

		template <typename T>
		LUX_FORCE_INLINE T min(T a, T b)
		{
			return a < b ? a : b;
		}

		template <typename T>
		LUX_FORCE_INLINE T max(T a, T b)
		{
			return a < b ? b : a;
		}

		template <typename T>
		LUX_FORCE_INLINE T abs(T a)
		{
			return a > 0 ? a : -a;
		}

		template <typename T>
		LUX_FORCE_INLINE T clamp(T value, T min_value, T max_value)
		{
			return min(max(value, min_value), max_value);
		}
	}
}