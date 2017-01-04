#include "engine/math_utils.h"
#include "engine/vec.h"
#include <cmath>
#include <random>


namespace Lumix
{
namespace Math
{

Vec3 degreesToRadians(const Vec3& v)
{
	return Vec3(degreesToRadians(v.x), degreesToRadians(v.y), degreesToRadians(v.z));
}

Vec3 radiansToDegrees(const Vec3& v)
{
	return Vec3(radiansToDegrees(v.x), radiansToDegrees(v.y), radiansToDegrees(v.z));
}

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

	float tmin = Math::maximum(
		Math::maximum(Math::minimum(t1, t2), Math::minimum(t3, t4)), Math::minimum(t5, t6));
	float tmax = Math::minimum(
		Math::minimum(Math::maximum(t1, t2), Math::maximum(t3, t4)), Math::maximum(t5, t6));

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
	if (abs(denom) < 1e-5f)
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


LUMIX_ENGINE_API bool getSphereTriangleIntersection(const Vec3& center,
	float radius,
	const Vec3& v0,
	const Vec3& v1,
	const Vec3& v2)
{
	Vec3 normal = crossProduct(v0 - v1, v2 - v1).normalized();
	float D = -dotProduct(v0, normal);

	float dist = dotProduct(center, normal) + D;

	if (fabs(dist) > radius) return false;

	float squared_radius = radius * radius;
	if ((v0 - center).squaredLength() < squared_radius) return true;
	if ((v1 - center).squaredLength() < squared_radius) return true;
	if ((v2 - center).squaredLength() < squared_radius) return true;

	return false;
}


static std::mt19937_64& getGUIDRandomGenerator()
{
	static std::random_device seed;
	static std::mt19937_64 gen(seed());

	return gen;
}

static std::mt19937& getRandomGenerator()
{
	static std::random_device seed;
	static std::mt19937 gen(seed());

	return gen;
}


float pow(float base, float exponent)
{
	return ::pow(base, exponent);
}


u64 randGUID()
{
	return getGUIDRandomGenerator()();
}


u32 rand()
{
	return getRandomGenerator()();
}


u32 rand(u32 from, u32 to)
{
	std::uniform_int_distribution<> dist(from, to);
	return dist(getRandomGenerator());
}


float randFloat()
{
	std::uniform_real_distribution<float> dist;
	return dist(getRandomGenerator());
}


void seedRandom(u32 seed)
{
	getRandomGenerator().seed(seed);
}


float randFloat(float from, float to)
{
	std::uniform_real_distribution<float> dist(from, to);
	return dist(getRandomGenerator());
}


} // ~namespace Math
} // ~namespace Lumix
