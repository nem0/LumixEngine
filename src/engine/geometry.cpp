#include "geometry.h"
#include <cmath>


namespace Lumix
{


Frustum::Frustum()
{
	xs[6] = xs[7] = 1;
	ys[6] = ys[7] = 0;
	zs[6] = zs[7] = 0;
	ds[6] = ds[7] = 0;
}


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

	setPlane(Planes::NEAR, -z, near_center);
	setPlane(Planes::FAR, z, far_center);
	setPlane(Planes::EXTRA0, -z, near_center);
	setPlane(Planes::EXTRA1, z, far_center);

	setPlane(Planes::TOP, -y, near_center + y * height);
	setPlane(Planes::BOTTOM, y, near_center - y * height);

	setPlane(Planes::LEFT, x, near_center - x * width);
	setPlane(Planes::RIGHT, -x, near_center + x * width);

	center = (near_center + far_center) * 0.5f;
	float z_diff = far_distance - near_distance;
	radius = std::sqrt(4 * width * width + 4 * height * height + z_diff * z_diff) * 0.5f;
	this->position = position;
	this->fov = -1;
	this->direction = direction;
	this->up = up;
	this->near_distance = near_distance;
	this->far_distance = far_distance;
}


void Frustum::setPlane(Planes side, const Vec3& normal, const Vec3& point)
{
	xs[(uint32)side] = normal.x;
	ys[(uint32)side] = normal.y;
	zs[(uint32)side] = normal.z;
	ds[(uint32)side] = -dotProduct(point, normal);
}


void Frustum::setPlane(Planes side, const Vec3& normal, float d)
{
	xs[(uint32)side] = normal.x;
	ys[(uint32)side] = normal.y;
	zs[(uint32)side] = normal.z;
	ds[(uint32)side] = d;
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

	Vec3 x = crossProduct(z, up);
	x.normalize();

	Vec3 y = crossProduct(x, z);

	Vec3 near_center = position + z * near_distance;
	Vec3 far_center = position + z * far_distance;
	center = position + z * ((near_distance + far_distance)* 0.5f);

	setPlane(Planes::NEAR, z, near_center);
	setPlane(Planes::FAR, -z, far_center);
	setPlane(Planes::EXTRA0, z, near_center);
	setPlane(Planes::EXTRA1, -z, far_center);

	Vec3 aux = (near_center + y * near_height) - position;
	aux.normalize();
	Vec3 normal = crossProduct(aux, x);
	setPlane(Planes::TOP, normal, near_center + y * near_height);

	aux = (near_center - y * near_height) - position;
	aux.normalize();
	normal = crossProduct(x, aux);
	setPlane(Planes::BOTTOM, normal, near_center - y * near_height);

	aux = (near_center - x * near_width) - position;
	aux.normalize();
	normal = crossProduct(aux, y);
	setPlane(Planes::LEFT, normal, near_center - x * near_width);

	aux = (near_center + x * near_width) - position;
	aux.normalize();
	normal = crossProduct(y, aux);
	setPlane(Planes::RIGHT, normal, near_center + x * near_width);

	float far_height = far_distance * tang;
	float far_width = far_height * ratio;

	Vec3 corner1 = near_center + x * near_width + y * near_height;
	Vec3 corner2 = far_center - x * far_width - y * far_height;

	float size = (corner1 - corner2).length();
	size = Math::maximum(std::sqrt(far_width * far_width * 4 + far_height * far_height * 4), size);
	this->radius = size * 0.5f;
	this->position = position;
	this->direction = direction;
	this->up = up;
	this->fov = fov;
	this->ratio = ratio;
	this->near_distance = near_distance;
	this->far_distance = far_distance;
}


} // namespace Lumix
