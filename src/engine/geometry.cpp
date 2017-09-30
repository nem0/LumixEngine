#include "geometry.h"
#include "engine/math_utils.h"
#include "engine/matrix.h"
#include "engine/simd.h"
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


bool Frustum::intersectAABB(const AABB& aabb) const
{
	Vec3 box[] = { aabb.min, aabb.max };

	for (int i = 0; i < 6; ++i)
	{
		int px = (int)(xs[i] > 0.0f);
		int py = (int)(ys[i] > 0.0f);
		int pz = (int)(zs[i] > 0.0f);

		float dp =
			(xs[i] * box[px].x) +
			(ys[i] * box[py].y) +
			(zs[i] * box[pz].z);

		if (dp < -ds[i]) { return false; }
	}
	return true;
}



bool Frustum::isSphereInside(const Vec3& center, float radius) const
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
	if (f4MoveMask(t)) return false;

	px = f4Load(&xs[4]);
	py = f4Load(&ys[4]);
	pz = f4Load(&zs[4]);
	pd = f4Load(&ds[4]);

	t = f4Mul(cx, px);
	t = f4Add(t, f4Mul(cy, py));
	t = f4Add(t, f4Mul(cz, pz));
	t = f4Add(t, pd);
	t = f4Sub(t, f4Splat(-radius));
	if (f4MoveMask(t)) return false;

	return true;
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
	this->width = width;
	this->ratio = width / height;
}


void Frustum::setPlane(Planes side, const Vec3& normal, const Vec3& point)
{
	xs[(u32)side] = normal.x;
	ys[(u32)side] = normal.y;
	zs[(u32)side] = normal.z;
	ds[(u32)side] = -dotProduct(point, normal);
}


void Frustum::setPlane(Planes side, const Vec3& normal, float d)
{
	xs[(u32)side] = normal.x;
	ys[(u32)side] = normal.y;
	zs[(u32)side] = normal.z;
	ds[(u32)side] = d;
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


void AABB::transform(const Matrix& matrix)
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

void AABB::getCorners(const Matrix& matrix, Vec3* points) const
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


Vec3 AABB::minCoords(const Vec3& a, const Vec3& b)
{
	return Vec3(Math::minimum(a.x, b.x), Math::minimum(a.y, b.y), Math::minimum(a.z, b.z));
}


Vec3 AABB::maxCoords(const Vec3& a, const Vec3& b)
{
	return Vec3(Math::maximum(a.x, b.x), Math::maximum(a.y, b.y), Math::maximum(a.z, b.z));
}


} // namespace Lumix
