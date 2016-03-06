#pragma once

#include "lumix.h"
#include "core/vec.h"
#include "core/math_utils.h"


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
	{}

	Sphere(const Vec3& point, float radius)
		: m_position(point)
		, m_radius(radius)
	{}

	explicit Sphere(const Vec4& sphere)
		: m_position(sphere.x, sphere.y, sphere.z)
		, m_radius(sphere.w)
	{}

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
		const Plane& plane = m_plane[(uint32)Sides::NEAR_PLANE];
		float distance = x * plane.normal.x + y * plane.normal.y + z * plane.normal.z + plane.d;
		distance = distance < 0 ? -distance : distance;
		return distance < radius;
	}


	bool isSphereInside(const Vec3& center, float radius) const
	{
		float x = center.x;
		float y = center.y;
		float z = center.z;
		const Plane* plane = &m_plane[0];
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

	const Vec3& getCenter() const { return m_center; }
	const Vec3& getPosition() const { return m_position; }
	const Vec3& getDirection() const { return m_direction; }
	const Vec3& getUp() const { return m_up; }
	float getFOV() const { return m_fov; }
	float getRatio() const { return m_ratio; }
	float getNearDistance() const { return m_near_distance; }
	float getFarDistance() const { return m_far_distance; }
	float getRadius() const { return m_radius; }

private:
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

private:
	Plane m_plane[(uint32)Sides::COUNT];
	Vec3 m_center;
	Vec3 m_position;
	Vec3 m_direction;
	Vec3 m_up;
	float m_fov;
	float m_ratio;
	float m_near_distance;
	float m_far_distance;
	float m_radius;
};


} // namespace Lumix