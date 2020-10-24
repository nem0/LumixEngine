#include "geometry.h"
#include "engine/crt.h"
#include "engine/math.h"
#include "engine/simd.h"


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


bool ShiftedFrustum::containsAABB(const DVec3& pos, const Vec3& size) const
{
	const Vec3 rel_pos = (pos - origin).toFloat();
	Vec3 box[] = { rel_pos, rel_pos + size };

	for (int i = 0; i < 6; ++i)
	{
		int px = int(xs[i] < 0.0f);
		int py = int(ys[i] < 0.0f);
		int pz = int(zs[i] < 0.0f);

		float dp =
			(xs[i] * box[px].x) +
			(ys[i] * box[py].y) +
			(zs[i] * box[pz].z);

		if (dp < -ds[i]) { return false; }
	}
	return true;
}


Frustum ShiftedFrustum::getRelative(const DVec3& origin) const
{
	Frustum res;
	const Vec3 offset = (this->origin - origin).toFloat();
	memcpy(res.points, points, sizeof(points));
	for(Vec3& p : res.points) {
		p += offset;
	}
	res.setPlanesFromPoints();
	return res;
}


bool ShiftedFrustum::intersectsAABB(const DVec3& pos, const Vec3& size) const
{
	const Vec3 rel_pos = (pos - origin).toFloat();
	Vec3 box[] = { rel_pos, rel_pos + size };

	for (int i = 0; i < 6; ++i)
	{
		int px = int(xs[i] > 0.0f);
		int py = int(ys[i] > 0.0f);
		int pz = int(zs[i] > 0.0f);

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

	for (u32 i = 0; i < lengthOf(xs); ++i)
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

Frustum Frustum::transformed(const Matrix& mtx) const
{
	Frustum res;
	for (u32 i = 0; i < lengthOf(points); ++i) {
		res.points[i] = mtx.transformPoint(points[i]);
	}

	for (u32 i = 0; i < lengthOf(xs); ++i) {
		Vec3 p;
		if (xs[i] != 0) p.set(-ds[i] / xs[i], 0, 0);
		else if (ys[i] != 0) p.set(0, -ds[i] / ys[i], 0);
		else p.set(0, 0, -ds[i] / zs[i]);

		Vec3 n = { xs[i], ys[i], zs[i] };
		n = mtx.transformVector(n);
		p = mtx.transformPoint(p);

		res.xs[i] = n.x;
		res.ys[i] = n.y;
		res.zs[i] = n.z;
		res.ds[i] = -dotProduct(p, n);
	}
	return res;
}

Sphere Frustum::computeBoundingSphere() const 
{
	Sphere sphere;
	sphere.position = points[0];
	for (u32 i = 1; i < lengthOf(points); ++i)
	{
		sphere.position += points[i];
	}
	sphere.position *= 1.0f / lengthOf(points);

	sphere.radius = 0;
	for (u32 i = 0; i < lengthOf(points); ++i)
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
	
	return f4MoveMask(t) == 0;
}


void Frustum::computeOrtho(const Vec3& position,
	const Vec3& direction,
	const Vec3& up,
	float width,
	float height,
	float near_distance,
	float far_distance)
{
	computeOrtho(position, direction, up, width, height, near_distance, far_distance, {-1, -1}, {1, 1});
}


void ShiftedFrustum::computeOrtho(const DVec3& position,
	const Vec3& direction,
	const Vec3& up,
	float width,
	float height,
	float near_distance,
	float far_distance)
{
	computeOrtho(position, direction, up, width, height, near_distance, far_distance, {-1, -1}, {1, 1});
}

void Frustum::setPlanesFromPoints()
{
	Vec3 normal_near = -crossProduct(points[0] - points[1], points[0] - points[2]).normalized();
	Vec3 normal_far = crossProduct(points[4] - points[5], points[4] - points[6]).normalized();
	setPlane(Frustum::Planes::EXTRA0, normal_near, points[0]);
	setPlane(Frustum::Planes::EXTRA1, normal_near, points[0]);
	setPlane(Frustum::Planes::NEAR, normal_near, points[0]);
	setPlane(Frustum::Planes::FAR, normal_far, points[4]);

	setPlane(Frustum::Planes::LEFT, crossProduct(points[1] - points[2], points[1] - points[5]).normalized(), points[1]);
	setPlane(Frustum::Planes::RIGHT, -crossProduct(points[0] - points[3], points[0] - points[4]).normalized(), points[0]);
	setPlane(Frustum::Planes::TOP, crossProduct(points[0] - points[1], points[0] - points[4]).normalized(), points[0]);
	setPlane(Frustum::Planes::BOTTOM, crossProduct(points[2] - points[3], points[2] - points[6]).normalized(), points[2]);
}


void ShiftedFrustum::setPlanesFromPoints()
{
	Vec3 normal_near = -crossProduct(points[0] - points[1], points[0] - points[2]).normalized();
	Vec3 normal_far = crossProduct(points[4] - points[5], points[4] - points[6]).normalized();
	setPlane(Frustum::Planes::EXTRA0, normal_near, points[0]);
	setPlane(Frustum::Planes::EXTRA1, normal_near, points[0]);
	setPlane(Frustum::Planes::NEAR, normal_near, points[0]);
	setPlane(Frustum::Planes::FAR, normal_far, points[4]);

	setPlane(Frustum::Planes::LEFT, crossProduct(points[1] - points[2], points[1] - points[5]).normalized(), points[1]);
	setPlane(Frustum::Planes::RIGHT, -crossProduct(points[0] - points[3], points[0] - points[4]).normalized(), points[0]);
	setPlane(Frustum::Planes::TOP, crossProduct(points[0] - points[1], points[0] - points[4]).normalized(), points[0]);
	setPlane(Frustum::Planes::BOTTOM, crossProduct(points[2] - points[3], points[2] - points[6]).normalized(), points[2]);
}

template <typename T>
static void setPoints(T& frustum
	, const Vec3& near_center
	, const Vec3& far_center
	, const Vec3& right_near
	, const Vec3& up_near
	, const Vec3& right_far
	, const Vec3& up_far
	, const Vec2& viewport_min
	, const Vec2& viewport_max)
{
	ASSERT(viewport_max.x >= viewport_min.x);
	ASSERT(viewport_max.y >= viewport_min.y);

	Vec3* points = frustum.points;

	points[0] = near_center + right_near * viewport_max.x + up_near * viewport_max.y;
	points[1] = near_center + right_near * viewport_min.x + up_near * viewport_max.y;
	points[2] = near_center + right_near * viewport_min.x + up_near * viewport_min.y;
	points[3] = near_center + right_near * viewport_max.x + up_near * viewport_min.y;

	points[4] = far_center + right_far * viewport_max.x + up_far * viewport_max.y;
	points[5] = far_center + right_far * viewport_min.x + up_far * viewport_max.y;
	points[6] = far_center + right_far * viewport_min.x + up_far * viewport_min.y;
	points[7] = far_center + right_far * viewport_max.x + up_far * viewport_min.y;

	frustum.setPlanesFromPoints();
}


void Frustum::computeOrtho(const Vec3& position,
	const Vec3& direction,
	const Vec3& up,
	float width,
	float height,
	float near_distance,
	float far_distance,
	const Vec2& viewport_min,
	const Vec2& viewport_max)
{
	Vec3 z = direction;
	z.normalize();
	Vec3 near_center = position - z * near_distance;
	Vec3 far_center = position - z * far_distance;

	Vec3 x = crossProduct(up, z).normalized() * width;
	Vec3 y = crossProduct(z, x).normalized() * height;

	setPoints(*this, near_center, far_center, x, y, x, y, viewport_min, viewport_max);
}


void ShiftedFrustum::computeOrtho(const DVec3& position,
	const Vec3& direction,
	const Vec3& up,
	float width,
	float height,
	float near_distance,
	float far_distance,
	const Vec2& viewport_min,
	const Vec2& viewport_max)
{
	Vec3 z = direction;
	z.normalize();
	origin = position;
	Vec3 near_center = - z * near_distance;
	Vec3 far_center = - z * far_distance;

	Vec3 x = crossProduct(up, z).normalized() * width;
	Vec3 y = crossProduct(z, x).normalized() * height;

	setPoints(*this, near_center, far_center, x, y, x, y, viewport_min, viewport_max);
}


void Frustum::setPlane(Planes side, const Vec3& normal, const Vec3& point)
{
	xs[(u32)side] = normal.x;
	ys[(u32)side] = normal.y;
	zs[(u32)side] = normal.z;
	ds[(u32)side] = -dotProduct(point, normal);
}


void ShiftedFrustum::setPlane(Frustum::Planes side, const Vec3& normal, const Vec3& point)
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
	float scale = (float)tan(fov * 0.5f);
	Vec3 right = crossProduct(direction, up);
	Vec3 up_near = up * near_distance * scale;
	Vec3 right_near = right * (near_distance * scale * ratio);
	Vec3 up_far = up * far_distance * scale;
	Vec3 right_far = right * (far_distance * scale * ratio);

	Vec3 z = direction.normalized();

	Vec3 near_center = position + z * near_distance;
	Vec3 far_center = position + z * far_distance;

	setPoints(*this, near_center, far_center, right_near, up_near, right_far, up_far, viewport_min, viewport_max);
}


void ShiftedFrustum::computePerspective(const DVec3& position,
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
	const float scale = (float)tan(fov * 0.5f);
	const Vec3 right = crossProduct(direction, up);
	const Vec3 up_near = up * near_distance * scale;
	const Vec3 right_near = right * (near_distance * scale * ratio);
	const Vec3 up_far = up * far_distance * scale;
	const Vec3 right_far = right * (far_distance * scale * ratio);

	const Vec3 z = direction.normalized();

	const Vec3 near_center = z * near_distance;
	const Vec3 far_center = z * far_distance;
	origin = position;

	setPoints(*this, near_center, far_center, right_near, up_near, right_far, up_far, viewport_min, viewport_max);
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

void ShiftedFrustum::computePerspective(const DVec3& position,
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

void AABB::getCorners(const Transform& tr, DVec3* points) const
{
	DVec3 p(min.x, min.y, min.z);
	points[0] = tr.transform(p);
	p = DVec3(min.x, min.y, max.z);
	points[1] = tr.transform(p);
	p = DVec3(min.x, max.y, min.z);
	points[2] = tr.transform(p);
	p = DVec3(min.x, max.y, max.z);
	points[3] = tr.transform(p);
	p = DVec3(max.x, min.y, min.z);
	points[4] = tr.transform(p);
	p = DVec3(max.x, min.y, max.z);
	points[5] = tr.transform(p);
	p = DVec3(max.x, max.y, min.z);
	points[6] = tr.transform(p);
	p = DVec3(max.x, max.y, max.z);
	points[7] = tr.transform(p);
}

Vec3 AABB::minCoords(const Vec3& a, const Vec3& b)
{
	return Vec3(minimum(a.x, b.x), minimum(a.y, b.y), minimum(a.z, b.z));
}


Vec3 AABB::maxCoords(const Vec3& a, const Vec3& b)
{
	return Vec3(maximum(a.x, b.x), maximum(a.y, b.y), maximum(a.z, b.z));
}


Matrix Viewport::getProjection() const
{
	Matrix mtx;
	const float ratio = h > 0 ? w / (float)h : 1;
	if (is_ortho) {
		mtx.setOrtho(-ortho_size * ratio,
			ortho_size * ratio,
			-ortho_size,
			ortho_size,
			near,
			far,
			true);
		return mtx;
	}

	mtx.setPerspective(fov, ratio, near, far, true);
	return mtx;
}


Matrix Viewport::getView(const DVec3& origin) const
{
	Matrix view = rot.toMatrix();
	view.setTranslation((pos - origin).toFloat());
	return view.fastInverted();
}


Matrix Viewport::getViewRotation() const
{
	Matrix view = rot.conjugated().toMatrix();
	return view;
}


void Viewport::getRay(const Vec2& screen_pos, DVec3& origin, Vec3& dir) const
{
	origin = pos;

	if (w <= 0 || h <= 0) {
		dir = rot.rotate(Vec3(0, 0, 1));
		return;
	}

	const float nx = 2 * (screen_pos.x / w) - 1;
	const float ny = 2 * ((h - screen_pos.y) / h) - 1;

	const Matrix projection_matrix = getProjection();

	if (is_ortho) {
		const Vec3 x = rot * Vec3(1, 0, 0);
		const Vec3 y = rot * Vec3(0, 1, 0);
		float ratio = h > 0 ? w / (float)h : 1;
		origin += x * nx * ortho_size * ratio
			+ y * ny * ortho_size;
	}

	const Matrix view_matrix = getView(origin);
	const Matrix inverted = (projection_matrix * view_matrix).inverted();

	Vec4 p0 = inverted * Vec4(nx, ny, -1, 1);
	Vec4 p1 = inverted * Vec4(nx, ny, 1, 1);
	p0 *= 1 / p0.w;
	p1 *= 1 / p1.w;
	dir = (p1 - p0).xyz();
	dir.normalize();
	if (is_ortho) dir *= -1.f;
}


Vec2 Viewport::worldToScreenPixels(const DVec3& world) const
{
	const Matrix mtx = getProjection() * getView(world);
	const Vec4 pos = mtx * Vec4(0, 0, 0, 1);
	const float inv = 1 / pos.w;
	const Vec2 screen_size((float)w, (float)h);
	const Vec2 screen_pos = { 0.5f * pos.x * inv + 0.5f, 1.0f - (0.5f * pos.y * inv + 0.5f) };
	return screen_pos * screen_size;
}


ShiftedFrustum Viewport::getFrustum(const Vec2& viewport_min_px, const Vec2& viewport_max_px) const
{
	const Matrix mtx = rot.toMatrix();
	ShiftedFrustum ret;
	const float ratio = h > 0 ? w / (float)h : 1;
	const Vec2 viewport_min = { viewport_min_px.x / w * 2 - 1, (1 - viewport_max_px.y / h) * 2 - 1 };
	const Vec2 viewport_max = { viewport_max_px.x / w * 2 - 1, (1 - viewport_min_px.y / h) * 2 - 1 };
	if (is_ortho) {
		ret.computeOrtho({0, 0, 0},
			mtx.getZVector(),
			mtx.getYVector(),
			ortho_size * ratio,
			ortho_size,
			near,
			far,
			viewport_min,
			viewport_max);
		ret.origin = pos;
		return ret;
	}
	ret.computePerspective({0, 0, 0},
		-mtx.getZVector(),
		mtx.getYVector(),
		fov,
		ratio,
		near,
		far,
		viewport_min,
		viewport_max);
	ret.origin = pos;
	return ret;
}


ShiftedFrustum Viewport::getFrustum() const
{
	ShiftedFrustum ret;
	const float ratio = h > 0 ? w / (float)h : 1;
	if (is_ortho) {
		ret.computeOrtho({0, 0, 0},
			rot * Vec3(0, 0, 1),
			rot * Vec3(0, 1, 0),
			ortho_size * ratio,
			ortho_size,
			near,
			far);
		ret.origin = pos;
		return ret;
	}

	ret.computePerspective({0, 0, 0},
		rot * Vec3(0, 0, -1),
		rot * Vec3(0, 1, 0),
		fov,
		ratio,
		near,
		far);
	ret.origin = pos;
	return ret;
}


} // namespace Lumix
