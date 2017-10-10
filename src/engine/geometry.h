#pragma once

#include "engine/lumix.h"
#include "engine/vec.h"


namespace Lumix
{


struct AABB;
struct Matrix;


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

	void computeOrtho(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float width,
		float height,
		float near_distance,
		float far_distance,
		const Vec2& viewport_min,
		const Vec2& viewport_max);

	void computePerspective(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float fov,
		float ratio,
		float near_distance,
		float far_distance);


	void computePerspective(const Vec3& position,
		const Vec3& direction,
		const Vec3& up,
		float fov,
		float ratio,
		float near_distance,
		float far_distance,
		const Vec2& viewport_min,
		const Vec2& viewport_max);


	bool intersectNearPlane(const Vec3& center, float radius) const
	{
		float x = center.x;
		float y = center.y;
		float z = center.z;
		u32 i = (u32)Planes::NEAR;
		float distance = xs[i] * x + ys[i] * y + z * zs[i] + ds[i];
		distance = distance < 0 ? -distance : distance;
		return distance < radius;
	}


	bool intersectAABB(const AABB& aabb) const;
	bool isSphereInside(const Vec3& center, float radius) const;
	Sphere computeBoundingSphere();
	void transform(const Matrix& mtx);


	enum class Planes : u32
	{
		NEAR,
		FAR,
		LEFT,
		RIGHT,
		TOP,
		BOTTOM,
		EXTRA0,
		EXTRA1,
		COUNT
	};


	Vec3 getNormal(Planes side) const
	{
		return Vec3(xs[(int)side], ys[(int)side], zs[(int)side]);
	}


	void setPlane(Planes side, const Vec3& normal, const Vec3& point);
	void setPlane(Planes side, const Vec3& normal, float d);


	float xs[(int)Planes::COUNT];
	float ys[(int)Planes::COUNT];
	float zs[(int)Planes::COUNT];
	float ds[(int)Planes::COUNT];

	Vec3 points[8];
} LUMIX_ALIGN_END(16);


struct LUMIX_ENGINE_API AABB
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
		if (aabb.min.y > max.y) return false;
		if (aabb.min.z > max.z) return false;
		return true;
	}


	void transform(const Matrix& matrix);
	void getCorners(const Matrix& matrix, Vec3* points) const;
	Vec3 minCoords(const Vec3& a, const Vec3& b);
	Vec3 maxCoords(const Vec3& a, const Vec3& b);


	Vec3 min;
	Vec3 max;
};


} // namespace Lumix
