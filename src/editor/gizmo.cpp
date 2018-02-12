#include "editor/gizmo.h"
#include "engine/math_utils.h"
#include "engine/matrix.h"
#include "engine/quat.h"
#include "engine/universe/universe.h"
#include "render_interface.h"
#include <cfloat>
#include <cmath>


namespace Lumix
{


static const float INFLUENCE_DISTANCE = 0.3f;
static const u32 X_COLOR = 0xff6363cf;
static const u32 Y_COLOR = 0xff63cf63;
static const u32 Z_COLOR = 0xffcf6363;
static const u32 SELECTED_COLOR = 0xff63cfcf;


enum class Axis : u32
{
	NONE,
	X,
	Y,
	Z,
	XY,
	XZ,
	YZ
};


enum class Pivot
{
	CENTER,
	OBJECT_PIVOT
};


enum class Mode : u32
{
	ROTATE,
	TRANSLATE,
	SCALE,

	COUNT
};


enum class CoordSystem
{
	LOCAL,
	WORLD
};


struct GizmoImpl LUMIX_FINAL : public Gizmo
{
	static const int MAX_GIZMOS = 16;
	static const int MAX_IMMEDIATE = 16;


	explicit GizmoImpl(WorldEditor& editor)
		: m_editor(editor)
		, m_mode(Mode::TRANSLATE)
		, m_pivot(Pivot::CENTER)
		, m_coord_system(CoordSystem::LOCAL)
		, m_is_autosnap_down(false)
		, m_count(0)
		, m_transform_axis(Axis::X)
		, m_active(-1)
		, m_is_step(false)
		, m_rel_accum(0, 0)
		, m_is_dragging(false)
		, m_mouse_pos(0, 0)
		, m_offset(0, 0, 0)
		, m_immediate_count(0)
	{
		m_steps[int(Mode::TRANSLATE)] = 10;
		m_steps[int(Mode::ROTATE)] = 45;
		m_steps[int(Mode::SCALE)] = 1;
		editor.universeDestroyed().bind<GizmoImpl, &GizmoImpl::onUniverseDestroyed>(this);
	}


	~GizmoImpl()
	{
		m_editor.universeDestroyed().unbind<GizmoImpl, &GizmoImpl::onUniverseDestroyed>(this);
	}


	void onUniverseDestroyed()
	{
		m_count = 0;
	}


	Matrix getMatrix(Entity entity) const
	{
		Matrix mtx;
		auto* universe = m_editor.getUniverse();
		if (m_pivot == Pivot::OBJECT_PIVOT)
		{
			mtx = universe->getPositionAndRotation(entity);
			mtx.translate(mtx.getRotation().rotate(m_offset));
		}
		else if (m_pivot == Pivot::CENTER)
		{
			mtx = universe->getPositionAndRotation(entity);
			float scale = universe->getScale(entity);
			Matrix scale_mtx = Matrix::IDENTITY;
			scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;
			Vec3 center = m_editor.getRenderInterface()->getModelCenter(entity);
			mtx.setTranslation((mtx * scale_mtx).transformPoint(center));
		}
		else
		{
			ASSERT(false);
		}

		if (m_coord_system == CoordSystem::WORLD)
		{
			Vec3 pos = mtx.getTranslation();
			mtx = Matrix::IDENTITY;
			mtx.setTranslation(pos);
		}
		return mtx;
	}


	static float getScale(const Vec3& camera_pos, float fov, const Vec3& pos, float entity_scale, bool is_ortho)
	{
		if (is_ortho) return 2;
		float scale = tanf(fov * 0.5f) * (pos - camera_pos).length() * 2;
		return scale / (10 / entity_scale);
	}


	void renderTranslateGizmo(const Matrix& gizmo_mtx,
		bool is_active,
		const Vec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho) const
	{
		Axis transform_axis = is_active ? m_transform_axis : Axis::NONE;
		Matrix scale_mtx = Matrix::IDENTITY;
		auto entity_pos = gizmo_mtx.getTranslation();
		float scale = getScale(camera_pos, fov, entity_pos, gizmo_mtx.getXVector().length(), is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Vec3 to_entity_dir = is_ortho ? camera_dir : camera_pos - entity_pos;
		Matrix mtx = gizmo_mtx * scale_mtx;

		RenderInterface::Vertex vertices[9];
		u16 indices[9];
		vertices[0].position = Vec3(0, 0, 0);
		vertices[0].color = transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
		indices[0] = 0;
		vertices[1].position = Vec3(1, 0, 0);
		vertices[1].color = transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
		indices[1] = 1;
		vertices[2].position = Vec3(0, 0, 0);
		vertices[2].color = transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
		indices[2] = 2;
		vertices[3].position = Vec3(0, 1, 0);
		vertices[3].color = transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
		indices[3] = 3;
		vertices[4].position = Vec3(0, 0, 0);
		vertices[4].color = transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
		indices[4] = 4;
		vertices[5].position = Vec3(0, 0, 1);
		vertices[5].color = transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
		indices[5] = 5;

		m_editor.getRenderInterface()->render(mtx, indices, 6, vertices, 6, true);

		if (dotProduct(gizmo_mtx.getXVector(), to_entity_dir) < 0) mtx.setXVector(-mtx.getXVector());
		if (dotProduct(gizmo_mtx.getYVector(), to_entity_dir) < 0) mtx.setYVector(-mtx.getYVector());
		if (dotProduct(gizmo_mtx.getZVector(), to_entity_dir) < 0) mtx.setZVector(-mtx.getZVector());

		vertices[0].position = Vec3(0, 0, 0);
		vertices[0].color = transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
		indices[0] = 0;
		vertices[1].position = Vec3(0.5f, 0, 0);
		vertices[1].color = transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
		indices[1] = 1;
		vertices[2].position = Vec3(0, 0.5f, 0);
		vertices[2].color = transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
		indices[2] = 2;

		vertices[3].position = Vec3(0, 0, 0);
		vertices[3].color = transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
		indices[3] = 3;
		vertices[4].position = Vec3(0, 0.5f, 0);
		vertices[4].color = transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
		indices[4] = 4;
		vertices[5].position = Vec3(0, 0, 0.5f);
		vertices[5].color = transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
		indices[5] = 5;

		vertices[6].position = Vec3(0, 0, 0);
		vertices[6].color = transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
		indices[6] = 6;
		vertices[7].position = Vec3(0.5f, 0, 0);
		vertices[7].color = transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
		indices[7] = 7;
		vertices[8].position = Vec3(0, 0, 0.5f);
		vertices[8].color = transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
		indices[8] = 8;

		RenderInterface* ri = m_editor.getRenderInterface();
		ri->render(mtx, indices, 9, vertices, 9, false);
		if (m_is_dragging)
		{
			Vec3 intersection = getMousePlaneIntersection(m_editor.getMousePos(), gizmo_mtx, m_transform_axis);
			Vec3 delta_vec = intersection - m_start_axis_point;

			Vec2 p = ri->worldToScreenPixels(entity_pos);
			StaticString<128> tmp("", delta_vec.x, "; ", delta_vec.y, "; ", delta_vec.z);
			ri->addText2D(p.x + 31, p.y + 31, 16, 0xff000000, tmp);
			ri->addText2D(p.x + 30, p.y + 30, 16, 0xffffFFFF, tmp);
		}
	}



	void renderScaleGizmo(const Matrix& gizmo_mtx,
		bool is_active,
		const Vec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho)
	{
		Axis transform_axis = is_active ? m_transform_axis : Axis::NONE;
		Matrix scale_mtx = Matrix::IDENTITY;
		auto entity_pos = gizmo_mtx.getTranslation();
		float scale = getScale(camera_pos, fov, entity_pos, gizmo_mtx.getXVector().length(), is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Matrix mtx = gizmo_mtx * scale_mtx;

		RenderInterface::Vertex vertices[9];
		u16 indices[12];
		vertices[0].position = Vec3(0, 0, 0);
		vertices[0].color = transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
		indices[0] = 0;
		vertices[1].position = Vec3(1, 0, 0);
		vertices[1].color = transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
		indices[1] = 1;
		vertices[2].position = Vec3(0, 0, 0);
		vertices[2].color = transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
		indices[2] = 2;
		vertices[3].position = Vec3(0, 1, 0);
		vertices[3].color = transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
		indices[3] = 3;
		vertices[4].position = Vec3(0, 0, 0);
		vertices[4].color = transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
		indices[4] = 4;
		vertices[5].position = Vec3(0, 0, 1);
		vertices[5].color = transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
		indices[5] = 5;

		m_editor.getRenderInterface()->render(mtx, indices, 6, vertices, 6, true);

		auto renderCube = [this](u32 color, const Matrix& mtx, const Vec3& pos) 
		{
			RenderInterface::Vertex vertices[8];

			for (int i = 0; i < 8; ++i) vertices[i].color = color;

			vertices[0].position = pos + Vec3(-0.1f, -0.1f, -0.1f);
			vertices[1].position = pos + Vec3(0.1f, -0.1f, -0.1f);
			vertices[2].position = pos + Vec3(0.1f, -0.1f, 0.1f);
			vertices[3].position = pos + Vec3(-0.1f, -0.1f, 0.1f);

			vertices[4].position = pos + Vec3(-0.1f, 0.1f, -0.1f);
			vertices[5].position = pos + Vec3(0.1f, 0.1f, -0.1f);
			vertices[6].position = pos + Vec3(0.1f, 0.1f, 0.1f);
			vertices[7].position = pos + Vec3(-0.1f, 0.1f, 0.1f);

			u16 indices[36] =
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
			m_editor.getRenderInterface()->render(mtx, indices, 36, vertices, 8, false);
		};
		renderCube(transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR, mtx, Vec3(1, 0, 0));
		renderCube(transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR, mtx, Vec3(0, 1, 0));
		renderCube(transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR, mtx, Vec3(0, 0, 1));
		
	}

	void renderQuarterRing(const Matrix& mtx, const Vec3& a, const Vec3& b, u32 color) const
	{
		RenderInterface::Vertex vertices[1200];
		u16 indices[1200];
		const float ANGLE_STEP = Math::degreesToRadians(1.0f / 100.0f * 360.0f);
		Vec3 n = crossProduct(a, b) * 0.05f;
		int offset = -1;
		for (int i = 0; i < 25; ++i)
		{
			float angle = i * ANGLE_STEP;
			float s = sinf(angle);
			float c = cosf(angle);
			float sn = sinf(angle + ANGLE_STEP);
			float cn = cosf(angle + ANGLE_STEP);

			Vec3 p0 = a * s + b * c - n * 0.5f;
			Vec3 p1 = a * sn + b * cn - n * 0.5f;

			++offset;
			vertices[offset].position = p0;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p1;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p0 + n;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p1;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p1 + n;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p0 + n;
			vertices[offset].color = color;
			indices[offset] = offset;
		}

		m_editor.getRenderInterface()->render(mtx, indices, offset, vertices, offset, false);

		const int GRID_SIZE = 5;
		offset = -1;
		for (int i = 0; i <= GRID_SIZE; ++i)
		{
			float t = 1.0f / GRID_SIZE * i;
			float ratio = sinf(acosf(t));

			++offset;
			vertices[offset].position = a * t;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = a * t + b * ratio;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = b * t + a * ratio;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = b * t;
			vertices[offset].color = color;
			indices[offset] = offset;
		}

		m_editor.getRenderInterface()->render(mtx, indices, offset, vertices, offset, true);
	}


	void renderArc(const Vec3& pos, const Vec3& n, const Vec3& origin, float angle, u32 color) const
	{
		u16 indices[51 * 3];
		RenderInterface::Vertex vertices[52];
		
		int count = Math::clamp(int(fabs(angle) / 0.1f), 1, 50);

		Vec3 side = crossProduct(n.normalized(), origin);

		vertices[0] = { pos, color, 0, 0 };
		for (int i = 0; i <= count; ++i)
		{
			float a = angle / count * i;

			Vec3 p = pos + origin * cosf(a) + side * sinf(a);
			vertices[i + 1] = { p, color, 0, 0 };
		}
		for (int i = 0; i < count; ++i)
		{
			indices[i * 3 + 0] = 0;
			indices[i * 3 + 1] = i+1;
			indices[i * 3 + 2] = i + 2;
		}

		RenderInterface* ri = m_editor.getRenderInterface();
		ri->render(Matrix::IDENTITY, indices, count * 3, vertices, count + 2, false);
	}


	void renderRotateGizmo(const Matrix& gizmo_mtx,
		bool is_active,
		const Vec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho)
	{
		Axis transform_axis = is_active ? m_transform_axis : Axis::NONE;
		Matrix scale_mtx = Matrix::IDENTITY;
		Vec3 entity_pos = gizmo_mtx.getTranslation();
		float scale = getScale(camera_pos, fov, entity_pos, gizmo_mtx.getXVector().length(), is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Vec3 to_entity_dir = is_ortho ? camera_dir : camera_pos - entity_pos;
		Matrix mtx = gizmo_mtx * scale_mtx;

		Vec3 right(1, 0, 0);
		Vec3 up(0, 1, 0);
		Vec3 dir(0, 0, 1);

		if (dotProduct(gizmo_mtx.getXVector(), to_entity_dir) < 0) right = -right;
		if (dotProduct(gizmo_mtx.getYVector(), to_entity_dir) < 0) up = -up;
		if (dotProduct(gizmo_mtx.getZVector(), to_entity_dir) < 0) dir = -dir;

		if (!m_is_dragging || !is_active)
		{
			renderQuarterRing(mtx, right, up, transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR);
			renderQuarterRing(mtx, up, dir, transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR);
			renderQuarterRing(mtx, right, dir, transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR);
		}
		else
		{
			Vec3 n;
			Vec3 axis1, axis2;
			switch (transform_axis)
			{
				case Axis::X:
					n = gizmo_mtx.getXVector();
					axis1 = up;
					axis2 = dir;
					break;
				case Axis::Y:
					n = gizmo_mtx.getYVector();
					axis1 = right;
					axis2 = dir;
					break;
				case Axis::Z:
					n = gizmo_mtx.getZVector();
					axis1 = right;
					axis2 = up;
					break;
				default: ASSERT(false); break;
			}
			renderQuarterRing(mtx, axis1, axis2, SELECTED_COLOR);
			renderQuarterRing(mtx, -axis1, axis2, SELECTED_COLOR);
			renderQuarterRing(mtx, -axis1, -axis2, SELECTED_COLOR);
			renderQuarterRing(mtx, axis1, -axis2, SELECTED_COLOR);
			RenderInterface* ri = m_editor.getRenderInterface();

			Vec3 origin = (m_start_plane_point - entity_pos).normalized();
			renderArc(entity_pos, n * scale, origin * scale, m_angle_accum, 0x8800a5ff);
			float angle_degrees = Math::radiansToDegrees(m_angle_accum);
			Vec2 p = ri->worldToScreenPixels(entity_pos);
			StaticString<128> tmp("", angle_degrees, " deg");
			Vec2 screen_delta = (m_start_mouse_pos - p).normalized();
			Vec2 text_pos = m_start_mouse_pos + screen_delta * 15;
			ri->addText2D(text_pos.x + 1, text_pos.y + 1, 16, 0xff000000, tmp);
			ri->addText2D(text_pos.x, text_pos.y, 16, 0xffffFFFF, tmp);
		}
	}


	void clearEntities() override
	{
		m_active = -1;
		m_pivot = Pivot::CENTER;
		m_offset.set(0, 0, 0);
		m_count = 0;
	}


	bool isActive() const override
	{
		return m_transform_axis != Axis::NONE;
	}


	Axis collideTranslate(const Matrix& gizmo_mtx,
		const Vec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		const Vec3& origin,
		const Vec3& dir) const
	{
		Matrix scale_mtx = Matrix::IDENTITY;
		Vec3 entity_pos = gizmo_mtx.getTranslation();
		float scale = getScale(camera_pos, fov, entity_pos, gizmo_mtx.getXVector().length(), is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Vec3 to_entity_dir = is_ortho ? camera_dir : camera_pos - entity_pos;
		Matrix mtx = gizmo_mtx * scale_mtx;
		Vec3 pos = mtx.getTranslation();

		Vec3 x = mtx.getXVector() * 0.5f;
		Vec3 y = mtx.getYVector() * 0.5f;
		Vec3 z = mtx.getZVector() * 0.5f;

		if (dotProduct(gizmo_mtx.getXVector(), to_entity_dir) < 0) x = -x;
		if (dotProduct(gizmo_mtx.getYVector(), to_entity_dir) < 0) y = -y;
		if (dotProduct(gizmo_mtx.getZVector(), to_entity_dir) < 0) z = -z;

		float t, tmin = FLT_MAX;
		bool hit = Math::getRayTriangleIntersection(origin, dir, pos, pos + x, pos + y, &t);
		Axis transform_axis = Axis::NONE;
		if (hit)
		{
			tmin = t;
			transform_axis = Axis::XY;
		}
		hit = Math::getRayTriangleIntersection(origin, dir, pos, pos + y, pos + z, &t);
		if (hit && t < tmin)
		{
			tmin = t;
			transform_axis = Axis::YZ;
		}
		hit = Math::getRayTriangleIntersection(origin, dir, pos, pos + x, pos + z, &t);
		if (hit && t < tmin)
		{
			transform_axis = Axis::XZ;
		}

		if (transform_axis != Axis::NONE)
		{
			return transform_axis;
		}

		float x_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getXVector());
		float y_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getYVector());
		float z_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getZVector());

		float influenced_dist = scale * INFLUENCE_DISTANCE;
		if (x_dist > influenced_dist && y_dist > influenced_dist && z_dist > influenced_dist)
		{
			return Axis::NONE;
		}

		if (x_dist < y_dist && x_dist < z_dist)
			return Axis::X;
		else if (y_dist < z_dist)
			return Axis::Y;
		return Axis::Z;
	}


	static Axis collideScale(const Matrix& gizmo_mtx,
		const Vec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		const Vec3& origin,
		const Vec3& dir)
	{
		Matrix scale_mtx = Matrix::IDENTITY;
		Vec3 entity_pos = gizmo_mtx.getTranslation();
		float scale = getScale(camera_pos, fov, entity_pos, gizmo_mtx.getXVector().length(), is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Vec3 to_entity_dir = is_ortho ? camera_dir : camera_pos - entity_pos;
		Matrix mtx = gizmo_mtx * scale_mtx;
		Vec3 pos = mtx.getTranslation();

		float x_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getXVector());
		float y_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getYVector());
		float z_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getZVector());

		float influenced_dist = scale * INFLUENCE_DISTANCE;
		if (x_dist > influenced_dist && y_dist > influenced_dist && z_dist > influenced_dist)
		{
			return Axis::NONE;
		}

		if (x_dist < y_dist && x_dist < z_dist)
			return Axis::X;
		else if (y_dist < z_dist)
			return Axis::Y;
		return Axis::Z;
	}


	Axis collideRotate(const Matrix& gizmo_mtx,
		const Vec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		const Vec3& origin,
		const Vec3& dir) const
	{
		Vec3 pos = gizmo_mtx.getTranslation();
		float scale = getScale(camera_pos, fov, pos, gizmo_mtx.getXVector().length(), is_ortho);
		Vec3 hit;
		if (Math::getRaySphereIntersection(origin, dir, pos, scale, hit))
		{
			Vec3 x = gizmo_mtx.getXVector();
			float x_dist = fabs(dotProduct(hit, x) - dotProduct(x, pos));

			Vec3 y = gizmo_mtx.getYVector();
			float y_dist = fabs(dotProduct(hit, y) - dotProduct(y, pos));

			Vec3 z = gizmo_mtx.getZVector();
			float z_dist = fabs(dotProduct(hit, z) - dotProduct(z, pos));

			float influence_dist = scale * 0.15f;
			if (x_dist > influence_dist && y_dist > influence_dist && z_dist > influence_dist)
			{
				return Axis::NONE;
			}

			if (x_dist < y_dist && x_dist < z_dist)
				return Axis::X;
			else if (y_dist < z_dist)
				return Axis::Y;
			else
				return Axis::Z;
		}
		return Axis::NONE;
	}


	bool immediate(Transform& frame) override
	{
		ASSERT(m_immediate_count < MAX_IMMEDIATE);
		Matrix mtx = frame.toMatrix();
		collide(mtx);
		bool ret = transform(frame);
		m_immediate_frames[m_immediate_count] = frame;
		++m_immediate_count;
		return ret;
	}


	void collide(const Matrix& gizmo_mtx)
	{
		if (m_is_dragging) return;

		auto edit_camera = m_editor.getEditCamera();
		auto* render_interface = m_editor.getRenderInterface();
		bool is_ortho = render_interface->isCameraOrtho(edit_camera.entity);
		auto camera_pos = m_editor.getUniverse()->getPosition(edit_camera.entity);
		auto camera_dir = m_editor.getUniverse()->getRotation(edit_camera.entity).rotate(Vec3(0, 0, -1));
		float fov = render_interface->getCameraFOV(edit_camera.entity);

		Vec3 origin, cursor_dir;
		Vec2 mouse_pos = m_editor.getMousePos();
		m_editor.getRenderInterface()->getRay(edit_camera.entity, mouse_pos, origin, cursor_dir);
		
		Axis axis = Axis::NONE;
		switch(m_mode)
		{
			case Mode::TRANSLATE:
				axis = collideTranslate(gizmo_mtx, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
				break;
			case Mode::ROTATE:
				axis = collideRotate(gizmo_mtx, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
				break;
			case Mode::SCALE:
				axis = collideScale(gizmo_mtx, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
				break;
			default:
				ASSERT(false);
				break;
		}
		if (axis != Axis::NONE)
		{
			m_transform_axis = axis;
			m_active = -1;
		}
	}


	void collide(const Vec3& camera_pos, const Vec3& camera_dir, float fov, bool is_ortho)
	{
		if (m_is_dragging) return;

		auto edit_camera = m_editor.getEditCamera();
		Vec3 origin, cursor_dir;
		Vec2 mouse_pos = m_editor.getMousePos();
		m_editor.getRenderInterface()->getRay(edit_camera.entity, mouse_pos, origin, cursor_dir);

		m_transform_axis = Axis::NONE;
		m_active = -1;
		for (int i = 0; i < m_count; ++i)
		{
			Matrix gizmo_mtx = getMatrix(m_entities[i]);

			Axis axis = Axis::NONE;
			switch (m_mode)
			{
				case Mode::TRANSLATE:
					axis = collideTranslate(gizmo_mtx, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
					break;
				case Mode::ROTATE:
					axis = collideRotate(gizmo_mtx, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
					break;
				case Mode::SCALE:
					axis = collideScale(gizmo_mtx, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
					break;
				default:
					ASSERT(false);
					break;
			}
			if (axis != Axis::NONE)
			{
				m_transform_axis = axis;
				m_active = i;
				return;
			}
		}
	}


	Vec3 getMousePlaneIntersection(const Vec2& mouse_pos, const Matrix& gizmo_mtx, Axis transform_axis) const
	{
		auto camera = m_editor.getEditCamera();
		Vec3 origin, dir;
		m_editor.getRenderInterface()->getRay(camera.entity, mouse_pos, origin, dir);
		dir.normalize();
		bool is_two_axed = transform_axis == Axis::XZ || transform_axis == Axis::XY || transform_axis == Axis::YZ;
		if (is_two_axed)
		{
			Vec3 plane_normal;
			switch (transform_axis)
			{
				case Axis::XZ: plane_normal = gizmo_mtx.getYVector(); break;
				case Axis::XY: plane_normal = gizmo_mtx.getZVector(); break;
				case Axis::YZ: plane_normal = gizmo_mtx.getXVector(); break;
				default: ASSERT(false); break;
			}
			float t;
			if (Math::getRayPlaneIntersecion(origin, dir, gizmo_mtx.getTranslation(), plane_normal, t))
			{
				return origin + dir * t;
			}
			return origin;
		}
		Vec3 axis;
		switch (transform_axis)
		{
			case Axis::X: axis = gizmo_mtx.getXVector(); break;
			case Axis::Y: axis = gizmo_mtx.getYVector(); break;
			case Axis::Z: axis = gizmo_mtx.getZVector(); break;
			default: ASSERT(false); return Vec3::ZERO;
		}
		Vec3 pos = gizmo_mtx.getTranslation();
		Vec3 normal = crossProduct(crossProduct(dir, axis), dir);
		float d = dotProduct(origin - pos, normal) / dotProduct(axis, normal);
		return axis * d + pos;
	}


	float computeRotateAngle(int relx, int rely)
	{
		if (relx == 0 && rely == 0) return 0;

		Matrix mtx = getMatrix(m_entities[m_active]);
		Axis plane;
		Vec3 axis;
		switch (m_transform_axis)
		{
			case Axis::X: plane = Axis::YZ; axis = mtx.getXVector(); break;
			case Axis::Y: plane = Axis::XZ; axis = mtx.getYVector(); break;
			case Axis::Z: plane = Axis::XY; axis = mtx.getZVector(); break;
			default: ASSERT(false); return 0;
		}

		Vec3 pos = getMousePlaneIntersection(m_editor.getMousePos(), mtx, plane);
		Vec3 start_pos = getMousePlaneIntersection(m_mouse_pos, mtx, plane);
		Vec3 delta = (pos - mtx.getTranslation()).normalized();
		Vec3 start_delta = (start_pos - mtx.getTranslation()).normalized();
		
		Vec3 side = crossProduct(axis, start_delta);

		float y = Math::clamp(dotProduct(delta, start_delta), -1.0f, 1.0f);
		float x = Math::clamp(dotProduct(delta, side), -1.0f, 1.0f);

		return atan2(x, y);
		/*
		if (m_is_step)
		{
			m_rel_accum.x += relx;
			m_rel_accum.y += rely;
			if (m_rel_accum.x + m_rel_accum.y > 50)
			{
				m_rel_accum.x = m_rel_accum.y = 0;
				return Math::degreesToRadians(float(getStep()));
			}
			else if (m_rel_accum.x + m_rel_accum.y < -50)
			{
				m_rel_accum.x = m_rel_accum.y = 0;
				return -Math::degreesToRadians(float(getStep()));
			}
			else
			{
				return 0;
			}
		}
		return (relx + rely) / 100.0f;*/
	}


	static Axis getPlane(Axis axis)
	{
		switch (axis)
		{
			case Axis::X: return Axis::YZ;
			case Axis::Y: return Axis::XZ;
			case Axis::Z: return Axis::XY;
			default: return axis;
		}
	}


	bool transform(Transform& frame)
	{
		if (m_active >= 0) return false;
		if (m_transform_axis == Axis::NONE) return false;
		if (m_editor.isMouseClick(MouseButton::LEFT))
		{
			m_is_dragging = true;
			m_transform_point = getMousePlaneIntersection(m_editor.getMousePos(), frame.toMatrix(), m_transform_axis);
			m_start_axis_point = m_transform_point;
			m_start_plane_point = getMousePlaneIntersection(m_editor.getMousePos(), frame.toMatrix(), getPlane(m_transform_axis));
			m_start_mouse_pos = m_editor.getMousePos();
			m_angle_accum = 0;
			m_active = -1;
		}

		if (!m_is_dragging) return false;
		if (m_mode == Mode::ROTATE) return rotate(frame.rot);
		if (m_mode == Mode::TRANSLATE) return translate(frame);
		if (m_mode == Mode::SCALE) return scale(frame);
		return false;
	}


	bool scale(Transform& frame) const
	{
		Vec2 mouse_pos = m_editor.getMousePos();
		Matrix frame_mtx = frame.toMatrix();
		Vec3 intersection = getMousePlaneIntersection(mouse_pos, frame_mtx, m_transform_axis);
		Vec2 old_mouse_pos = { mouse_pos.x - m_editor.getMouseRelX(),mouse_pos.y - m_editor.getMouseRelY() };
		Vec3 old_intersection = getMousePlaneIntersection(old_mouse_pos, frame_mtx, m_transform_axis);
		Vec3 delta = intersection - old_intersection;
		if (!m_is_step || delta.length() > float(getStep()))
		{
			if (m_is_step) delta = delta.normalized() * float(getStep());

			frame.scale += delta.length();

			return true;
		}
		return false;
	}


	bool translate(Transform& frame) const
	{
		Vec2 mouse_pos = m_editor.getMousePos();
		Matrix frame_mtx = frame.toMatrix();
		Vec3 intersection = getMousePlaneIntersection(mouse_pos, frame_mtx, m_transform_axis);
		Vec2 old_mouse_pos = {mouse_pos.x - m_editor.getMouseRelX(),mouse_pos.y - m_editor.getMouseRelY()};
		Vec3 old_intersection = getMousePlaneIntersection(old_mouse_pos, frame_mtx, m_transform_axis);
		Vec3 delta = intersection - old_intersection;
		if (!m_is_step || delta.length() > float(getStep()))
		{
			if (m_is_step) delta = delta.normalized() * float(getStep());

			frame.pos += delta;

			return true;
		}
		return false;
	}


	bool rotate(Quat& rot)
	{
		float relx = m_editor.getMouseRelX();
		float rely = m_editor.getMouseRelY();

		if (relx == 0 && rely == 0) return false;

		auto mtx = rot.toMatrix();

		Vec3 axis;
		switch (m_transform_axis)
		{
			case Axis::X: axis = mtx.getXVector(); break;
			case Axis::Y: axis = mtx.getYVector(); break;
			case Axis::Z: axis = mtx.getZVector(); break;
			default: ASSERT(false); break;
		}
		float angle = computeRotateAngle((int)relx, (int)rely);
		m_angle_accum += angle;
		m_angle_accum = Math::angleDiff(m_angle_accum, 0);

		rot = Quat(axis, angle) * rot;
		return true;
	}


	void rotate()
	{
		if (m_active < 0) return;

		float relx = m_editor.getMousePos().x - m_mouse_pos.x;
		float rely = m_editor.getMousePos().y - m_mouse_pos.y;

		Universe* universe = m_editor.getUniverse();
		Array<Vec3> new_positions(m_editor.getAllocator());
		Array<Quat> new_rotations(m_editor.getAllocator());
		auto mtx = getMatrix(m_entities[m_active]);

		Vec3 axis;
		switch (m_transform_axis)
		{
			case Axis::X: axis = mtx.getXVector(); break;
			case Axis::Y: axis = mtx.getYVector(); break;
			case Axis::Z: axis = mtx.getZVector(); break;
			default: ASSERT(false); break;
		}
		float angle = computeRotateAngle((int)relx, (int)rely);
		m_angle_accum += angle;
		m_angle_accum = Math::angleDiff(m_angle_accum, 0);

		if (m_editor.getSelectedEntities()[0] == m_entities[m_active])
		{
			for (int i = 0, c = m_editor.getSelectedEntities().size(); i < c; ++i)
			{
				Vec3 pos = universe->getPosition(m_editor.getSelectedEntities()[i]);
				Quat old_rot = universe->getRotation(m_editor.getSelectedEntities()[i]);
				Quat new_rot = Quat(axis, angle) * old_rot;
				new_rot.normalize();
				new_rotations.push(new_rot);
				Vec3 pdif = mtx.getTranslation() - pos;
				old_rot.conjugate();
				pos = -pdif;
				pos = new_rot.rotate(old_rot.rotate(pos));
				pos += mtx.getTranslation();

				new_positions.push(pos);
			}
			m_editor.setEntitiesPositionsAndRotations(&m_editor.getSelectedEntities()[0],
				&new_positions[0],
				&new_rotations[0],
				new_positions.size());
		}
		else
		{
			Quat old_rot = universe->getRotation(m_entities[m_active]);
			Quat new_rot = Quat(axis, angle) * old_rot;
			new_rot.normalize();
			new_rotations.push(new_rot);
			m_editor.setEntitiesRotations(&m_entities[m_active], &new_rotations[0], 1);
		}
	}


	void scale()
	{
		Matrix gizmo_mtx = getMatrix(m_entities[m_active]);
		Vec3 intersection = getMousePlaneIntersection(m_editor.getMousePos(), gizmo_mtx, m_transform_axis);
		Vec3 delta_vec = intersection - m_transform_point;
		float delta = delta_vec.length();
		Vec3 entity_to_intersection = intersection - gizmo_mtx.getTranslation();
		if (dotProduct(delta_vec, entity_to_intersection) < 0) delta = -delta;
		if (!m_is_step || delta > float(getStep()))
		{
			if (m_is_step) delta = float(getStep());

			Array<float> new_scales(m_editor.getAllocator());
			if (m_entities[m_active] == m_editor.getSelectedEntities()[0])
			{
				for (int i = 0, ci = m_editor.getSelectedEntities().size(); i < ci; ++i)
				{
					float scale = m_editor.getUniverse()->getScale(m_editor.getSelectedEntities()[i]);
					scale += delta;
					new_scales.push(scale);
				}
				m_editor.setEntitiesScales(&m_editor.getSelectedEntities()[0], &new_scales[0], new_scales.size());
			}
			else
			{
				float scale = m_editor.getUniverse()->getScale(m_entities[m_active]);
				scale += delta;
				new_scales.push(scale);
				m_editor.setEntitiesScales(&m_entities[m_active], &new_scales[0], 1);
			}

			m_transform_point = intersection;
		}
	}


	void translate()
	{
		Matrix mtx = getMatrix(m_entities[m_active]);
		Vec3 intersection = getMousePlaneIntersection(m_editor.getMousePos(), mtx, m_transform_axis);
		Vec3 delta = intersection - m_transform_point;
		if (!m_is_step || delta.length() > float(getStep()))
		{
			if (m_is_step) delta = delta.normalized() * float(getStep());

			Array<Vec3> new_positions(m_editor.getAllocator());
			if (m_entities[m_active] == m_editor.getSelectedEntities()[0])
			{
				for (int i = 0, ci = m_editor.getSelectedEntities().size(); i < ci; ++i)
				{
					Vec3 pos = m_editor.getUniverse()->getPosition(m_editor.getSelectedEntities()[i]);
					pos += delta;
					new_positions.push(pos);
				}
				m_editor.setEntitiesPositions(
					&m_editor.getSelectedEntities()[0], &new_positions[0], new_positions.size());
				if (m_is_autosnap_down) m_editor.snapDown();
			}
			else
			{
				Vec3 pos = m_editor.getUniverse()->getPosition(m_entities[m_active]);
				pos += delta;
				new_positions.push(pos);
				m_editor.setEntitiesPositions(&m_entities[m_active], &new_positions[0], 1);
			}

			m_transform_point = intersection;
		}
	}


	void transform()
	{
		if (m_active >= 0 && m_editor.isMouseClick(MouseButton::LEFT))
		{
			Matrix gizmo_mtx = getMatrix(m_entities[m_active]);
			m_transform_point = getMousePlaneIntersection(m_editor.getMousePos(), gizmo_mtx, m_transform_axis);
			m_start_axis_point = m_transform_point;
			m_start_plane_point = getMousePlaneIntersection(m_editor.getMousePos(), gizmo_mtx, getPlane(m_transform_axis));
			m_start_mouse_pos = m_editor.getMousePos();
			m_angle_accum = 0;
			m_is_dragging = true;
		}
		else if (!m_editor.isMouseDown(MouseButton::LEFT))
		{
			m_is_dragging = false;
		}
		if (!m_is_dragging || m_active < 0) return;

		switch (m_mode)
		{
			case Mode::ROTATE: rotate(); break;
			case Mode::TRANSLATE: translate(); break;
			case Mode::SCALE: scale(); break;
			default: ASSERT(false); break;
		}
	}


	void render(const Matrix& gizmo_mtx, bool is_active)
	{
		auto edit_camera = m_editor.getEditCamera();
		auto* render_interface = m_editor.getRenderInterface();
		bool is_ortho = render_interface->isCameraOrtho(edit_camera.entity);
		auto camera_pos = m_editor.getUniverse()->getPosition(edit_camera.entity);
		auto camera_dir = m_editor.getUniverse()->getRotation(edit_camera.entity).rotate(Vec3(0, 0, -1));
		float fov = render_interface->getCameraFOV(edit_camera.entity);

		switch (m_mode)
		{
			case Mode::TRANSLATE:
				renderTranslateGizmo(gizmo_mtx, is_active, camera_pos, camera_dir, fov, is_ortho);
				break;
			case Mode::ROTATE:
				renderRotateGizmo(gizmo_mtx, is_active, camera_pos, camera_dir, fov, is_ortho);
				break;
			case Mode::SCALE:
				renderScaleGizmo(gizmo_mtx, is_active, camera_pos, camera_dir, fov, is_ortho);
				break;
			default:
				ASSERT(false);
				break;
		}
	}


	void render() override
	{
		auto edit_camera = m_editor.getEditCamera();
		auto* render_interface = m_editor.getRenderInterface();
		bool is_ortho = render_interface->isCameraOrtho(edit_camera.entity);
		auto camera_pos = m_editor.getUniverse()->getPosition(edit_camera.entity);
		auto camera_dir = m_editor.getUniverse()->getRotation(edit_camera.entity).rotate(Vec3(0, 0, -1));
		float fov = render_interface->getCameraFOV(edit_camera.entity);

		collide(camera_pos, camera_dir, fov, is_ortho);
		transform();

		for (int i = 0; i < m_count; ++i)
		{
			Matrix gizmo_mtx = getMatrix(m_entities[i]);

			render(gizmo_mtx, m_active == i);
		}

		for (int i = 0; i < m_immediate_count; ++i)
		{
			render(m_immediate_frames[i].toMatrix(), m_active < 0);
		}
		m_immediate_count = 0;

		m_mouse_pos.x = m_editor.getMousePos().x;
		m_mouse_pos.y = m_editor.getMousePos().y;
		m_count = 0;
	}


	Vec3 getOffset() const override
	{
		return m_offset;
	}


	void setOffset(const Vec3& offset) override
	{
		m_pivot = Pivot::OBJECT_PIVOT;
		m_offset = offset;
	}


	void add(Entity entity) override
	{
		if (m_count >= MAX_GIZMOS) return;

		m_entities[m_count] = entity;
		++m_count;
	}


	void setTranslateMode() override
	{
		m_mode = Mode::TRANSLATE;
	}

	void setRotateMode() override
	{
		m_mode = Mode::ROTATE;
	}

	void setScaleMode() override
	{
		m_mode = Mode::SCALE;
	}

	void setPivotCenter() override { m_pivot = Pivot::CENTER; m_offset.set(0, 0, 0); }
	void setPivotOrigin() override { m_pivot = Pivot::OBJECT_PIVOT; }
	bool isPivotCenter() const override { return m_pivot == Pivot::CENTER; }
	bool isPivotOrigin() const override { return m_pivot == Pivot::OBJECT_PIVOT; }
	void setGlobalCoordSystem() override { m_coord_system = CoordSystem::WORLD; }
	void setLocalCoordSystem() override { m_coord_system = CoordSystem::LOCAL; }
	bool isLocalCoordSystem() const override { return m_coord_system == CoordSystem::LOCAL; }
	bool isGlobalCoordSystem() const override { return m_coord_system == CoordSystem::WORLD; }
	int getStep() const override { return m_steps[(int)m_mode]; }
	void enableStep(bool enable) override { m_is_step = enable; }
	void setStep(int step) override { m_steps[(int)m_mode] = step; }
	bool isAutosnapDown() const override { return m_is_autosnap_down; }
	void setAutosnapDown(bool snap) override { m_is_autosnap_down = snap; }
	bool isTranslateMode() const override { return m_mode == Mode::TRANSLATE; }
	bool isScaleMode() const override { return m_mode == Mode::SCALE; }
	bool isRotateMode() const override { return m_mode == Mode::ROTATE; }

	Pivot m_pivot;
	CoordSystem m_coord_system;
	int m_steps[(int)Mode::COUNT];
	Mode m_mode;
	Axis m_transform_axis;
	bool m_is_autosnap_down;
	WorldEditor& m_editor;
	Vec3 m_transform_point;
	Vec3 m_start_axis_point;
	Vec3 m_start_plane_point;
	Vec2 m_start_mouse_pos;
	float m_angle_accum;
	bool m_is_dragging;
	int m_active;
	Vec2 m_mouse_pos;
	Vec2 m_rel_accum;
	bool m_is_step;
	int m_count;
	Entity m_entities[MAX_GIZMOS];
	Transform m_immediate_frames[MAX_IMMEDIATE];
	int m_immediate_count;
	Vec3 m_offset;
};


Gizmo* Gizmo::create(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), GizmoImpl)(editor);
}


void Gizmo::destroy(Gizmo& gizmo)
{
	LUMIX_DELETE(static_cast<GizmoImpl&>(gizmo).m_editor.getAllocator(), &gizmo);
}


} // namespace Lumix
