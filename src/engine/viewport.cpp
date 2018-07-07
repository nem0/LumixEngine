#include "viewport.h"
#include "engine/geometry.h"
#include "engine/matrix.h"


namespace Lumix
{


Matrix Viewport::getProjection(bool is_homogenous_depth) const
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
			is_homogenous_depth,
			true);
		return mtx;
	}

	mtx.setPerspective(fov, ratio, near, far, is_homogenous_depth, true);
	return mtx;
}


Matrix Viewport::getView() const
{
	Matrix view = rot.toMatrix();
	view.setTranslation(pos);
	view.fastInverse();
	return view;
}


void Viewport::getRay(const Vec2& screen_pos, Vec3& origin, Vec3& dir) const
{
	origin = pos;

	if (w <= 0 || h <= 0) {
		dir = rot.rotate(Vec3(0, 0, 1));
		return;
	}

	const float nx = 2 * (screen_pos.x / w) - 1;
	const float ny = 2 * ((h - screen_pos.y) / h) - 1;

	const Matrix projection_matrix = getProjection(false);

	if (is_ortho) {
		const Vec3 x = rot * Vec3(1, 0, 0);
		const Vec3 y = rot * Vec3(0, 1, 0);
		float ratio = h > 0 ? w / (float)h : 1;
		origin += x * nx * ortho_size * ratio
			+ y * ny * ortho_size;
	}

	const Matrix view_matrix = getView();
	Matrix inverted = (projection_matrix * view_matrix);
	inverted.inverse();

	Vec4 p0 = inverted * Vec4(nx, ny, -1, 1);
	Vec4 p1 = inverted * Vec4(nx, ny, 1, 1);
	p0 *= 1 / p0.w;
	p1 *= 1 / p1.w;
	dir = (p1 - p0).xyz();
	dir.normalize();
}


Vec2 Viewport::worldToScreenPixels(const Vec3& world) const
{
	const Matrix mtx = getView();
	const Vec4 pos = mtx * Vec4(world, 1);
	const float inv = 1 / pos.w;
	const Vec2 screen_size((float)w, (float)h);
	const Vec2 screen_pos = { 0.5f * pos.x * inv + 0.5f, 1.0f - (0.5f * pos.y * inv + 0.5f) };
	return screen_pos * screen_size;
}


Frustum Viewport::getFrustum() const
{
	Frustum ret;
	const float ratio = h > 0 ? w / (float)h : 1;
	if (is_ortho) {
		ret.computeOrtho(pos,
			rot * Vec3(0, 0, 1),
			rot * Vec3(0, 1, 0),
			ortho_size * ratio,
			ortho_size,
			near,
			far);
		return ret;
	}

	ret.computePerspective(pos,
		rot * Vec3(0, 0, -1),
		rot * Vec3(0, 1, 0),
		fov,
		ratio,
		near,
		far);
	return ret;
}


} // namespace Lumix