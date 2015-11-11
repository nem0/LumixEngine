#pragma once

#include "lumix.h"
#include "core/plane.h"
#include "core/math_utils.h"

namespace Lumix
{
	class Frustum
	{
		public:
			void computeOrtho(const Vec3& position,
							  const Vec3& direction,
							  const Vec3& up,
							  float width,
							  float height,
							  float near_distance,
							  float far_distance)
			{
				Vec3 z = direction;
				z.normalize();
				Vec3 near_center = position - z * near_distance;
				Vec3 far_center = position - z * far_distance;

				Vec3 x = crossProduct(up, z);
				x.normalize();
				Vec3 y = crossProduct(z, x);

				m_plane[(uint32)Sides::NEAR_PLANE].set(-z, near_center);
				m_plane[(uint32)Sides::FAR_PLANE].set(z, far_center);
			
				m_plane[(uint32)Sides::TOP_PLANE].set(-y, near_center + y * (height * 0.5f));
				m_plane[(uint32)Sides::BOTTOM_PLANE].set(y, near_center - y * (height * 0.5f));
			
				m_plane[(uint32)Sides::LEFT_PLANE].set(x, near_center - x * (width * 0.5f));
				m_plane[(uint32)Sides::RIGHT_PLANE].set(-x, near_center + x * (width * 0.5f));

				m_center = (near_center + far_center) * 0.5f;
				float z_diff = far_distance - near_distance;
				m_radius = sqrt(width * width + height * height + z_diff * z_diff) * 0.5f;
				m_position = position;
			}
		

			void computePerspective(const Vec3& position, const Vec3& direction, const Vec3& up, float fov, float ratio, float near_distance, float far_distance)
			{
				ASSERT(near_distance > 0);
				ASSERT(far_distance > 0);
				ASSERT(near_distance < far_distance);
				ASSERT(fov > 0);
				ASSERT(ratio > 0);
				float tang = (float)tan(fov * 0.5f);
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

				m_plane[(uint32)Sides::NEAR_PLANE].set(-z, near_center);
				m_plane[(uint32)Sides::FAR_PLANE].set(z, far_center);

				Vec3 aux = (near_center + y * near_height) - position;
				aux.normalize();
				Vec3 normal = crossProduct(aux, x);
				m_plane[(uint32)Sides::TOP_PLANE].set(normal, near_center + y * near_height);

				aux = (near_center - y * near_height) - position;
				aux.normalize();
				normal = crossProduct(x, aux);
				m_plane[(uint32)Sides::BOTTOM_PLANE].set(normal, near_center - y * near_height);

				aux = (near_center - x * near_width) - position;
				aux.normalize();
				normal = crossProduct(aux, y);
				m_plane[(uint32)Sides::LEFT_PLANE].set(normal, near_center - x * near_width);

				aux = (near_center + x * near_width) - position;
				aux.normalize();
				normal = crossProduct(y, aux);
				m_plane[(uint32)Sides::RIGHT_PLANE].set(normal, near_center + x * near_width);

				float far_height = far_distance * tang;
				float far_width = far_height * ratio;

				Vec3 corner1 = near_center + x * near_width + y * near_height;
				Vec3 corner2 = far_center + x * far_width + y * far_height;

				float size = (corner1 - corner2).length();
				size = Math::maxValue(sqrt(far_width * far_width * 4 + far_height * far_height * 4), size);
				m_radius = size * 0.5f;
				m_position = position;
				m_direction = direction;
				m_up = up;
				m_fov = fov;
				m_ratio = ratio;
				m_near_distance = near_distance;
				m_far_distance = far_distance;
			}


			bool isSphereInside(const Vec3 &center, float radius) const
			{
				float x = center.x;
				float y = center.y;
				float z = center.z;
				const Plane* plane = &m_plane[0];
				float distance = x * plane[0].normal.x + y * plane[0].normal.y + z * plane[0].normal.z + plane[0].d;
				if (distance < -radius)	return false;
				
				distance = x * plane[1].normal.x + y * plane[1].normal.y + z * plane[1].normal.z + plane[1].d;
				if (distance < -radius)	return false;

				distance = x * plane[2].normal.x + y * plane[2].normal.y + z * plane[2].normal.z + plane[2].d;
				if (distance < -radius)	return false;

				distance = x * plane[3].normal.x + y * plane[3].normal.y + z * plane[3].normal.z + plane[3].d;
				if (distance < -radius)	return false;

				distance = x * plane[4].normal.x + y * plane[4].normal.y + z * plane[4].normal.z + plane[4].d;
				if (distance < -radius)	return false;

				distance = x * plane[5].normal.x + y * plane[5].normal.y + z * plane[5].normal.z + plane[5].d;
				if (distance < -radius)	return false;

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
			enum class Sides : uint32	{ NEAR_PLANE, FAR_PLANE, LEFT_PLANE, RIGHT_PLANE, TOP_PLANE, BOTTOM_PLANE, COUNT };
		
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
}