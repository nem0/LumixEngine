#pragma once

#include "engine/lumix.h"
#include "engine/math_utils.h"
#include "engine/matrix.h"
#include "engine/simd.h"
#include "engine/vec.h"


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
	Sphere() {}

	Sphere(float x, float y, float z, float _radius)
		: position(x, y, z)
		, radius(_radius)
	{
	}

	Sphere(const Vec3& point, float _radius)
		: position(point)
		, radius(_radius)
	{
	}

	explicit Sphere(const Vec4& sphere)
		: position(sphere.x, sphere.y, sphere.z)
		, radius(sphere.w)
	{
	}

	Vec3 position;
	float radius;
};


LUMIX_ALIGN_BEGIN(16) struct LUMIX_ENGINE_API Frustum
{
	Frustum();

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
		uint32 i = (uint32)Sides::NEAR_PLANE;
		float distance = xs[i] * x + ys[i] * y + z * zs[i] + ds[i];
		distance = distance < 0 ? -distance : distance;
		return distance < radius;
	}


	bool isSphereInside(const Vec3& center, float radius) const
	{
		float4 px = f4Load(xs);
		float4 py = f4Load(ys);
		float4 pz = f4Load(zs);
		float4 pd = f4Load(ds);

		float4 cx = f4Splat(center.x);
		float4 cy = f4Splat(center.y);
		float4 cz = f4Splat(center.z);

		float4 t = f4Mul(cx, px);
		t = f4Add(t, f4Mul(cy, py));
		t = f4Add(t, f4Mul(cz, pz));
		t = f4Add(t, pd);
		t = f4Sub(t, f4Splat(-radius));
		if(f4MoveMask(t)) return false; // TODO wrap _mm

		px = f4Load(&xs[4]);
		py = f4Load(&ys[4]);
		pz = f4Load(&zs[4]);
		pd = f4Load(&ds[4]);

		t = f4Mul(cx, px);
		t = f4Add(t, f4Mul(cy, py));
		t = f4Add(t, f4Mul(cz, pz));
		t = f4Add(t, pd);
		t = f4Sub(t, f4Splat(-radius));
		if (f4MoveMask(t)) return false; // TODO wrap _mm

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
		EXTRA_PLANE0,
		EXTRA_PLANE1,
		COUNT
	};

	void setPlane(Sides side, const Vec3& normal, const Vec3& point);

	float xs[(int)Sides::COUNT];
	float ys[(int)Sides::COUNT];
	float zs[(int)Sides::COUNT];
	float ds[(int)Sides::COUNT];

	Vec3 center;
	Vec3 position;
	Vec3 direction;
	Vec3 up;
	float fov;
	float ratio;
	float near_distance;
	float far_distance;
	float radius;
} LUMIX_ALIGN_END(16);


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
			points[j] = matrix.transform(points[j]);
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
		points[0] = matrix.transform(p);
		p.set(min.x, min.y, max.z);
		points[1] = matrix.transform(p);
		p.set(min.x, max.y, min.z);
		points[2] = matrix.transform(p);
		p.set(min.x, max.y, max.z);
		points[3] = matrix.transform(p);
		p.set(max.x, min.y, min.z);
		points[4] = matrix.transform(p);
		p.set(max.x, min.y, max.z);
		points[5] = matrix.transform(p);
		p.set(max.x, max.y, min.z);
		points[6] = matrix.transform(p);
		p.set(max.x, max.y, max.z);
		points[7] = matrix.transform(p);
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
