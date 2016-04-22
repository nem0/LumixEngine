#pragma once

#include "engine/lumix.h"
#include "engine/core/math_utils.h"
#include "engine/core/matrix.h"
#include "engine/core/vec.h"


namespace Lumix
{


struct Plane
{
	Plane() {}

	Plane(const Vec3& normal, float d)
		: normal(normal.x, normal.y, normal.z)
		, d(d)
	{
	}

	explicit Plane(const Vec4& rhs)
		: normal(rhs.x, rhs.y, rhs.z)
		, d(rhs.w)
	{
	}

	Plane(const Vec3& point, const Vec3& normal)
		: normal(normal.x, normal.y, normal.z)
		, d(-dotProduct(point, normal))
	{
	}

	void set(const Vec3& normal, float d)
	{
		this->normal = normal;
		this->d = d;
	}

	void set(const Vec3& normal, const Vec3& point)
	{
		this->normal = normal;
		d = -dotProduct(point, normal);
	}

	void set(const Vec4& rhs)
	{
		normal.x = rhs.x;
		normal.y = rhs.y;
		normal.z = rhs.z;
		d = rhs.w;
	}

	Vec3 getNormal() const { return normal; }

	float getD() const { return d; }

	float distance(const Vec3& point) const { return dotProduct(point, normal) + d; }

	bool getIntersectionWithLine(const Vec3& line_point,
		const Vec3& line_vect,
		Vec3& out_intersection) const
	{
		float t2 = dotProduct(normal, line_vect);

		if (t2 == 0.f) return false;

		float t = -(dotProduct(normal, line_point) + d) / t2;
		out_intersection = line_point + (line_vect * t);
		return true;
	}

	Vec3 normal;
	float d;
};


struct Sphere
{
	Sphere(float x, float y, float z, float radius)
		: m_position(x, y, z)
		, m_radius(radius)
	{
	}

	Sphere(const Vec3& point, float radius)
		: m_position(point)
		, m_radius(radius)
	{
	}

	explicit Sphere(const Vec4& sphere)
		: m_position(sphere.x, sphere.y, sphere.z)
		, m_radius(sphere.w)
	{
	}

	Vec3 m_position;
	float m_radius;
};


class LUMIX_ENGINE_API Frustum
{
public:
	void computeOrtho(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float width,
		float height,
		float near_distance,
		float far_distance);


	void computePerspective(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float fov,
		float ratio,
		float near_distance,
		float far_distance);


	bool intersectNearPlane(const Vec3& center, float radius) const
	{
		float x = center.x;
		float y = center.y;
		float z = center.z;
		const Plane& plane = planes[(uint32)Sides::NEAR_PLANE];
		float distance = x * plane.normal.x + y * plane.normal.y + z * plane.normal.z + plane.d;
		distance = distance < 0 ? -distance : distance;
		return distance < radius;
	}


	bool isSphereInside(const Vec3& center, float radius) const
	{
		float x = center.x;
		float y = center.y;
		float z = center.z;
		const Plane* plane = &planes[0];
		float distance =
			x * plane[0].normal.x + y * plane[0].normal.y + z * plane[0].normal.z + plane[0].d;
		if (distance < -radius) return false;

		distance =
			x * plane[1].normal.x + y * plane[1].normal.y + z * plane[1].normal.z + plane[1].d;
		if (distance < -radius) return false;

		distance =
			x * plane[2].normal.x + y * plane[2].normal.y + z * plane[2].normal.z + plane[2].d;
		if (distance < -radius) return false;

		distance =
			x * plane[3].normal.x + y * plane[3].normal.y + z * plane[3].normal.z + plane[3].d;
		if (distance < -radius) return false;

		distance =
			x * plane[4].normal.x + y * plane[4].normal.y + z * plane[4].normal.z + plane[4].d;
		if (distance < -radius) return false;

		distance =
			x * plane[5].normal.x + y * plane[5].normal.y + z * plane[5].normal.z + plane[5].d;
		if (distance < -radius) return false;

		return true;
	}

	enum class Sides : uint32
	{
		NEAR_PLANE,
		FAR_PLANE,
		LEFT_PLANE,
		RIGHT_PLANE,
		TOP_PLANE,
		BOTTOM_PLANE,
		COUNT
	};

	Plane planes[(uint32)Sides::COUNT];
	Vec3 center;
	Vec3 position;
	Vec3 direction;
	Vec3 up;
	float fov;
	float ratio;
	float near_distance;
	float far_distance;
	float radius;
};


struct AABB
{
	AABB() {}
	AABB(const Vec3& _min, const Vec3& _max)
		: min(_min)
		, max(_max)
	{
	}


	void set(const Vec3& _min, const Vec3& _max)
	{
		min = _min;
		max = _max;
	}


	void merge(const AABB& rhs)
	{
		addPoint(rhs.min);
		addPoint(rhs.max);
	}


	void addPoint(const Vec3& point)
	{
		min = minCoords(point, min);
		max = maxCoords(point, max);
	}


	bool overlaps(const AABB& aabb)
	{
		if (min.x > aabb.max.x) return false;
		if (min.y > aabb.max.y) return false;
		if (min.z > aabb.max.z) return false;
		if (aabb.min.x > max.x) return false;
		if (aabb.min.x > max.x) return false;
		if (aabb.min.x > max.x) return false;
		return true;
	}


	void transform(const Matrix& matrix)
	{
		Vec3 points[8];
		points[0] = min;
		points[7] = max;
		points[1].set(points[0].x, points[0].y, points[7].z);
		points[2].set(points[0].x, points[7].y, points[0].z);
		points[3].set(points[0].x, points[7].y, points[7].z);
		points[4].set(points[7].x, points[0].y, points[0].z);
		points[5].set(points[7].x, points[0].y, points[7].z);
		points[6].set(points[7].x, points[7].y, points[0].z);

		for (int j = 0; j < 8; ++j)
		{
			points[j] = matrix.multiplyPosition(points[j]);
		}

		Vec3 new_min = points[0];
		Vec3 new_max = points[0];

		for (int j = 0; j < 8; ++j)
		{
			new_min = minCoords(points[j], new_min);
			new_max = maxCoords(points[j], new_max);
		}

		min = new_min;
		max = new_max;
	}

	void getCorners(const Matrix& matrix, Vec3* points) const
	{
		Vec3 p(min.x, min.y, min.z);
		points[0] = matrix.multiplyPosition(p);
		p.set(min.x, min.y, max.z);
		points[1] = matrix.multiplyPosition(p);
		p.set(min.x, max.y, min.z);
		points[2] = matrix.multiplyPosition(p);
		p.set(min.x, max.y, max.z);
		points[3] = matrix.multiplyPosition(p);
		p.set(max.x, min.y, min.z);
		points[4] = matrix.multiplyPosition(p);
		p.set(max.x, min.y, max.z);
		points[5] = matrix.multiplyPosition(p);
		p.set(max.x, max.y, min.z);
		points[6] = matrix.multiplyPosition(p);
		p.set(max.x, max.y, max.z);
		points[7] = matrix.multiplyPosition(p);
	}

	Vec3 minCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(Math::minimum(a.x, b.x), Math::minimum(a.y, b.y), Math::minimum(a.z, b.z));
	}


	Vec3 maxCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(Math::maximum(a.x, b.x), Math::maximum(a.y, b.y), Math::maximum(a.z, b.z));
	}

	Vec3 min;
	Vec3 max;
};


} // namespace Lumix
