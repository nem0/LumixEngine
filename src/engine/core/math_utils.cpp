#include "core/math_utils.h"
#include "core/vec.h"
#include <cmath>


namespace Lumix
{
namespace Math
{


bool getRayPlaneIntersecion(const Vec3& origin,
	const Vec3& dir,
	const Vec3& plane_point,
	const Vec3& normal,
	float& out)
{
	float d = dotProduct(dir, normal);
	if (d == 0)
	{
		return false;
	}
	d = dotProduct(plane_point - origin, normal) / d;
	out = d;
	return true;
}

bool getRaySphereIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& center,
	float radius,
	Vec3& out)
{
	ASSERT(dir.length() < 1.01f && dir.length() > 0.99f);
	Vec3 L = center - origin;
	float tca = dotProduct(L, dir);
	if (tca < 0) return false;
	float d2 = dotProduct(L, L) - tca * tca;
	if (d2 > radius * radius) return false;
	float thc = sqrt(radius * radius - d2);
	float t0 = tca - thc;
	out = origin + dir * t0;
	return true;
}

bool getRayAABBIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& min,
	const Vec3& size,
	Vec3& out)
{
	Vec3 dirfrac;

	dirfrac.x = 1.0f / (dir.x == 0 ? 0.00000001f : dir.x);
	dirfrac.y = 1.0f / (dir.y == 0 ? 0.00000001f : dir.y);
	dirfrac.z = 1.0f / (dir.z == 0 ? 0.00000001f : dir.z);

	Vec3 max = min + size;
	float t1 = (min.x - origin.x) * dirfrac.x;
	float t2 = (max.x - origin.x) * dirfrac.x;
	float t3 = (min.y - origin.y) * dirfrac.y;
	float t4 = (max.y - origin.y) * dirfrac.y;
	float t5 = (min.z - origin.z) * dirfrac.z;
	float t6 = (max.z - origin.z) * dirfrac.z;

	float tmin = Math::maxValue(
		Math::maxValue(Math::minValue(t1, t2), Math::minValue(t3, t4)), Math::minValue(t5, t6));
	float tmax = Math::minValue(
		Math::minValue(Math::maxValue(t1, t2), Math::maxValue(t3, t4)), Math::maxValue(t5, t6));

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


float getLineSegmentDistance(const Vec3& origin, const Vec3& dir, const Vec3& a, const Vec3& b)
{
	Vec3 a_origin = origin - a;
	Vec3 ab = b - a;

	float dot1 = dotProduct(ab, a_origin);
	float dot2 = dotProduct(ab, dir);
	float dot3 = dotProduct(dir, a_origin);
	float dot4 = dotProduct(ab, ab);
	float dot5 = dotProduct(dir, dir);

	float denom = dot4 * dot5 - dot2 * dot2;
	if (fabsf(denom) < 1e-5f)
	{
		Vec3 X = origin + dir * dotProduct(b - origin, dir);
		return (b - X).length();
	}

	float numer = dot1 * dot2 - dot3 * dot4;
	float param_a = numer / denom;
	float param_b = (dot1 + dot2 * param_a) / dot4;

	if (param_b < 0 || param_b > 1)
	{
		param_b = Math::clamp(param_b, 0.0f, 1.0f);
		Vec3 B = a + ab * param_b;
		Vec3 X = origin + dir * dotProduct(b - origin, dir);
		return (B - X).length();
	}

	Vec3 vec = (origin + dir * param_a) - (a + ab * param_b);
	return vec.length();
}


bool getRayTriangleIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& p0,
	const Vec3& p1,
	const Vec3& p2,
	float* out_t)
{
	Vec3 normal = crossProduct(p1 - p0, p2 - p0);
	float q = dotProduct(normal, dir);
	if (q == 0) return false;

	float d = -dotProduct(normal, p0);
	float t = -(dotProduct(normal, origin) + d) / q;
	if (t < 0) return false;

	Vec3 hit_point = origin + dir * t;

	Vec3 edge0 = p1 - p0;
	Vec3 VP0 = hit_point - p0;
	if (dotProduct(normal, crossProduct(edge0, VP0)) < 0)
	{
		return false;
	}

	Vec3 edge1 = p2 - p1;
	Vec3 VP1 = hit_point - p1;
	if (dotProduct(normal, crossProduct(edge1, VP1)) < 0)
	{
		return false;
	}

	Vec3 edge2 = p0 - p2;
	Vec3 VP2 = hit_point - p2;
	if (dotProduct(normal, crossProduct(edge2, VP2)) < 0)
	{
		return false;
	}

	if (out_t) *out_t = t;
	return true;
}


} // ~namespace Math
} // ~namespace Lumix
