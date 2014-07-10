#pragma once

#include "core/lumix.h"
#include "core/plane.h"
#include "core/math_utils.h"

namespace Lumix
{
	struct Frustum
	{
		inline Frustum() {}

		inline void compute(const Vec3& position, const Vec3& direction, const Vec3& up, float fov, float ratio, float near, float far)
		{
			float tang = (float)tan(Math::PI / 180.0f * fov * 0.5f);
			float nh = near * tang;
			float nw = nh * ratio;

			Vec3 Z = position - direction;
			Z.normalize();

			Vec3 X = crossProduct(up, Z);
			X.normalize();

			Vec3 Y = crossProduct(Z, X);

			Vec3 nc = position - Z * near;
			Vec3 fc = position - Z * far;

			m_plane[(uint32_t)Sides::NEAR].set(-Z, nc);
			m_plane[(uint32_t)Sides::FAR].set(Z, fc);

			Vec3 aux = (nc + Y * nh) - position;
			aux.normalize();
			Vec3 normal = crossProduct(aux, X);
			m_plane[(uint32_t)Sides::TOP].set(normal, nc + Y*nh);

			aux = (nc - Y*nh) - position;
			aux.normalize();
			normal = crossProduct(X, aux);
			m_plane[(uint32_t)Sides::BOTTOM].set(normal, nc - Y*nh);

			aux = (nc - X * nw) - position;
			aux.normalize();
			normal = crossProduct(aux, Y);
			m_plane[(uint32_t)Sides::LEFT].set(normal, nc - X*nw);

			aux = (nc + X * nw) - position;
			aux.normalize();
			normal = crossProduct(Y, aux);
			m_plane[(uint32_t)Sides::RIGHT].set(normal, nc + X*nw);
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

		enum class Sides : uint32_t	{ NEAR, FAR, LEFT, RIGHT, TOP, BOTTOM, COUNT };
		Plane m_plane[(uint32_t)Sides::COUNT];
	};
}