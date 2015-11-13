#include "frustum.h"
#include <cmath>


namespace Lumix
{


void Frustum::computeOrtho(const Vec3& position,
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


void Frustum::computePerspective(const Vec3& position,
	const Vec3& direction,
	const Vec3& up,
	float fov,
	float ratio,
	float near_distance,
	float far_distance)
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


} // namespace Lumix
