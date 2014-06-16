#include "core/math_utils.h"


namespace Lumix
{
	namespace Math
	{
		Vec3 getRayPlaneIntersecion(const Vec3& origin, const Vec3& dir, const Vec3& plane_point, const Vec3& normal)
		{
			float d = dotProduct(dir, normal);
			if(d == 0) 
			{
				return origin;
			}
			d = dotProduct(plane_point - origin, normal) / d;
			return dir * d + origin;
		}

		bool getRaySphereIntersection(const Vec3& center, float radius, const Vec3& origin, const Vec3& dir, Vec3& out)
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
	} // ~namespace Math
} // ~namespace Lumix
