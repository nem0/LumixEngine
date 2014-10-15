#pragma once

#include "core/lumix.h"
#include "core/plane.h"
#include "core/math_utils.h"

namespace Lumix
{
	struct Frustum
	{
		inline Frustum() {}

		inline void compute(const Vec3& position, const Vec3& direction, const Vec3& up, float fov, float ratio, float near_distance, float far_distance)
		{
			float tang = (float)tan(Math::PI / 180.0f * fov * 0.5f);
			float near_height = near_distance * tang;
			float near_width = near_height * ratio;

			Vec3 z = direction;
			z.normalize();

			Vec3 x = crossProduct(up, z);
			x.normalize();

			Vec3 y = crossProduct(z, x);

			Vec3 near_center = position - z * near_distance;
			Vec3 far_center = position - z * far_distance;
			m_center = position - z * ((near_distance + far_distance)* 0.5f);

			m_plane[(uint32_t)Sides::NEAR_PLANE].set(-z, near_center);
			m_plane[(uint32_t)Sides::FAR_PLANE].set(z, far_center);

			Vec3 aux = (near_center + y * near_height) - position;
			aux.normalize();
			Vec3 normal = crossProduct(aux, x);
			m_plane[(uint32_t)Sides::TOP_PLANE].set(normal, near_center + y * near_height);

			aux = (near_center - y * near_height) - position;
			aux.normalize();
			normal = crossProduct(x, aux);
			m_plane[(uint32_t)Sides::BOTTOM_PLANE].set(normal, near_center - y * near_height);

			aux = (near_center - x * near_width) - position;
			aux.normalize();
			normal = crossProduct(aux, y);
			m_plane[(uint32_t)Sides::LEFT_PLANE].set(normal, near_center - x * near_width);

			aux = (near_center + x * near_width) - position;
			aux.normalize();
			normal = crossProduct(y, aux);
			m_plane[(uint32_t)Sides::RIGHT_PLANE].set(normal, near_center + x * near_width);

			float far_height = far_distance * tang;
			float far_width = far_height * ratio;

			Vec3 corner1 = near_center + x * near_width + y * near_height;
			Vec3 corner2 = far_center + x * far_width + y * far_height;

			float size = (corner1 - corner2).length();
			size = Math::maxValue(sqrt(far_width * far_width * 4 + far_height * far_height * 4), size);
			m_size = size;
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

		enum class Sides : uint32_t	{ NEAR_PLANE, FAR_PLANE, LEFT_PLANE, RIGHT_PLANE, TOP_PLANE, BOTTOM_PLANE, COUNT };
		Plane m_plane[(uint32_t)Sides::COUNT];
		Vec3 m_center;
		float m_size;
	};
}