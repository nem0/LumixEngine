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


void Frustum::transform(const Matrix& mtx)
{
	for (Vec3& p : points)
	{
		p = mtx.transformPoint(p);
	}

	for (int i = 0; i < lengthOf(xs); ++i)
	{
		Vec3 p;
		if (xs[i] != 0) p.set(-ds[i] / xs[i], 0, 0);
		else if (ys[i] != 0) p.set(0, -ds[i] / ys[i], 0);
		else p.set(0, 0, -ds[i] / zs[i]);

		Vec3 n = {xs[i], ys[i], zs[i]};
		n = mtx.transformVector(n);
		p = mtx.transformPoint(p);

		xs[i] = n.x;
		ys[i] = n.y;
		zs[i] = n.z;
		ds[i] = -dotProduct(p, n);
	}
}


Sphere Frustum::computeBoundingSphere()
{
	Sphere sphere;
	sphere.position = points[0];
	for (int i = 1; i < lengthOf(points); ++i)
	{
		sphere.position += points[i];
	}
	sphere.position *= 1.0f / lengthOf(points);

	sphere.radius = 0;
	for (int i = 0; i < lengthOf(points); ++i)
	{
		float len_sq = (points[i] - sphere.position).squaredLength();
		if (len_sq > sphere.radius) sphere.radius = len_sq;
	}
	sphere.radius = sqrtf(sphere.radius);
	return sphere;
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

	points[0] = near_center + x * width + y * height;
	points[1] = near_center + x * width - y * height;
	points[2] = near_center - x * width - y * height;
	points[3] = near_center - x * width + y * height;

	points[4] = far_center + x * width + y * height;
	points[5] = far_center + x * width - y * height;
	points[6] = far_center - x * width - y * height;
	points[7] = far_center - x * width + y * height;
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
	float far_distance,
	const Vec2& viewport_min,
	const Vec2& viewport_max)
{
	ASSERT(near_distance > 0);
	ASSERT(far_distance > 0);
	ASSERT(near_distance < far_distance);
	ASSERT(fov > 0);
	ASSERT(ratio > 0);
	float tang = (float)tan(fov * 0.5f);
	float near_height = near_distance * tang;
	float near_width = near_height * ratio;
	float scale = (float)tan(fov * 0.5f);
	Vec3 right = crossProduct(direction, up);
	Vec3 up_near = up * near_distance * scale;
	Vec3 right_near = right * (near_distance * scale * ratio);
	Vec3 up_far = up * far_distance * scale;
	Vec3 right_far = right * (far_distance * scale * ratio);

	Vec3 z = direction.normalized();

	Vec3 near_center = position + z * near_distance;
	Vec3 far_center = position + z * far_distance;

	points[0] = near_center + right_near * viewport_max.x + up_near * viewport_max.y;
	points[1] = near_center + right_near * viewport_min.x + up_near * viewport_max.y;
	points[2] = near_center + right_near * viewport_min.x + up_near * viewport_min.y;
	points[3] = near_center + right_near * viewport_max.x + up_near * viewport_min.y;

	points[4] = far_center + right_far * viewport_max.x + up_far * viewport_max.y;
	points[5] = far_center + right_far * viewport_min.x + up_far * viewport_max.y;
	points[6] = far_center + right_far * viewport_min.x + up_far * viewport_min.y;
	points[7] = far_center + right_far * viewport_max.x + up_far * viewport_min.y;

	Vec3 normal_near = -crossProduct(points[0] - points[1], points[0] - points[2]).normalized();
	Vec3 normal_far = crossProduct(points[4] - points[5], points[4] - points[6]).normalized();
	setPlane(Planes::EXTRA0, normal_near, points[0]);
	setPlane(Planes::EXTRA1, normal_near, points[0]);
	setPlane(Planes::NEAR, normal_near, points[0]);
	setPlane(Planes::FAR, normal_far, points[4]);

	setPlane(Planes::LEFT, crossProduct(points[1] - points[2], points[1] - points[5]).normalized(), points[1]);
	setPlane(Planes::RIGHT, -crossProduct(points[0] - points[3], points[0] - points[4]).normalized(), points[0]);
	setPlane(Planes::TOP, crossProduct(points[0] - points[1], points[0] - points[4]).normalized(), points[0]);
	setPlane(Planes::BOTTOM, crossProduct(points[2] - points[3], points[2] - points[6]).normalized(), points[2]);

	Sphere sp = computeBoundingSphere();

	for (int i = 0; i < 8; ++i)
	{
		bool is = dotProduct(getNormal((Planes)i), sp.position) + ds[i] < 0;
		is = is;
	}
}


void Frustum::computePerspective(const Vec3& position,
	const Vec3& direction,
	const Vec3& up,
	float fov,
	float ratio,
	float near_distance,
	float far_distance)
{
	computePerspective(position, direction, up, fov, ratio, near_distance, far_distance, {-1, -1}, {1, 1});
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
		points[j] = matrix.transformPoint(points[j]);
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
	points[0] = matrix.transformPoint(p);
	p.set(min.x, min.y, max.z);
	points[1] = matrix.transformPoint(p);
	p.set(min.x, max.y, min.z);
	points[2] = matrix.transformPoint(p);
	p.set(min.x, max.y, max.z);
	points[3] = matrix.transformPoint(p);
	p.set(max.x, min.y, min.z);
	points[4] = matrix.transformPoint(p);
	p.set(max.x, min.y, max.z);
	points[5] = matrix.transformPoint(p);
	p.set(max.x, max.y, min.z);
	points[6] = matrix.transformPoint(p);
	p.set(max.x, max.y, max.z);
	points[7] = matrix.transformPoint(p);
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
