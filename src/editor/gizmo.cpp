#include "core/crt.h"
#include "core/geometry.h"
#include "core/math.h"
#include "core/os.h"
#include "core/string.h"

#include "editor/gizmo.h"
#include "engine/world.h"
#include "render_interface.h"


namespace Lumix {

namespace Gizmo {

static const float INFLUENCE_DISTANCE = 0.3f;
static const u32 X_COLOR = 0xff6363cf;
static const u32 Y_COLOR = 0xff63cf63;
static const u32 Z_COLOR = 0xffcf6363;
static const u32 SELECTED_COLOR = 0xff63cfcf;

enum class Axis : u32
{
	NONE,
	X = 1 << 0,
	Y = 1 << 1,
	Z = 1 << 2,
	XY = X | Y,
	XZ = X | Z,
	YZ = Y | Z
};

enum class BoxAxis : u32 {
	XP,
	XN,
	YP,
	YN,
	ZP,
	ZN,

	NONE
};

struct {
	struct {
		Transform start_transform;
		Vec3 start_half_extents;
		DVec3 start_pos;
		BoxAxis axis;
	} box;

	u64 frame = 0;
	u64 last_manipulate_frame = 0;
	u64 dragged_id = ~(u64)0;
	u64 active_id = ~(u64)0;
	Axis axis = Axis::NONE;
	DVec3 prev_point;
	DVec3 start_pos;
	Quat start_rot;
} g_gizmo_state;

struct BoxGizmo {
	Vec3 x, y, z;
	DVec3 pos;
};

struct TranslationGizmo {
	Vec3 x, y, z;
	DVec3 pos;
};

struct RotationGizmo {
	Vec3 x, y, z;
	DVec3 pos;
};

struct ScaleGizmo {
	Vec3 x, y, z;
	DVec3 pos;
};

void renderCube(WorldView& view, u32 color, const Vec3& pos, float scale, const Vec3& x, const Vec3& y, const Vec3& z) {
	WorldView::Vertex* vertices = view.render(false, 36);

	WorldView::Vertex tmp[8];
	for (int i = 0; i < 8; ++i) tmp[i].abgr = color;

	tmp[0].pos = pos + (-x - y - z) * scale;
	tmp[1].pos = pos + (x - y - z) * scale;
	tmp[2].pos = pos + (x - y + z) * scale;
	tmp[3].pos = pos + (-x - y + z) * scale;

	tmp[4].pos = pos + (-x + y - z) * scale;
	tmp[5].pos = pos + (x + y - z) * scale;
	tmp[6].pos = pos + (x + y + z) * scale;
	tmp[7].pos = pos + (-x + y + z) * scale;

	const u16 indices[36] =
	{
		0, 1, 2,
		0, 2, 3,
		4, 6, 5,
		4, 7, 6,
		0, 4, 5,
		0, 5, 1,
		2, 6, 7,
		2, 7, 3,
		0, 3, 7,
		0, 7, 4,
		1, 2, 6,
		1, 6, 5
	};

	for (u32 i = 0; i < lengthOf(indices); ++i) {
		vertices[i] = tmp[indices[i]];
	}
}

float getScale(const Viewport& viewport, const DVec3& pos, float base_scale) {
	if (viewport.is_ortho) return 2;
	float scale = tanf(viewport.fov * 0.5f) * (float)length(pos - viewport.pos) * 2;
	return base_scale * scale / 10;
}

template <typename T>
T getGizmo(WorldView& view, Transform& tr, const Gizmo::Config& cfg)
{
	T gizmo;
	gizmo.pos = tr.pos;

	const float scale = getScale(view.getViewport(), tr.pos, cfg.scale);
	if (cfg.coord_system == Gizmo::Config::GLOBAL) {
		gizmo.x = Vec3(scale, 0, 0);
		gizmo.y = Vec3(0, scale, 0);
		gizmo.z = Vec3(0, 0, scale);
	}
	else {
		gizmo.x = tr.rot.rotate(Vec3(scale, 0, 0));
		gizmo.y = tr.rot.rotate(Vec3(0, scale, 0));
		gizmo.z = tr.rot.rotate(Vec3(0, 0, scale));
	}

	const Vec3 cam_dir = normalize(Vec3(tr.pos - view.getViewport().pos));
	if (dot(cam_dir, gizmo.x) > 0) gizmo.x = -gizmo.x;
	if (dot(cam_dir, gizmo.y) > 0) gizmo.y = -gizmo.y;
	if (dot(cam_dir, gizmo.z) > 0) gizmo.z = -gizmo.z;

	return gizmo;
}

Axis collide(const ScaleGizmo& gizmo, const WorldView& view, const Gizmo::Config& cfg) {
	const Viewport vp = view.getViewport();
	const float scale = getScale(vp, gizmo.pos, cfg.scale);

	const Vec3 pos(gizmo.pos - vp.pos);
	const Vec2 mp = view.getMousePos();
	const Ray ray = vp.getRay(mp);
	const Vec3 rel_origin = Vec3(ray.origin - vp.pos);
	const float x_dist = getLineSegmentDistance(rel_origin, ray.dir, pos, pos + gizmo.x);
	const float y_dist = getLineSegmentDistance(rel_origin, ray.dir, pos, pos + gizmo.y);
	const float z_dist = getLineSegmentDistance(rel_origin, ray.dir, pos, pos + gizmo.z);

	float influenced_dist = scale * INFLUENCE_DISTANCE;

	if (x_dist < y_dist && x_dist < z_dist && x_dist < influenced_dist) return Axis::X;
	if (y_dist < z_dist && y_dist < influenced_dist) return Axis::Y;
	return z_dist < influenced_dist ? Axis::Z : Axis::NONE;
}

Axis collide(const RotationGizmo& gizmo, const WorldView& view, const Gizmo::Config& cfg) { 
	const Viewport vp = view.getViewport();
	const Vec3 pos(gizmo.pos - vp.pos);
	const float scale = getScale(vp, gizmo.pos, cfg.scale);

	const Viewport viewport = view.getViewport();
	const Vec2 mp = view.getMousePos();
	const Ray ray = viewport.getRay(mp);
	const Vec3 rel_origin(ray.origin - vp.pos);

	float t;
	float mint = FLT_MAX;
	float d = FLT_MAX;
	Axis axis = Axis::NONE;
	if (getRayPlaneIntersecion(rel_origin, ray.dir, pos, normalize(gizmo.x), t) && t > 0) {
		const Vec3 p = rel_origin + ray.dir * t;
		mint = t;
		d = length(p - pos);
		axis = Axis::X;
	}

	if (getRayPlaneIntersecion(rel_origin, ray.dir, pos, normalize(gizmo.y), t) && t < mint && t > 0) {
		const Vec3 p = rel_origin + ray.dir * t;
		d = length(p - pos);
		mint = t;
		axis = Axis::Y;
	}

	if (getRayPlaneIntersecion(rel_origin, ray.dir, pos, normalize(gizmo.z), t) && t < mint && t > 0) {
		const Vec3 p = rel_origin + ray.dir * t;
		d = length(p - pos);
		axis = Axis::Z;
	}

	if (d > scale * 1.2f) return Axis::NONE;
	return axis;
}

Axis collide(const TranslationGizmo& gizmo, const Transform& tr, const WorldView& view, const Gizmo::Config& cfg) {
	const Viewport viewport = view.getViewport();
	const Vec2 mp = view.getMousePos();
	const Ray ray = viewport.getRay(mp);

	const Vec3 rel_origin(ray.origin - viewport.pos);
	float t, tmin = FLT_MAX;
	const Vec3 pos(gizmo.pos - viewport.pos);
	bool hit = getRayTriangleIntersection(rel_origin, ray.dir, pos, pos + gizmo.x * 0.5f, pos + gizmo.y * 0.5f, &t);
	Axis transform_axis = Axis::NONE;
	if (hit) {
		tmin = t;
		transform_axis = Axis::XY;
	}
	hit = getRayTriangleIntersection(rel_origin, ray.dir, pos, pos + gizmo.y * 0.5f, pos + gizmo.z * 0.5f, &t);
	if (hit && t < tmin) {
		tmin = t;
		transform_axis = Axis::YZ;
	}
	hit = getRayTriangleIntersection(rel_origin, ray.dir, pos, pos + gizmo.x * 0.5f, pos + gizmo.z * 0.5f, &t);
	if (hit && t < tmin) transform_axis = Axis::XZ;

	if (transform_axis != Axis::NONE) return transform_axis;

	const bool is_global = cfg.coord_system == Gizmo::Config::CoordSystem::GLOBAL;
	const float scale = length(gizmo.x);
	const Vec3 x = is_global ? gizmo.x : tr.rot.rotate(Vec3(scale, 0, 0));
	const Vec3 y = is_global ? gizmo.y : tr.rot.rotate(Vec3(0, scale, 0));
	const Vec3 z = is_global ? gizmo.z : tr.rot.rotate(Vec3(0, 0, scale));
	const float x_dist = getLineSegmentDistance(rel_origin, ray.dir, pos, pos + x);
	const float y_dist = getLineSegmentDistance(rel_origin, ray.dir, pos, pos + y);
	const float z_dist = getLineSegmentDistance(rel_origin, ray.dir, pos, pos + z);

	const float influenced_dist = length(gizmo.x) * INFLUENCE_DISTANCE;
	if (x_dist < y_dist && x_dist < z_dist && x_dist < influenced_dist) return Axis::X;
	if (y_dist < z_dist && y_dist < influenced_dist) return Axis::Y;
	return z_dist < influenced_dist ? Axis::Z : Axis::NONE;
}

template <typename Gizmo>
DVec3 getMousePlaneIntersection(const WorldView& view, const Gizmo& gizmo, Axis transform_axis) {
	const Viewport& vp = view.getViewport();
	const Vec2 mouse_pos = view.getMousePos();
	const Ray ray = vp.getRay(mouse_pos);
	bool is_two_axed = transform_axis == Axis::XZ || transform_axis == Axis::XY || transform_axis == Axis::YZ;
	if (is_two_axed) {
		Vec3 plane_normal;
		switch (transform_axis) {
			case Axis::XZ: plane_normal = normalize(gizmo.y); break;
			case Axis::XY: plane_normal = normalize(gizmo.z); break;
			case Axis::YZ: plane_normal = normalize(gizmo.x); break;
			default: ASSERT(false); break;
		}
		float t;
		const Vec3 rel_origin = Vec3(ray.origin - gizmo.pos);
		if (getRayPlaneIntersecion(rel_origin, ray.dir, Vec3(0), plane_normal, t)) {
			return ray.origin + ray.dir * t;
		}
		return ray.origin;
	}

	Vec3 axis;
	switch (transform_axis) {
		case Axis::X: axis = normalize(gizmo.x); break;
		case Axis::Y: axis = normalize(gizmo.y); break;
		case Axis::Z: axis = normalize(gizmo.z); break;
		default: ASSERT(false); return DVec3(0);
	}
	const Vec3 normal = cross(cross(ray.dir, axis), ray.dir);
	const float d = dot(Vec3(ray.origin - gizmo.pos), normal) / dot(axis, normal);
	return gizmo.pos + axis * d;
}

void draw(WorldView& view, const TranslationGizmo& gizmo, const Transform& tr, Axis axis, const Gizmo::Config& cfg) {
	const DVec3 cam_pos = view.getViewport().pos;
	const Vec3 rel_pos(gizmo.pos - cam_pos);

	const bool is_global = cfg.coord_system == Gizmo::Config::CoordSystem::GLOBAL;
	const float scale = length(gizmo.x);
	const Vec3 x = is_global ? gizmo.x : tr.rot.rotate(Vec3(scale, 0, 0));
	const Vec3 y = is_global ? gizmo.y : tr.rot.rotate(Vec3(0, scale, 0));
	const Vec3 z = is_global ? gizmo.z : tr.rot.rotate(Vec3(0, 0, scale));

	switch (cfg.is_grab ? axis : Axis::NONE) {
		case Axis::X: {
			WorldView::Vertex* line_vertices = view.render(true, 2);
			line_vertices[0].pos = rel_pos - x * 10000;
			line_vertices[0].abgr = SELECTED_COLOR;
			line_vertices[1].pos = rel_pos + x * 10000;
			line_vertices[1].abgr = SELECTED_COLOR;
			break;
		}
		case Axis::Y: {
			WorldView::Vertex* line_vertices = view.render(true, 2);
			line_vertices[0].pos = rel_pos - y * 10000;
			line_vertices[0].abgr = SELECTED_COLOR;
			line_vertices[1].pos = rel_pos + y * 10000;
			line_vertices[1].abgr = SELECTED_COLOR;
			break;
		}
		case Axis::Z: {
			WorldView::Vertex* line_vertices = view.render(true, 2);
			line_vertices[0].pos = rel_pos - z * 10000;
			line_vertices[0].abgr = SELECTED_COLOR;
			line_vertices[1].pos = rel_pos + z * 10000;
			line_vertices[1].abgr = SELECTED_COLOR;
			break;
		}
		default: {
			WorldView::Vertex* line_vertices = view.render(true, 6);
			line_vertices[0].pos = rel_pos;
			line_vertices[0].abgr = axis == Axis::X ? SELECTED_COLOR : X_COLOR;
			line_vertices[1].pos = rel_pos + x;
			line_vertices[1].abgr = line_vertices[0].abgr;
			line_vertices[2].pos = rel_pos;
			line_vertices[2].abgr = axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
			line_vertices[3].pos = rel_pos + y;
			line_vertices[3].abgr = line_vertices[2].abgr;
			line_vertices[4].pos = rel_pos;
			line_vertices[4].abgr = axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
			line_vertices[5].pos = rel_pos + z;
			line_vertices[5].abgr = line_vertices[4].abgr;

			WorldView::Vertex* vertices = view.render(false, 9);

			vertices[0].pos = rel_pos;
			vertices[0].abgr = axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
			vertices[1].pos = rel_pos + gizmo.x * 0.5f;
			vertices[1].abgr = vertices[0].abgr;
			vertices[2].pos = rel_pos + gizmo.y * 0.5f;
			vertices[2].abgr = vertices[0].abgr;

			vertices[3].pos = rel_pos;
			vertices[3].abgr = axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
			vertices[4].pos = rel_pos + gizmo.y * 0.5f;
			vertices[4].abgr = vertices[3].abgr;
			vertices[5].pos = rel_pos + gizmo.z * 0.5f;
			vertices[5].abgr = vertices[3].abgr;

			vertices[6].pos = rel_pos;
			vertices[6].abgr = axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
			vertices[7].pos = rel_pos + gizmo.x * 0.5f;
			vertices[7].abgr = vertices[6].abgr;
			vertices[8].pos = rel_pos + gizmo.z * 0.5f;
			vertices[8].abgr = vertices[6].abgr;
			break;
		}
	}
}

void renderQuarterRing(WorldView& view, const Vec3& p, const Vec3& a, const Vec3& b, u32 color) {
	{
		WorldView::Vertex* vertices = view.render(false, 25*6);
	
		const float ANGLE_STEP = degreesToRadians(1.0f / 100.0f * 360.0f);
		i32 offset = -1;
		for (int i = 0; i < 25; ++i) {
			float angle = i * ANGLE_STEP;
			float s = sinf(angle);
			float c = cosf(angle);
			float sn = sinf(angle + ANGLE_STEP);
			float cn = cosf(angle + ANGLE_STEP);
	
			const Vec3 p0 = p + a * s + b * c;
			const Vec3 p1 = p + (a * 1.1f) * s + (b * 1.1f) * c;
			const Vec3 p2 = p + (a * 1.1f) * sn + (b * 1.1f) * cn;
			const Vec3 p3 = p + a * sn + b * cn;
	
			++offset;
			vertices[offset].pos = p0;
			vertices[offset].abgr = color;
	
			++offset;
			vertices[offset].pos = p1;
			vertices[offset].abgr = color;
	
			++offset;
			vertices[offset].pos = p2;
			vertices[offset].abgr = color;
	
			++offset;
			vertices[offset].pos = p0;
			vertices[offset].abgr = color;
	
			++offset;
			vertices[offset].pos = p2;
			vertices[offset].abgr = color;
	
			++offset;
			vertices[offset].pos = p3;
			vertices[offset].abgr = color;
		}
	}

	{
		const int GRID_SIZE = 5;
		WorldView::Vertex* vertices = view.render(true, (GRID_SIZE + 1) * 4);

		i32 offset = -1;
		for (int i = 0; i <= GRID_SIZE; ++i) {
			float t = 1.0f / GRID_SIZE * i;
			float ratio = sinf(acosf(t));

			++offset;
			vertices[offset].pos = p + a * t;
			vertices[offset].abgr = color;

			++offset;
			vertices[offset].pos = p + a * t + b * ratio;
			vertices[offset].abgr = color;

			++offset;
			vertices[offset].pos = p + b * t + a * ratio;
			vertices[offset].abgr = color;

			++offset;
			vertices[offset].pos = p + b * t;
			vertices[offset].abgr = color;
		}
	}
}

void renderArc(WorldView& view, const Vec3& pos, const Vec3& n, const Vec3& origin, const Vec3& dst, float scale, u32 color) {
	WorldView::Vertex* vertices = view.render(false, 25 * 3);

	int offset = -1;
	for (int i = 0; i < 25; ++i) {
		const Vec3 a = scale * normalize(slerp(origin, dst, i / 25.f));
		const Vec3 b = scale * normalize(slerp(origin, dst, (i + 1) / 25.f));

		++offset;
		vertices[offset] = { pos, color };

		++offset;
		Vec3 p = pos + a;
		vertices[offset] = { p, color };

		++offset;
		p = pos + b;
		vertices[offset] = { p, color };
	}
}

void draw(WorldView& view, const RotationGizmo& gizmo, Axis axis, bool active, const DVec3& current, const Gizmo::Config& cfg) {
	const Viewport vp = view.getViewport();
	const float scale = getScale(vp, gizmo.pos, cfg.scale);
	const Vec3 rel_pos(gizmo.pos - vp.pos);

	if (!active) {
		renderQuarterRing(view, rel_pos, gizmo.x, gizmo.y, axis == Axis::Z ? SELECTED_COLOR : Z_COLOR);
		renderQuarterRing(view, rel_pos, gizmo.y, gizmo.z, axis == Axis::X ? SELECTED_COLOR : X_COLOR);
		renderQuarterRing(view, rel_pos, gizmo.x, gizmo.z, axis == Axis::Y ? SELECTED_COLOR : Y_COLOR);
		return;
	}

	Vec3 n;
	Vec3 axis1, axis2;
	switch (axis) {
		case Axis::X:
			n = gizmo.x;
			axis1 = gizmo.y;
			axis2 = gizmo.z;
			break;
		case Axis::Y:
			n = gizmo.y;
			axis1 = gizmo.x;
			axis2 = gizmo.z;
			break;
		case Axis::Z:
			n = gizmo.z;
			axis1 = gizmo.x;
			axis2 = gizmo.y;
			break;
		default: ASSERT(false); break;
	}
	renderQuarterRing(view, rel_pos, axis1, axis2, SELECTED_COLOR);
	renderQuarterRing(view, rel_pos, -axis1, axis2, SELECTED_COLOR);
	renderQuarterRing(view, rel_pos, -axis1, -axis2, SELECTED_COLOR);
	renderQuarterRing(view, rel_pos, axis1, -axis2, SELECTED_COLOR);

	const Vec3 origin = normalize(Vec3(g_gizmo_state.prev_point - gizmo.pos));
	const Vec3 d1 = normalize(Vec3(current - gizmo.pos));
	renderArc(view, rel_pos, n, origin, d1, scale, 0x8800a5ff);
}

Axis toPlane(Axis axis) {
	switch (axis) {
		case Axis::X: return Axis::YZ;
		case Axis::Y: return Axis::XZ;
		case Axis::Z: return Axis::XY;
		default: ASSERT(false); return Axis::NONE;
	}
}

float computeRotateAngle(WorldView& view, const RotationGizmo& gizmo, Axis normal_axis) {
	Axis plane;
	Vec3 axis;
	switch (normal_axis) {
		case Axis::X: plane = Axis::YZ; axis = normalize(gizmo.x); break;
		case Axis::Y: plane = Axis::XZ; axis = normalize(gizmo.y); break;
		case Axis::Z: plane = Axis::XY; axis = normalize(gizmo.z); break;
		default: ASSERT(false); return 0;
	}

	const DVec3 pos = g_gizmo_state.prev_point;
	const DVec3 start_pos = getMousePlaneIntersection(view, gizmo, plane);
	const Vec3 delta = normalize(Vec3(pos - gizmo.pos));
	const Vec3 start_delta = normalize(Vec3(start_pos - gizmo.pos));
		
	const Vec3 side = cross(axis, start_delta);

	const float y = clamp(dot(delta, start_delta), -1.0f, 1.0f);
	const float x = clamp(dot(delta, side), -1.0f, 1.0f);

	return -atan2f(x, y);
}
	
void draw(WorldView& view, const ScaleGizmo& gizmo, Axis axis, const Gizmo::Config& cfg) {
	const Viewport vp = view.getViewport();
	const float scale = getScale(vp, gizmo.pos, cfg.scale) * 0.1f;
	const Vec3 rel_pos = Vec3(gizmo.pos - vp.pos);

	{
		WorldView::Vertex* vertices = view.render(true, 6);

		vertices[0].pos = rel_pos;
		vertices[0].abgr = axis == Axis::X ? SELECTED_COLOR : X_COLOR;
		vertices[1].pos = rel_pos + gizmo.x;
		vertices[1].abgr = axis == Axis::X ? SELECTED_COLOR : X_COLOR;
		vertices[2].pos = rel_pos;
		vertices[2].abgr = axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
		vertices[3].pos = rel_pos + gizmo.y;
		vertices[3].abgr = axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
		vertices[4].pos = rel_pos;
		vertices[4].abgr = axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
		vertices[5].pos = rel_pos + gizmo.z;
		vertices[5].abgr = axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
	}

	const Vec3 x = normalize(gizmo.x);
	const Vec3 y = normalize(gizmo.y);
	const Vec3 z = normalize(gizmo.z);

	renderCube(view, axis == Axis::X ? SELECTED_COLOR : X_COLOR, rel_pos + gizmo.x, scale, x, y, z);
	renderCube(view, axis == Axis::Y ? SELECTED_COLOR : Y_COLOR, rel_pos + gizmo.y, scale, x, y, z);
	renderCube(view, axis == Axis::Z ? SELECTED_COLOR : Z_COLOR, rel_pos + gizmo.z, scale, x, y, z);
}

void frame() {
	++g_gizmo_state.frame;
	if (g_gizmo_state.last_manipulate_frame < g_gizmo_state.frame - 2) {
		g_gizmo_state.active_id = ~(u64)0;
		g_gizmo_state.dragged_id = ~(u64)0;
	}
}

void setDragged(u64 id) {
	g_gizmo_state.dragged_id = id;
}

bool translate(u64 id, WorldView& view, Transform& tr, const Gizmo::Config& cfg) {
	TranslationGizmo gizmo = getGizmo<TranslationGizmo>(view, tr, cfg);

	const bool none_active = g_gizmo_state.dragged_id == ~(u64)0;
	const bool other_is_active = !none_active && id != g_gizmo_state.dragged_id;
	if (other_is_active) {
		draw(view, gizmo, tr, Axis::NONE, cfg);
		return false;
	}

	if (none_active) {
		if (cfg.is_grab) {
			g_gizmo_state.active_id = id;
			g_gizmo_state.dragged_id = id;
			g_gizmo_state.prev_point = getMousePlaneIntersection(view, gizmo, g_gizmo_state.axis);
			g_gizmo_state.start_pos = gizmo.pos;
			draw(view, gizmo, tr, g_gizmo_state.axis, cfg);
		}
		else {
			const Axis axis = collide(gizmo, tr, view, cfg);
			if (axis != Axis::NONE) g_gizmo_state.active_id = id;
			else if (g_gizmo_state.active_id == id) g_gizmo_state.active_id = ~(u64)0;
			draw(view, gizmo, tr, axis, cfg);
			if (view.isMouseClick(os::MouseButton::LEFT) && axis != Axis::NONE) {
				g_gizmo_state.dragged_id = id;
				g_gizmo_state.axis = axis;
				g_gizmo_state.prev_point = getMousePlaneIntersection(view, gizmo, g_gizmo_state.axis);
				g_gizmo_state.start_pos = gizmo.pos;
			}
		}
		return false;
	}

	if (!view.isMouseDown(os::MouseButton::LEFT) && !cfg.is_grab) {
		g_gizmo_state.dragged_id = ~(u64)0;
		g_gizmo_state.axis = Axis::NONE;
		return false;
	}
		
	draw(view, gizmo, tr, g_gizmo_state.axis, cfg);
		
	const DVec3 pos = getMousePlaneIntersection(view, gizmo, g_gizmo_state.axis);
	const Vec3 delta_vec = Vec3(pos - g_gizmo_state.prev_point);
	DVec3 res = tr.pos + delta_vec;

	auto print_delta = [&](){
		const Vec2 p = view.getViewport().worldToScreenPixels(gizmo.pos);
		const Vec3 from_start = Vec3(tr.pos - g_gizmo_state.start_pos);
		StaticString<128> tmp(from_start.x, "; ", from_start.y, "; ", from_start.z);
		view.addText2D(p.x + 31, p.y + 31, 0xff000000, tmp);
		view.addText2D(p.x + 30, p.y + 30, 0xffffFFFF, tmp);
	};

	if (!cfg.is_step || cfg.getStep() <= 0) {
		g_gizmo_state.prev_point = pos;
		tr.pos = res;
		print_delta();		
		return squaredLength(delta_vec) > 0.f;
	}

	const float step = cfg.getStep();
	res.x = double(i64((res.x + signum(res.x) * step * 0.5f) / step)) * step;
	res.y = double(i64((res.y + signum(res.y) * step * 0.5f) / step)) * step;
	res.z = double(i64((res.z + signum(res.z) * step * 0.5f) / step)) * step;
	if (res.x != tr.pos.x || res.y != tr.pos.y || res.z != tr.pos.z) {
		g_gizmo_state.prev_point = res;
		tr.pos = res;
		print_delta();
		return true;
	}
	print_delta();
	return false;
}

bool scale(u64 id, WorldView& view, Transform& tr, const Gizmo::Config& cfg) {
	ScaleGizmo gizmo = getGizmo<ScaleGizmo>(view, tr, cfg);
		
	const bool none_active = g_gizmo_state.dragged_id == ~(u64)0;
	const bool other_is_active = !none_active && id != g_gizmo_state.dragged_id;
	if (other_is_active) {
		draw(view, gizmo, Axis::NONE, cfg);
		return false;
	}

	if (none_active) {
		const Axis axis = collide(gizmo, view, cfg);
		if (axis != Axis::NONE) g_gizmo_state.active_id = id;
		else if (g_gizmo_state.active_id == id) g_gizmo_state.active_id = ~(u64)0;
		draw(view, gizmo, axis, cfg);
		if (view.isMouseClick(os::MouseButton::LEFT) && axis != Axis::NONE) {
			g_gizmo_state.dragged_id = id;
			g_gizmo_state.axis = axis;
			g_gizmo_state.prev_point = getMousePlaneIntersection(view, gizmo, axis);
		}
		return false;
	}

	if (!view.isMouseDown(os::MouseButton::LEFT)) {
		g_gizmo_state.dragged_id = ~(u64)0;
		g_gizmo_state.axis = Axis::NONE;
		return false;
	}

	const DVec3 p = getMousePlaneIntersection(view, gizmo, g_gizmo_state.axis);
	const Vec3 delta = Vec3(p - g_gizmo_state.prev_point);
	const float sign = dot(delta, Vec3(p - gizmo.pos)) < 0 ? -1.f : 1.f;

	draw(view, gizmo, g_gizmo_state.axis, cfg);
	if (squaredLength(delta) > 0) {
		g_gizmo_state.prev_point = p;
		const Axis axis = cfg.anisotropic_scale ? g_gizmo_state.axis : Axis::NONE;
		switch (axis) {
			case Axis::X: tr.scale.x += length(delta) * sign; break;
			case Axis::Y: tr.scale.y += length(delta) * sign; break;
			case Axis::Z: tr.scale.z += length(delta) * sign; break;
			default: tr.scale += Vec3(length(delta) * sign); break;
		}
		return true;
	}
	return false;
}


bool rotate(u64 id, WorldView& view, Transform& tr, const Gizmo::Config& cfg) {
	RotationGizmo gizmo = getGizmo<RotationGizmo>(view, tr, cfg);

	const bool none_active = g_gizmo_state.dragged_id == ~(u64)0;
	const bool other_is_active = !none_active && id != g_gizmo_state.dragged_id;
	if (other_is_active) {
		draw(view, gizmo, Axis::NONE, false, 0, cfg);
		return false;
	}

	if (none_active) {
		const Axis axis = collide(gizmo, view, cfg);
		if (axis != Axis::NONE) g_gizmo_state.active_id = id;
		else if (g_gizmo_state.active_id == id) g_gizmo_state.active_id = ~(u64)0;
		draw(view, gizmo, axis, false, 0, cfg);
		if (view.isMouseClick(os::MouseButton::LEFT) && axis != Axis::NONE) {
			g_gizmo_state.dragged_id = id;
			g_gizmo_state.axis = axis;
			g_gizmo_state.prev_point = getMousePlaneIntersection(view, gizmo, toPlane(axis));
			g_gizmo_state.start_rot = tr.rot;
		}
		return false;
	}

	if (!view.isMouseDown(os::MouseButton::LEFT)) {
		g_gizmo_state.dragged_id = ~(u64)0;
		g_gizmo_state.axis = Axis::NONE;
		return false;
	}

	const DVec3 current = getMousePlaneIntersection(view, gizmo, toPlane(g_gizmo_state.axis));
	draw(view, gizmo, g_gizmo_state.axis, g_gizmo_state.dragged_id == id, current, cfg);

	float angle = computeRotateAngle(view, gizmo, g_gizmo_state.axis);
	if(angle != 0) {
		Vec3 normal;
		switch (g_gizmo_state.axis) {
			case Axis::X: normal = gizmo.x; break;
			case Axis::Y: normal = gizmo.y; break;
			case Axis::Z: normal = gizmo.z; break;
			default: ASSERT(false); break;
		}

		if (!cfg.is_step || cfg.getStep() <= 0) {
			tr.rot = normalize(Quat(normalize(normal), angle) * g_gizmo_state.start_rot);
			return true;
		}

		if (cfg.is_step && fabs(angle) > degreesToRadians(cfg.getStep())) {
			angle = angle - fmodf(angle, degreesToRadians(cfg.getStep()));
			tr.rot = normalize(Quat(normalize(normal), angle) * g_gizmo_state.start_rot);
			return true;
		}
	}
	return false;
}


bool isActive() { return g_gizmo_state.active_id != ~(u64)0 || g_gizmo_state.dragged_id != ~(u64)0; }


bool box(u64 id, WorldView& view, Transform& tr, Vec3& half_extents, const Config& cfg, bool keep_center) {
	id |= u64(0xff) << 56;
	const Vec3 xn = tr.rot.rotate(Vec3(1, 0, 0));
	const Vec3 yn = tr.rot.rotate(Vec3(0, 1, 0));
	const Vec3 zn = tr.rot.rotate(Vec3(0, 0, 1));
	const Vec3 x = xn * half_extents.x;
	const Vec3 y = yn * half_extents.y;
	const Vec3 z = zn * half_extents.z;
	addCube(view, tr.pos, x, y, z, Color::BLUE);
	
	const Viewport vp = view.getViewport();
	const float scale = getScale(vp, tr.pos, cfg.scale) * 0.1f;

	const Vec2 mp = view.getMousePos();
	const Ray ray = vp.getRay(mp);
	
	const Vec3 pos = Vec3(ray.origin - tr.pos);
	const Vec3 center = Vec3(tr.pos - vp.pos);
	auto cube = [&](u32 color, Vec3 p, float& prev_t){
		float t;
		if (getRaySphereIntersection(pos, ray.dir, p, scale * 1.414f, t) && (prev_t < 0 || t < prev_t)) {
			renderCube(view, SELECTED_COLOR, center + p, scale, xn, yn, zn);
			WorldView::Vertex* line = view.render(true, 2);
			line[0].pos = center;
			line[0].abgr = color;
			line[1].pos = center + 2 * p;
			line[1].abgr = color;
			prev_t = t;
			return true;
		}
		renderCube(view, color, center + p, scale, xn, yn, zn);
		return false;
	};

	const bool none_active = g_gizmo_state.dragged_id == ~(u64)0;
	const bool other_is_active = !none_active && id != g_gizmo_state.dragged_id;
	if (other_is_active) return false;

	BoxAxis axis = BoxAxis::NONE;
	float t = -1;
	if (cube(X_COLOR, x, t)) axis = BoxAxis::XP;
	if (cube(X_COLOR, -x, t)) axis = BoxAxis::XN;
	if (cube(Y_COLOR, y, t)) axis = BoxAxis::YP;
	if (cube(Y_COLOR, -y, t)) axis = BoxAxis::YN;
	if (cube(Z_COLOR, z, t)) axis = BoxAxis::ZP;
	if (cube(Z_COLOR, -z, t)) axis = BoxAxis::ZN;

	if (axis != BoxAxis::NONE) g_gizmo_state.active_id = id;
	BoxGizmo gizmo = getGizmo<BoxGizmo>(view, tr, cfg);
	if (view.isMouseClick(os::MouseButton::LEFT) && t >= 0) {
		switch(axis) {
			case BoxAxis::XP:
			case BoxAxis::XN:
				g_gizmo_state.axis = Axis::X;
				break;
			case BoxAxis::YP:
			case BoxAxis::YN:
				g_gizmo_state.axis = Axis::Y;
				break;
			case BoxAxis::ZP:
			case BoxAxis::ZN:
				g_gizmo_state.axis = Axis::Z;
				break;
			default: ASSERT(false); break;
		}
		g_gizmo_state.box.axis = axis;
		g_gizmo_state.box.start_transform = tr;
		g_gizmo_state.box.start_half_extents = half_extents;
		g_gizmo_state.box.start_pos = getMousePlaneIntersection(view, gizmo, g_gizmo_state.axis);
		g_gizmo_state.dragged_id = id;
	}

	if (none_active) return false;

	if (!view.isMouseDown(os::MouseButton::LEFT)) {
		g_gizmo_state.dragged_id = ~(u64)0;
		g_gizmo_state.axis = Axis::NONE;
		return false;
	}

	const Vec3 diff = Vec3(getMousePlaneIntersection(view, gizmo, g_gizmo_state.axis) - g_gizmo_state.box.start_pos);
	switch (g_gizmo_state.box.axis) {
		case BoxAxis::XN:
		case BoxAxis::XP: {
			const float sign = g_gizmo_state.box.axis == BoxAxis::XN ? -1.f : 1.f;
			const DVec3 e0 = g_gizmo_state.box.start_transform.pos - xn *  g_gizmo_state.box.start_half_extents.x * sign;
			if (keep_center) {
				const float half = g_gizmo_state.box.start_half_extents.x + dot(diff, xn) * sign;
				half_extents.x = half;
			}
			else {
				const float half = g_gizmo_state.box.start_half_extents.x + dot(diff, xn) * 0.5f * sign;
				const DVec3 c = e0 + xn * half * sign;
				tr.pos = c;
				half_extents.x = half;
			}
			return true;
		}
		case BoxAxis::YN:
		case BoxAxis::YP: {
			const float sign = g_gizmo_state.box.axis == BoxAxis::YN ? -1.f : 1.f;
			const DVec3 e0 = g_gizmo_state.box.start_transform.pos - yn *  g_gizmo_state.box.start_half_extents.y * sign;
			if (keep_center) {
				const float half = g_gizmo_state.box.start_half_extents.y + dot(diff, yn) * sign;
				half_extents.y = half;
			}
			else {
				const float half = g_gizmo_state.box.start_half_extents.y + dot(diff, yn) * 0.5f * sign;
				const DVec3 c = e0 + yn * half * sign;
				tr.pos = c;
				half_extents.y = half;
			}
			return true;
		}
		case BoxAxis::ZN:
		case BoxAxis::ZP: {
			const float sign = g_gizmo_state.box.axis == BoxAxis::ZN ? -1.f : 1.f;
			const DVec3 e0 = g_gizmo_state.box.start_transform.pos - zn *  g_gizmo_state.box.start_half_extents.z * sign;
			if (keep_center) {
				const float half = g_gizmo_state.box.start_half_extents.z + dot(diff, zn) * sign;
				half_extents.z = half;
			}
			else {
				const float half = g_gizmo_state.box.start_half_extents.z + dot(diff, zn) * 0.5f * sign;
				const DVec3 c = e0 + zn * half * sign;
				tr.pos = c;
				half_extents.z = half;
			}
			return true;
		}
		default: ASSERT(false); break;
	}

	return false;
}


bool manipulate(u64 id, WorldView& view, Transform& tr, const Config& cfg) {
	g_gizmo_state.last_manipulate_frame = g_gizmo_state.frame;
	switch (cfg.mode) {
		case Gizmo::Config::TRANSLATE: return translate(id, view, tr, cfg);
		case Gizmo::Config::ROTATE: return rotate(id, view, tr, cfg);
		case Gizmo::Config::SCALE: return scale(id, view, tr, cfg);
	}
	ASSERT(false);
	return false;
}

void Config::lockXAxis() {
	if (!is_grab) return;

	g_gizmo_state.dragged_id = ~(u64)0;
	g_gizmo_state.axis = Axis::X;
}

void Config::lockYAxis() {
	if (!is_grab) return;

	g_gizmo_state.dragged_id = ~(u64)0;
	g_gizmo_state.axis = Axis::Y;
}

void Config::lockZAxis() {
	if (!is_grab) return;
	
	g_gizmo_state.dragged_id = ~(u64)0;
	g_gizmo_state.axis = Axis::Z;
}

void Config::ungrab() {
	is_grab = false;
	g_gizmo_state.dragged_id = ~(u64)0;
}

void Config::grab() {
	is_grab = true;
	g_gizmo_state.axis = Axis::X;
	g_gizmo_state.dragged_id = ~(u64)0;
}

} // namespace Gizmo
} // namespace Lumix
