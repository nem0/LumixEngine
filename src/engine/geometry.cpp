#include "geometry.h"
#include "engine/crt.h"
#include "engine/math.h"
#include "engine/simd.h"


namespace Lumix
{

Sphere::Sphere() {}

Sphere::Sphere(float x, float y, float z, float _radius)
	: position(x, y, z)
	, radius(_radius)
{
}

Sphere::Sphere(const Vec3& point, float _radius)
	: position(point)
	, radius(_radius)
{
}

Sphere::Sphere(const Vec4& sphere)
	: position(sphere.x, sphere.y, sphere.z)
	, radius(sphere.w)
{
}

Frustum::Frustum()
{
	xs[6] = xs[7] = 1;
	ys[6] = ys[7] = 0;
	zs[6] = zs[7] = 0;
	ds[6] = ds[7] = 0;
}

bool ShiftedFrustum::intersectNearPlane(const DVec3& center, float radius) const {
	const float x = float(center.x - origin.x);
	const float y = float(center.y - origin.y);
	const float z = float(center.z - origin.z);
	const u32 i = (u32)Frustum::Planes::NEAR;
	float distance = xs[i] * x + ys[i] * y + z * zs[i] + ds[i];
	distance = distance < 0 ? -distance : distance;
	return distance < radius;
}

bool Frustum::intersectNearPlane(const Vec3& center, float radius) const {
	float x = center.x;
	float y = center.y;
	float z = center.z;
	u32 i = (u32)Planes::NEAR;
	float distance = xs[i] * x + ys[i] * y + z * zs[i] + ds[i];
	distance = distance < 0 ? -distance : distance;
	return distance < radius;
}

bool Frustum::intersectAABBWithOffset(const AABB& aabb, float size_offset) const {
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

		if (dp < -ds[i] - size_offset) { return false; }
	}
	return true;
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
	const Vec3 rel_pos = Vec3(pos - origin);
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
	const Vec3 offset = Vec3(this->origin - origin);
	memcpy(res.points, points, sizeof(points));
	for(Vec3& p : res.points) {
		p += offset;
	}
	res.setPlanesFromPoints();
	return res;
}


bool ShiftedFrustum::intersectsAABB(const DVec3& pos, const Vec3& size) const
{
	const Vec3 rel_pos = Vec3(pos - origin);
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
		if (xs[i] != 0) p = Vec3(-ds[i] / xs[i], 0, 0);
		else if (ys[i] != 0) p = Vec3(0, -ds[i] / ys[i], 0);
		else p = Vec3(0, 0, -ds[i] / zs[i]);

		Vec3 n = {xs[i], ys[i], zs[i]};
		n = mtx.transformVector(n);
		p = mtx.transformPoint(p);

		xs[i] = n.x;
		ys[i] = n.y;
		zs[i] = n.z;
		ds[i] = -dot(p, n);
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
		if (xs[i] != 0) p = Vec3(-ds[i] / xs[i], 0, 0);
		else if (ys[i] != 0) p = Vec3(0, -ds[i] / ys[i], 0);
		else p = Vec3(0, 0, -ds[i] / zs[i]);

		Vec3 n = { xs[i], ys[i], zs[i] };
		n = mtx.transformVector(n);
		p = mtx.transformPoint(p);

		res.xs[i] = n.x;
		res.ys[i] = n.y;
		res.zs[i] = n.z;
		res.ds[i] = -dot(p, n);
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
		float len_sq = squaredLength(points[i] - sphere.position);
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
	Vec3 normal_near = -normalize(cross(points[0] - points[1], points[0] - points[2]));
	Vec3 normal_far = normalize(cross(points[4] - points[5], points[4] - points[6]));
	setPlane(Frustum::Planes::EXTRA0, normal_near, points[0]);
	setPlane(Frustum::Planes::EXTRA1, normal_near, points[0]);
	setPlane(Frustum::Planes::NEAR, normal_near, points[0]);
	setPlane(Frustum::Planes::FAR, normal_far, points[4]);

	setPlane(Frustum::Planes::LEFT, normalize(cross(points[1] - points[2], points[1] - points[5])), points[1]);
	setPlane(Frustum::Planes::RIGHT, -normalize(cross(points[0] - points[3], points[0] - points[4])), points[0]);
	setPlane(Frustum::Planes::TOP, normalize(cross(points[0] - points[1], points[0] - points[4])), points[0]);
	setPlane(Frustum::Planes::BOTTOM, normalize(cross(points[2] - points[3], points[2] - points[6])), points[2]);
}


void ShiftedFrustum::setPlanesFromPoints()
{
	Vec3 normal_near = -normalize(cross(points[0] - points[1], points[0] - points[2]));
	Vec3 normal_far = normalize(cross(points[4] - points[5], points[4] - points[6]));
	setPlane(Frustum::Planes::EXTRA0, normal_near, points[0]);
	setPlane(Frustum::Planes::EXTRA1, normal_near, points[0]);
	setPlane(Frustum::Planes::NEAR, normal_near, points[0]);
	setPlane(Frustum::Planes::FAR, normal_far, points[4]);

	setPlane(Frustum::Planes::LEFT, normalize(cross(points[1] - points[2], points[1] - points[5])), points[1]);
	setPlane(Frustum::Planes::RIGHT, -normalize(cross(points[0] - points[3], points[0] - points[4])), points[0]);
	setPlane(Frustum::Planes::TOP, normalize(cross(points[0] - points[1], points[0] - points[4])), points[0]);
	setPlane(Frustum::Planes::BOTTOM, normalize(cross(points[2] - points[3], points[2] - points[6])), points[2]);
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
	Vec3 z = normalize(direction);
	Vec3 near_center = position - z * near_distance;
	Vec3 far_center = position - z * far_distance;

	Vec3 x = normalize(cross(up, z)) * width;
	Vec3 y = normalize(cross(z, x)) * height;

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
	Vec3 z = normalize(direction);
	origin = position;
	Vec3 near_center = - z * near_distance;
	Vec3 far_center = - z * far_distance;

	Vec3 x = normalize(cross(up, z)) * width;
	Vec3 y = normalize(cross(z, x)) * height;

	setPoints(*this, near_center, far_center, x, y, x, y, viewport_min, viewport_max);
}


void Frustum::setPlane(Planes side, const Vec3& normal, const Vec3& point)
{
	xs[(u32)side] = normal.x;
	ys[(u32)side] = normal.y;
	zs[(u32)side] = normal.z;
	ds[(u32)side] = -dot(point, normal);
}


void ShiftedFrustum::setPlane(Frustum::Planes side, const Vec3& normal, const Vec3& point)
{
	xs[(u32)side] = normal.x;
	ys[(u32)side] = normal.y;
	zs[(u32)side] = normal.z;
	ds[(u32)side] = -dot(point, normal);
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
	Vec3 right = cross(direction, up);
	Vec3 up_near = up * near_distance * scale;
	Vec3 right_near = right * (near_distance * scale * ratio);
	Vec3 up_far = up * far_distance * scale;
	Vec3 right_far = right * (far_distance * scale * ratio);

	Vec3 z = normalize(direction);

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
	const Vec3 right = cross(direction, up);
	const Vec3 up_near = up * near_distance * scale;
	const Vec3 right_near = right * (near_distance * scale * ratio);
	const Vec3 up_far = up * far_distance * scale;
	const Vec3 right_far = right * (far_distance * scale * ratio);

	const Vec3 z = normalize(direction);

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

AABB::AABB() {}

AABB::AABB(const Vec3& _min, const Vec3& _max)
	: min(_min)
	, max(_max)
{}

void AABB::merge(const AABB& rhs) {
	addPoint(rhs.min);
	addPoint(rhs.max);
}

void AABB::addPoint(const Vec3& point) {
	min = minCoords(point, min);
	max = maxCoords(point, max);
}

bool AABB::contains(const Vec3& point) const {
	if (min.x > point.x) return false;
	if (min.y > point.y) return false;
	if (min.z > point.z) return false;
	if (point.x > max.x) return false;
	if (point.y > max.y) return false;
	if (point.z > max.z) return false;
	return true;
}

bool AABB::overlaps(const AABB& aabb) const {
	if (min.x > aabb.max.x) return false;
	if (min.y > aabb.max.y) return false;
	if (min.z > aabb.max.z) return false;
	if (aabb.min.x > max.x) return false;
	if (aabb.min.y > max.y) return false;
	if (aabb.min.z > max.z) return false;
	return true;
}

AABB AABB::intersection(const AABB& rhs) const {
	return AABB(maximum(rhs.min, min), minimum(rhs.max, max));
}

void AABB::translate(const Vec3& v) {
	min += v;
	max += v;
}

void AABB::transform(const Matrix& matrix)
{
	Vec3 points[8];
	points[0] = min;
	points[7] = max;
	points[1] = Vec3(points[0].x, points[0].y, points[7].z);
	points[2] = Vec3(points[0].x, points[7].y, points[0].z);
	points[3] = Vec3(points[0].x, points[7].y, points[7].z);
	points[4] = Vec3(points[7].x, points[0].y, points[0].z);
	points[5] = Vec3(points[7].x, points[0].y, points[7].z);
	points[6] = Vec3(points[7].x, points[7].y, points[0].z);

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
	p = Vec3(min.x, min.y, max.z);
	points[1] = matrix.transformPoint(p);
	p = Vec3(min.x, max.y, min.z);
	points[2] = matrix.transformPoint(p);
	p = Vec3(min.x, max.y, max.z);
	points[3] = matrix.transformPoint(p);
	p = Vec3(max.x, min.y, min.z);
	points[4] = matrix.transformPoint(p);
	p = Vec3(max.x, min.y, max.z);
	points[5] = matrix.transformPoint(p);
	p = Vec3(max.x, max.y, min.z);
	points[6] = matrix.transformPoint(p);
	p = Vec3(max.x, max.y, max.z);
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

void AABB::shrink(float x) {
	min += Vec3(x);
	max -= Vec3(x);
}

Vec3 AABB::minCoords(const Vec3& a, const Vec3& b)
{
	return Vec3(minimum(a.x, b.x), minimum(a.y, b.y), minimum(a.z, b.z));
}


Vec3 AABB::maxCoords(const Vec3& a, const Vec3& b)
{
	return Vec3(maximum(a.x, b.x), maximum(a.y, b.y), maximum(a.z, b.z));
}


Matrix Viewport::getProjectionWithJitter() const
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
	mtx.columns[2].x = pixel_offset.x;
	mtx.columns[2].y = pixel_offset.y;
	return mtx;
}


Matrix Viewport::getProjectionNoJitter() const
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
	view.setTranslation(Vec3(pos - origin));
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

	const Matrix projection_matrix = getProjectionNoJitter();

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
	dir = normalize((p1 - p0).xyz());
	if (is_ortho) dir *= -1.f;
}


Vec2 Viewport::worldToScreenPixels(const DVec3& world) const
{
	const Matrix mtx = getProjectionNoJitter() * getView(world);
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

Vec4 makePlane(const Vec3& normal, const Vec3& point) {
	ASSERT(squaredLength(normal) < 1.001f);
	ASSERT(squaredLength(normal) > 0.999f);
	return Vec4(normal, -dot(normal, point));
}

float planeDist(const Vec4& plane, const Vec3& point) {
	return plane.x * point.x + plane.y * point.y + plane.z * point.z + plane.w;
}

bool getRayPlaneIntersecion(const Vec3& origin,
	const Vec3& dir,
	const Vec3& plane_point,
	const Vec3& normal,
	float& out)
{
	float d = dot(dir, normal);
	if (d == 0) return false;

	d = dot(plane_point - origin, normal) / d;
	out = d;
	return true;
}

bool getRaySphereIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& center,
	float radius,
	float& out)
{
	ASSERT(length(dir) < 1.01f && length(dir) > 0.99f);
	Vec3 L = center - origin;
	float tca = dot(L, dir);
	float d2 = dot(L, L) - tca * tca;
	if (d2 > radius * radius) return false;
	float thc = sqrtf(radius * radius - d2);
	float t = tca - thc;
	out = t >= 0 ? t : tca + thc;
	return true;
}

bool getRayAABBIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& min,
	const Vec3& size,
	Vec3& out)
{
	Vec3 dirfrac;

	dirfrac.x = 1.0f / (dir.x == 0 ? 0.00000001f : dir.x);
	dirfrac.y = 1.0f / (dir.y == 0 ? 0.00000001f : dir.y);
	dirfrac.z = 1.0f / (dir.z == 0 ? 0.00000001f : dir.z);

	Vec3 max = min + size;
	float t1 = (min.x - origin.x) * dirfrac.x;
	float t2 = (max.x - origin.x) * dirfrac.x;
	float t3 = (min.y - origin.y) * dirfrac.y;
	float t4 = (max.y - origin.y) * dirfrac.y;
	float t5 = (min.z - origin.z) * dirfrac.z;
	float t6 = (max.z - origin.z) * dirfrac.z;

	float tmin = maximum(maximum(minimum(t1, t2), minimum(t3, t4)), minimum(t5, t6));
	float tmax = minimum(minimum(maximum(t1, t2), maximum(t3, t4)), maximum(t5, t6));

	if (tmax < 0) return false;
	if (tmin > tmax) return false;

	out = tmin < 0 ? origin : origin + dir * tmin;
	return true;
}


float getLineSegmentDistance(const Vec3& origin, const Vec3& dir, const Vec3& a, const Vec3& b)
{
	Vec3 a_origin = origin - a;
	Vec3 ab = b - a;

	float dot1 = dot(ab, a_origin);
	float dot2 = dot(ab, dir);
	float dot3 = dot(dir, a_origin);
	float dot4 = dot(ab, ab);
	float dot5 = dot(dir, dir);

	float denom = dot4 * dot5 - dot2 * dot2;
	if (fabsf(denom) < 1e-5f)
	{
		Vec3 X = origin + dir * dot(b - origin, dir);
		return length(b - X);
	}

	float numer = dot1 * dot2 - dot3 * dot4;
	float param_a = numer / denom;
	float param_b = (dot1 + dot2 * param_a) / dot4;

	if (param_b < 0 || param_b > 1)
	{
		param_b = clamp(param_b, 0.0f, 1.0f);
		Vec3 B = a + ab * param_b;
		Vec3 X = origin + dir * dot(b - origin, dir);
		return length(B - X);
	}

	Vec3 vec = (origin + dir * param_a) - (a + ab * param_b);
	return length(vec);
}


bool getRayTriangleIntersection(const Vec3& origin,
	const Vec3& dir,
	const Vec3& p0,
	const Vec3& p1,
	const Vec3& p2,
	float* out_t)
{
	Vec3 normal = cross(p1 - p0, p2 - p0);
	float q = dot(normal, dir);
	if (q == 0) return false;

	float d = -dot(normal, p0);
	float t = -(dot(normal, origin) + d) / q;
	if (t < 0) return false;

	Vec3 hit_point = origin + dir * t;

	Vec3 edge0 = p1 - p0;
	Vec3 VP0 = hit_point - p0;
	if (dot(normal, cross(edge0, VP0)) < 0)
	{
		return false;
	}

	Vec3 edge1 = p2 - p1;
	Vec3 VP1 = hit_point - p1;
	if (dot(normal, cross(edge1, VP1)) < 0)
	{
		return false;
	}

	Vec3 edge2 = p0 - p2;
	Vec3 VP2 = hit_point - p2;
	if (dot(normal, cross(edge2, VP2)) < 0)
	{
		return false;
	}

	if (out_t) *out_t = t;
	return true;
}


LUMIX_ENGINE_API bool getSphereTriangleIntersection(const Vec3& center,
	float radius,
	const Vec3& v0,
	const Vec3& v1,
	const Vec3& v2)
{
	Vec3 normal = normalize(cross(v0 - v1, v2 - v1));
	float D = -dot(v0, normal);

	float dist = dot(center, normal) + D;

	if (fabs(dist) > radius) return false;

	float squared_radius = radius * radius;
	if (squaredLength(v0 - center) < squared_radius) return true;
	if (squaredLength(v1 - center) < squared_radius) return true;
	if (squaredLength(v2 - center) < squared_radius) return true;

	return false;
}

static void getProjections(const Vec3& axis,
	const Vec3 vertices[8],
	float& min,
	float& max)
{
	max = dot(vertices[0], axis);
	min = max;
	for(int i = 1; i < 8; ++i)
	{
		float d = dot(vertices[i], axis);
		min = minimum(d, min);
		max = maximum(d, max);
	}
}

static bool overlaps(float min1, float max1, float min2, float max2)
{
	return (min1 <= min2 && min2 <= max1) || (min2 <= min1 && min1 <= max2);
}

bool testOBBCollision(const AABB& a, const Matrix& mtx_b, const AABB& b)
{
	Vec3 box_a_points[8];
	Vec3 box_b_points[8];

	a.getCorners(Matrix::IDENTITY, box_a_points);
	b.getCorners(mtx_b, box_b_points);

	const Vec3 normals[] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
	for(int i = 0; i < 3; i++)
	{
		float box_a_min, box_a_max, box_b_min, box_b_max;
		getProjections(normals[i], box_a_points, box_a_min, box_a_max);
		getProjections(normals[i], box_b_points, box_b_min, box_b_max);
		if(!overlaps(box_a_min, box_a_max, box_b_min, box_b_max))
		{
			return false;
		}
	}

	Vec3 normals_b[] = {
		normalize(mtx_b.getXVector()), normalize(mtx_b.getYVector()), normalize(mtx_b.getZVector())};
	for(int i = 0; i < 3; i++)
	{
		float box_a_min, box_a_max, box_b_min, box_b_max;
		getProjections(normals_b[i], box_a_points, box_a_min, box_a_max);
		getProjections(normals_b[i], box_b_points, box_b_min, box_b_max);
		if(!overlaps(box_a_min, box_a_max, box_b_min, box_b_max))
		{
			return false;
		}
	}

	return true;
}

} // namespace Lumix
