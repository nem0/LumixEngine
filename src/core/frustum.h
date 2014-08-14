#pragma once

#include "core/lumix.h"
#include "core/plane.h"
#include "core/math_utils.h"

namespace Lumix
{
	struct Frustum
	{
		inline Frustum() {}

		inline void compute(const Vec3& position, const Vec3& direction, const Vec3& up, float fov, float ratio, float near, float Far)
		{
			float tang = (float)tan(Math::PI / 180.0f * fov * 0.5f);
			float nh = near * tang;
			float nw = nh * ratio;

			Vec3 z = direction;
			z.normalize();

			Vec3 x = crossProduct(up, z);
			x.normalize();

			Vec3 y = crossProduct(z, x);

			Vec3 nc = position - z * near;
			Vec3 fc = position - z * Far;

			m_plane[(uint32_t)Sides::Near].set(-z, nc);
			m_plane[(uint32_t)Sides::Far].set(z, fc);

			Vec3 aux = (nc + y * nh) - position;
			aux.normalize();
			Vec3 normal = crossProduct(aux, x);
			m_plane[(uint32_t)Sides::Top].set(normal, nc + y * nh);

			aux = (nc - y * nh) - position;
			aux.normalize();
			normal = crossProduct(x, aux);
			m_plane[(uint32_t)Sides::Bottom].set(normal, nc - y * nh);

			aux = (nc - x * nw) - position;
			aux.normalize();
			normal = crossProduct(aux, y);
			m_plane[(uint32_t)Sides::Left].set(normal, nc - x * nw);

			aux = (nc + x * nw) - position;
			aux.normalize();
			normal = crossProduct(y, aux);
			m_plane[(uint32_t)Sides::Right].set(normal, nc + x * nw);
		}

		bool sphereInFrustum(const Vec3 &p, float radius) const
		{
			for (int i = 0; i < (uint32_t)Sides::COUNT; i++) 
			{
				float distance = m_plane[i].distance(p);
				if (distance < -radius)
					return false;
				else if (distance < radius)
					return true;
			}

			return true;
		}

		enum class Sides : uint32_t	{ Near, Far, Left, Right, Top, Bottom, COUNT };
		Plane m_plane[(uint32_t)Sides::COUNT];
	};
}