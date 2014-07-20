#include "core/math_utils.h"


namespace Lumix
{
	namespace Math
	{
		bool getRayPlaneIntersecion(const Vec3& origin, const Vec3& dir, const Vec3& plane_point, const Vec3& normal, float& out)
		{
			float d = dotProduct(dir, normal);
			if(d == 0) 
			{
				return false;
			}
			d = dotProduct(plane_point - origin, normal) / d;
			out = d;
			return true;
		}

		bool getRaySphereIntersection(const Vec3& origin, const Vec3& dir, const Vec3& center, float radius, Vec3& out)
		{
			ASSERT(dir.length() < 1.01f && dir.length() > 0.99f);
			Vec3 L = center - origin;
			float tca = dotProduct(L, dir);
			if (tca < 0) return false;
			float d2 = dotProduct(L, L) - tca * tca;
			if (d2 > radius * radius) return false;
			float thc = sqrt(radius * radius - d2);
			float t0 = tca - thc;
			//float t1 = tca + thc;
			out = origin + dir * t0;
			return true;
		}

		bool getRayAABBIntersection(const Vec3& origin, const Vec3& dir, const Vec3& min, const Vec3& size, Vec3& out)
		{
			Vec3 dirfrac;

			dirfrac.x = 1.0f / dir.x;
			dirfrac.y = 1.0f / dir.y;
			dirfrac.z = 1.0f / dir.z;

			Vec3 max = min + size;
			float t1 = (min.x - origin.x) * dirfrac.x;
			float t2 = (max.x - origin.x) * dirfrac.x;
			float t3 = (min.y - origin.y) * dirfrac.y;
			float t4 = (max.y - origin.y) * dirfrac.y;
			float t5 = (min.z - origin.z) * dirfrac.z;
			float t6 = (max.z - origin.z) * dirfrac.z;

			float tmin = Math::maxValue(Math::maxValue(Math::minValue(t1, t2), Math::minValue(t3, t4)), Math::minValue(t5, t6));
			float tmax = Math::minValue(Math::minValue(Math::maxValue(t1, t2), Math::maxValue(t3, t4)), Math::maxValue(t5, t6));

			if (tmax < 0)
			{
				return false;
			}

			if (tmin > tmax)
			{
				return false;
			}

			out = tmin < 0 ? origin : origin + dir * tmin;
			return true;
		}
	} // ~namespace Math
} // ~namespace Lumix
