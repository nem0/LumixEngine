#pragma once

#include "vec.h"

namespace Lumix
{
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
}