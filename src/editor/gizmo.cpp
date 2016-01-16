#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "render_interface.h"
#include "renderer/render_scene.h"
#include "universe/universe.h"
#include <cfloat>
#include <cmath>


namespace Lumix
{


static const uint32 RENDERABLE_HASH = crc32("renderable");
static const float INFLUENCE_DISTANCE = 0.3f;
static const uint32 X_COLOR = 0xff6363cf;
static const uint32 Y_COLOR = 0xff63cf63;
static const uint32 Z_COLOR = 0xffcf6363;
static const uint32 SELECTED_COLOR = 0xff63cfcf;


enum class Axis : uint32
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


enum class Mode : uint32
{
	ROTATE,
	TRANSLATE,

	COUNT
};


enum class CoordSystem
{
	LOCAL,
	WORLD
};


struct GizmoImpl : public Gizmo
{
	static const int MAX_GIZMOS = 16;


	GizmoImpl(WorldEditor& editor)
		: m_editor(editor)
	{
		m_mode = Mode::TRANSLATE;
		m_pivot = Pivot::CENTER;
		m_coord_system = CoordSystem::LOCAL;
		m_is_autosnap_down = false;
		m_steps[int(Mode::TRANSLATE)] = 10;
		m_steps[int(Mode::ROTATE)] = 45;
		m_count = 0;
		m_transform_axis = Axis::X;
		m_active = -1;
		m_is_step = false;
		m_relx_accum = m_rely_accum = 0;
		m_is_dragging = false;
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


	Matrix getMatrix(Entity entity)
	{
		Matrix mtx;
		auto* universe = m_editor.getUniverse();
		if (m_pivot == Pivot::OBJECT_PIVOT)
		{
			mtx = universe->getPositionAndRotation(entity);
		}
		else if (m_pivot == Pivot::CENTER)
		{
			mtx = universe->getPositionAndRotation(entity);
			Vec3 center = m_editor.getRenderInterface()->getModelCenter(entity);
			mtx.setTranslation(mtx.multiplyPosition(center));
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


	float getScale(const Vec3& camera_pos, float fov, const Vec3& pos, float entity_scale)
	{
		float scale = tanf(fov * Math::PI / 180 * 0.5f) * (pos - camera_pos).length() * 2;
		return scale / (10 / entity_scale);
	}


	void renderTranslateGizmo(Entity entity, const Vec3& camera_pos, float fov)
	{
		bool is_active = entity == m_entities[m_active];

		Matrix scale_mtx = Matrix::IDENTITY;
		Matrix gizmo_mtx = getMatrix(entity);
		auto entity_pos = gizmo_mtx.getTranslation();
		float scale = getScale(camera_pos, fov, entity_pos, gizmo_mtx.getXVector().length());
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;
			
		Vec3 camera_dir = entity_pos - camera_pos;
		Matrix mtx = gizmo_mtx * scale_mtx;

		RenderInterface::Vertex vertices[9];
		uint16 indices[9];
		vertices[0].position = Vec3(0, 0, 0);
		vertices[0].color = m_transform_axis == Axis::X && is_active ? SELECTED_COLOR : X_COLOR;
		indices[0] = 0;
		vertices[1].position = Vec3(1, 0, 0);
		vertices[1].color = m_transform_axis == Axis::X && is_active ? SELECTED_COLOR : X_COLOR;
		indices[1] = 1;
		vertices[2].position = Vec3(0, 0, 0);
		vertices[2].color = m_transform_axis == Axis::Y && is_active ? SELECTED_COLOR : Y_COLOR;
		indices[2] = 2;
		vertices[3].position = Vec3(0, 1, 0);
		vertices[3].color = m_transform_axis == Axis::Y && is_active ? SELECTED_COLOR : Y_COLOR;
		indices[3] = 3;
		vertices[4].position = Vec3(0, 0, 0);
		vertices[4].color = m_transform_axis == Axis::Z && is_active ? SELECTED_COLOR : Z_COLOR;
		indices[4] = 4;
		vertices[5].position = Vec3(0, 0, 1);
		vertices[5].color = m_transform_axis == Axis::Z && is_active ? SELECTED_COLOR : Z_COLOR;
		indices[5] = 5;

		m_editor.getRenderInterface()->render(mtx, indices, 6, vertices, 6, true);

		if (dotProduct(gizmo_mtx.getXVector(), camera_dir) < 0) mtx.setXVector(-mtx.getXVector());
		if (dotProduct(gizmo_mtx.getYVector(), camera_dir) < 0) mtx.setYVector(-mtx.getYVector());
		if (dotProduct(gizmo_mtx.getZVector(), camera_dir) < 0) mtx.setZVector(-mtx.getZVector());

		vertices[0].position = Vec3(0, 0, 0);
		vertices[0].color = m_transform_axis == Axis::XY && is_active ? SELECTED_COLOR : Z_COLOR;
		indices[0] = 0;
		vertices[1].position = Vec3(0.5f, 0, 0);
		vertices[1].color = m_transform_axis == Axis::XY && is_active ? SELECTED_COLOR : Z_COLOR;
		indices[1] = 1;
		vertices[2].position = Vec3(0, 0.5f, 0);
		vertices[2].color = m_transform_axis == Axis::XY && is_active ? SELECTED_COLOR : Z_COLOR;
		indices[2] = 2;

		vertices[3].position = Vec3(0, 0, 0);
		vertices[3].color = m_transform_axis == Axis::YZ && is_active ? SELECTED_COLOR : X_COLOR;
		indices[3] = 3;
		vertices[4].position = Vec3(0, 0.5f, 0);
		vertices[4].color = m_transform_axis == Axis::YZ && is_active ? SELECTED_COLOR : X_COLOR;
		indices[4] = 4;
		vertices[5].position = Vec3(0, 0, 0.5f);
		vertices[5].color = m_transform_axis == Axis::YZ && is_active ? SELECTED_COLOR : X_COLOR;
		indices[5] = 5;

		vertices[6].position = Vec3(0, 0, 0);
		vertices[6].color = m_transform_axis == Axis::XZ && is_active ? SELECTED_COLOR : Y_COLOR;
		indices[6] = 6;
		vertices[7].position = Vec3(0.5f, 0, 0);
		vertices[7].color = m_transform_axis == Axis::XZ && is_active ? SELECTED_COLOR : Y_COLOR;
		indices[7] = 7;
		vertices[8].position = Vec3(0, 0, 0.5f);
		vertices[8].color = m_transform_axis == Axis::XZ && is_active ? SELECTED_COLOR : Y_COLOR;
		indices[8] = 8;

		m_editor.getRenderInterface()->render(mtx, indices, 9, vertices, 9, false);
	}


	void renderQuarterRing(const Matrix& mtx, const Vec3& a, const Vec3& b, uint32 color)
	{
		RenderInterface::Vertex vertices[1200];
		uint16 indices[1200];
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
	};


	void renderRotateGizmo(Entity entity, const Vec3& camera_pos, float fov)
	{
		bool is_active = entity == m_entities[m_active];

		Matrix scale_mtx = Matrix::IDENTITY;
		Matrix gizmo_mtx = getMatrix(entity);
		auto entity_pos = gizmo_mtx.getTranslation();
		float scale = getScale(camera_pos, fov, entity_pos, gizmo_mtx.getXVector().length());
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Vec3 camera_dir = camera_pos - entity_pos;
		Matrix mtx = gizmo_mtx * scale_mtx;

		Vec3 pos = mtx.getTranslation();
		Vec3 right(1, 0, 0);
		Vec3 up(0, 1, 0);
		Vec3 dir(0, 0, 1);

		if (dotProduct(gizmo_mtx.getXVector(), camera_dir) < 0) right = -right;
		if (dotProduct(gizmo_mtx.getYVector(), camera_dir) < 0) up = -up;
		if (dotProduct(gizmo_mtx.getZVector(), camera_dir) < 0) dir = -dir;
		
		if (!m_is_dragging)
		{
			renderQuarterRing(
				mtx, right, up, m_transform_axis == Axis::Z && is_active ? SELECTED_COLOR : Z_COLOR);
			renderQuarterRing(mtx, up, dir, m_transform_axis == Axis::X && is_active ? SELECTED_COLOR : X_COLOR);
			renderQuarterRing(
				mtx, right, dir, m_transform_axis == Axis::Y && is_active ? SELECTED_COLOR : Y_COLOR);
		}
		else
		{
			Vec3 axis1, axis2;
			switch (m_transform_axis)
			{
				case Axis::X:
					axis1 = up;
					axis2 = dir;
					break;
				case Axis::Y:
					axis1 = right;
					axis2 = dir;
					break;
				case Axis::Z:
					axis1 = right;
					axis2 = up;
					break;
			}
			renderQuarterRing(mtx, axis1, axis2, SELECTED_COLOR);
			renderQuarterRing(mtx, -axis1, axis2, SELECTED_COLOR);
			renderQuarterRing(mtx, -axis1, -axis2, SELECTED_COLOR);
			renderQuarterRing(mtx, axis1, -axis2, SELECTED_COLOR);
		}
	}


	bool isActive() const override
	{
		return m_active >= 0;
	}


	void collide(const Vec3& camera_pos, float fov)
	{
		if (m_is_dragging) return;

		auto edit_camera = m_editor.getEditCamera();
		auto scene = static_cast<RenderScene*>(edit_camera.scene);
		Vec3 origin, cursor_dir;
		scene->getRay(edit_camera.index, m_editor.getMouseX(), m_editor.getMouseY(), origin, cursor_dir);

		m_transform_axis = Axis::NONE;
		m_active = -1;
		for (int i = 0; i < m_count; ++i)
		{
			Matrix scale_mtx = Matrix::IDENTITY;
			Matrix gizmo_mtx = getMatrix(m_entities[i]);
			auto entity_pos = gizmo_mtx.getTranslation();
			float scale = getScale(camera_pos, fov, entity_pos, gizmo_mtx.getXVector().length());
			scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

			Vec3 camera_dir = entity_pos - camera_pos;
			Matrix mtx = gizmo_mtx * scale_mtx;
			Vec3 pos = mtx.getTranslation();

			if (m_mode == Mode::TRANSLATE)
			{
				Matrix triangle_mtx = mtx;

				if (dotProduct(gizmo_mtx.getXVector(), camera_dir) < 0) triangle_mtx.setXVector(-triangle_mtx.getXVector());
				if (dotProduct(gizmo_mtx.getYVector(), camera_dir) < 0) triangle_mtx.setYVector(-triangle_mtx.getYVector());
				if (dotProduct(gizmo_mtx.getZVector(), camera_dir) < 0) triangle_mtx.setZVector(-triangle_mtx.getZVector());

				float t, tmin = FLT_MAX;
				bool hit = Math::getRayTriangleIntersection(
					origin, cursor_dir, pos, pos + triangle_mtx.getXVector() * 0.5f, pos + triangle_mtx.getYVector() * 0.5f, &t);
				if (hit)
				{
					tmin = t;
					m_transform_axis = Axis::XY;
				}
				hit = Math::getRayTriangleIntersection(
					origin, cursor_dir, pos, pos + triangle_mtx.getYVector() * 0.5f, pos + triangle_mtx.getZVector() * 0.5f, &t);
				if (hit && t < tmin)
				{
					tmin = t;
					m_transform_axis = Axis::YZ;
				}
				hit = Math::getRayTriangleIntersection(
					origin, cursor_dir, pos, pos + triangle_mtx.getXVector() * 0.5f, pos + triangle_mtx.getZVector() * 0.5f, &t);
				if (hit && t < tmin)
				{
					m_transform_axis = Axis::XZ;
				}

				if (m_transform_axis != Axis::NONE)
				{
					m_active = i;
					return;
				}

				float x_dist = Math::getLineSegmentDistance(origin, cursor_dir, pos, pos + mtx.getXVector());
				float y_dist = Math::getLineSegmentDistance(origin, cursor_dir, pos, pos + mtx.getYVector());
				float z_dist = Math::getLineSegmentDistance(origin, cursor_dir, pos, pos + mtx.getZVector());

				float influenced_dist = scale * INFLUENCE_DISTANCE;
				if (x_dist > influenced_dist && y_dist > influenced_dist && z_dist > influenced_dist)
				{
					continue;
				}

				if (x_dist < y_dist && x_dist < z_dist) m_transform_axis = Axis::X;
				else if (y_dist < z_dist) m_transform_axis = Axis::Y;
				else m_transform_axis = Axis::Z;

				if (m_transform_axis != Axis::NONE)
				{
					m_active = i;
					return;
				}
			}
			
			if (m_mode == Mode::ROTATE)
			{
				Vec3 hit;
				if (Math::getRaySphereIntersection(origin, cursor_dir, pos, scale, hit))
				{
					auto x = gizmo_mtx.getXVector();
					float x_dist = fabs(dotProduct(hit, x) - dotProduct(x, pos));

					auto y = gizmo_mtx.getYVector();
					float y_dist = fabs(dotProduct(hit, y) - dotProduct(y, pos));

					auto z = gizmo_mtx.getZVector();
					float z_dist = fabs(dotProduct(hit, z) - dotProduct(z, pos));

					float qq = scale * 0.15f;
					if (x_dist > qq && y_dist > qq && z_dist > qq)
					{
						m_transform_axis = Axis::NONE;
						return;
					}

					if (x_dist < y_dist && x_dist < z_dist) m_transform_axis = Axis::X;
					else if (y_dist < z_dist) m_transform_axis = Axis::Y;
					else m_transform_axis = Axis::Z;

					m_active = i;
					return;
				}
			}
		}
	}


	Vec3 getMousePlaneIntersection()
	{
		auto gizmo_mtx = m_editor.getUniverse()->getMatrix(m_entities[m_active]);
		auto camera = m_editor.getEditCamera();
		Vec3 origin, dir;
		auto* scene = static_cast<RenderScene*>(camera.scene);
		scene->getRay(camera.index, m_editor.getMouseX(), m_editor.getMouseY(), origin, dir);
		dir.normalize();
		Matrix camera_mtx = m_editor.getUniverse()->getPositionAndRotation(camera.entity);
		bool is_two_axed = m_transform_axis == Axis::XZ || m_transform_axis == Axis::XY ||
			m_transform_axis == Axis::YZ;
		if (is_two_axed)
		{
			Vec3 plane_normal;
			switch (m_transform_axis)
			{
			case Axis::XZ:
				plane_normal = gizmo_mtx.getYVector();
				break;
			case Axis::XY:
				plane_normal = gizmo_mtx.getZVector();
				break;
			case Axis::YZ:
				plane_normal = gizmo_mtx.getXVector();
				break;
			}
			float t;
			if (Math::getRayPlaneIntersecion(origin, dir, gizmo_mtx.getTranslation(), plane_normal, t))
			{
				return origin + dir * t;
			}
			return origin;
		}
		Vec3 axis;
		switch (m_transform_axis)
		{
		case Axis::X:
			axis = gizmo_mtx.getXVector();
			break;
		case Axis::Y:
			axis = gizmo_mtx.getYVector();
			break;
		case Axis::Z:
			axis = gizmo_mtx.getZVector();
			break;
		}
		Vec3 pos = gizmo_mtx.getTranslation();
		Vec3 normal = crossProduct(crossProduct(dir, axis), dir);
		float d = dotProduct(origin - pos, normal) / dotProduct(axis, normal);
		return axis * d + pos;
	}



	float computeRotateAngle(int relx, int rely)
	{
		if (m_is_step)
		{
			m_relx_accum += relx;
			m_rely_accum += rely;
			if (m_relx_accum + m_rely_accum > 50)
			{
				m_relx_accum = m_rely_accum = 0;
				return Math::degreesToRadians(float(getStep()));
			}
			else if (m_relx_accum + m_rely_accum < -50)
			{
				m_relx_accum = m_rely_accum = 0;
				return -Math::degreesToRadians(float(getStep()));
			}
			else
			{
				return 0;
			}
		}
		return (relx + rely) / 100.0f;
	}


	void rotate()
	{
		if (m_active < 0) return;

		float relx = m_editor.getMouseX() - m_mouse_x;
		float rely = m_editor.getMouseY() - m_mouse_y;

		Universe* universe = m_editor.getUniverse();
		Array<Vec3> new_positions(m_editor.getAllocator());
		Array<Quat> new_rotations(m_editor.getAllocator());
		auto mtx = universe->getMatrix(m_entities[m_active]);
		for (int i = 0, c = m_editor.getSelectedEntities().size(); i < c; ++i)
		{
			Vec3 pos = universe->getPosition(m_editor.getSelectedEntities()[i]);
			Vec3 axis;
			switch (m_transform_axis)
			{
				case Axis::X: axis = mtx.getXVector(); break;
				case Axis::Y: axis = mtx.getYVector(); break;
				case Axis::Z: axis = mtx.getZVector(); break;
			}
			float angle = computeRotateAngle((int)relx, (int)rely);

			Quat old_rot = universe->getRotation(m_editor.getSelectedEntities()[i]);
			Quat new_rot = old_rot * Quat(axis, angle);
			new_rot.normalize();
			new_rotations.push(new_rot);

			Vec3 pdif = mtx.getTranslation() - pos;

			old_rot.conjugate();
			pos = -pdif;
			pos = new_rot * (old_rot * pos);
			pos += mtx.getTranslation();

			new_positions.push(pos);
		}
		m_editor.setEntitiesPositionsAndRotations(&m_editor.getSelectedEntities()[0],
			&new_positions[0],
			&new_rotations[0],
			new_positions.size());
	}


	void translate()
	{
		auto mtx = m_editor.getUniverse()->getMatrix(m_entities[m_active]);
		auto camera = m_editor.getEditCamera();
		Vec3 intersection = getMousePlaneIntersection();
		Vec3 delta = intersection - m_transform_point;
		if (!m_is_step || delta.length() > float(getStep()))
		{
			if (m_is_step) delta = delta.normalized() * float(getStep());

			Array<Vec3> new_positions(m_editor.getAllocator());
			for (int i = 0, ci = m_editor.getSelectedEntities().size(); i < ci; ++i)
			{
				Vec3 pos = m_editor.getUniverse()->getPosition(m_editor.getSelectedEntities()[i]);
				pos += delta;
				new_positions.push(pos);
			}
			m_editor.setEntitiesPositions(&m_editor.getSelectedEntities()[0],
				&new_positions[0],
				new_positions.size());
			if (m_is_autosnap_down) m_editor.snapDown();

			m_transform_point = intersection;
		}
	}


	void transform()
	{
		if (m_active >= 0 && m_editor.isMouseClick(MouseButton::LEFT))
		{
			m_transform_point = getMousePlaneIntersection();
			m_is_dragging = true;
		}
		else if (!m_editor.isMouseDown(MouseButton::LEFT))
		{
			m_is_dragging = false;
		}
		if (!m_is_dragging) return;

		if (m_mode == Mode::ROTATE)
		{
			rotate();
		}
		else
		{
			translate();
		}
	}


	void render() override
	{
		auto edit_camera = m_editor.getEditCamera();
		auto scene = static_cast<RenderScene*>(edit_camera.scene);
		auto camera_pos = m_editor.getUniverse()->getPosition(edit_camera.entity);
		float fov = scene->getCameraFOV(edit_camera.index);

		collide(camera_pos, fov);
		transform();

		for (int i = 0; i < m_count; ++i)
		{
			if (m_mode == Mode::TRANSLATE)
			{
				renderTranslateGizmo(m_entities[i], camera_pos, fov);
			}
			else
			{
				renderRotateGizmo(m_entities[i], camera_pos, fov);
			}
		}
		
		m_mouse_x = m_editor.getMouseX();
		m_mouse_y = m_editor.getMouseY();
		m_count = 0;
	}


	void add(Entity entity) override
	{
		if (m_count >= MAX_GIZMOS) return;

		m_entities[m_count] = entity;
		++m_count;
	}


	void toggleMode() override 
	{
		if (m_mode == Mode::ROTATE)
		{
			m_mode = Mode::TRANSLATE;
		}
		else if (m_mode == Mode::TRANSLATE)
		{
			m_mode = Mode::ROTATE;
		}
		else
		{
			ASSERT(false);
		}
	}


	void togglePivot() override
	{
		if (m_pivot == Pivot::CENTER)
		{
			m_pivot = Pivot::OBJECT_PIVOT;
		}
		else if (m_pivot == Pivot::OBJECT_PIVOT)
		{
			m_pivot = Pivot::CENTER;
		}
		else
		{
			ASSERT(false);
		}
	}


	void toggleCoordSystem() override
	{
		if (m_coord_system == CoordSystem::LOCAL)
		{
			m_coord_system = CoordSystem::WORLD;
		}
		else if (m_coord_system == CoordSystem::WORLD)
		{
			m_coord_system = CoordSystem::LOCAL;
		}
		else
		{
			ASSERT(false);
		}
	}


	int getStep() const override { return m_steps[(int)m_mode]; }
	void setStep(int step) override { m_steps[(int)m_mode] = step; }
	bool isAutosnapDown() const override { return m_is_autosnap_down; }
	void setAutosnapDown(bool snap) override { m_is_autosnap_down = snap; }


	Pivot m_pivot;
	CoordSystem m_coord_system;
	int m_steps[(int)Mode::COUNT];
	Mode m_mode;
	Axis m_transform_axis;
	bool m_is_autosnap_down;
	WorldEditor& m_editor;
	Vec3 m_transform_point;
	bool m_is_dragging;
	int m_active;
	int m_count;
	float m_mouse_x;
	float m_mouse_y;
	float m_relx_accum;
	float m_rely_accum;
	bool m_is_step;
	Entity m_entities[MAX_GIZMOS];
};


Gizmo* Gizmo::create(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), GizmoImpl)(editor);
}


void Gizmo::destroy(Gizmo& gizmo)
{
	LUMIX_DELETE(static_cast<GizmoImpl&>(gizmo).m_editor.getAllocator(), &gizmo);
}


/*


Gizmo::Gizmo(WorldEditor& editor)
	: m_editor(editor)
{
	m_is_autosnap_down = false;
	m_pivot = Pivot::OBJECT_PIVOT;
	m_coord_system = CoordSystem::LOCAL;
	m_is_transforming = false;
	m_mode = Mode::TRANSLATE;
	m_step[int(Mode::TRANSLATE)] = 10;
	m_step[int(Mode::ROTATE)] = 45;
}


Gizmo::~Gizmo()
{
}


void Gizmo::destroy()
{
}


void Gizmo::create()
{
	m_scale = 1;
	m_scene = static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
}


void Gizmo::getMatrix(Matrix& mtx)
{
	getEnityMatrix(mtx, 0);
}


void Gizmo::getEnityMatrix(Matrix& mtx, int selection_index)
{
	Entity entity = m_editor.getSelectedEntities()[selection_index];

	if (m_pivot == Pivot::OBJECT_PIVOT)
	{
		mtx = m_universe->getPositionAndRotation(entity);
	}
	else if (m_pivot == Pivot::CENTER)
	{
		mtx = m_universe->getPositionAndRotation(entity);
		Vec3 center = m_editor.getRenderInterface()->getModelCenter(entity);
		mtx.setTranslation(mtx.multiplyPosition(center));
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
}


void Gizmo::updateScale(ComponentIndex camera)
{
	if (m_editor.getSelectedEntities().empty()) return;

	Entity entity = m_scene->getCameraEntity(camera);
	Vec3 camera_pos = m_universe->getPosition(entity);
	Matrix mtx;
	getMatrix(mtx);
	Vec3 pos = mtx.getTranslation();
	float fov = m_scene->getCameraFOV(camera);
	float scale = tanf(fov * Math::PI / 180 * 0.5f) *
					(mtx.getTranslation() - camera_pos).length() * 2;
	scale /= 10 / mtx.getXVector().length();
	m_scale = scale;
}


void Gizmo::setUniverse(Universe* universe)
{
	m_universe = universe;
}


void Gizmo::toggleCoordSystem()
{
	if (m_coord_system == CoordSystem::LOCAL)
	{
		m_coord_system = CoordSystem::WORLD;
	}
	else if (m_coord_system == CoordSystem::WORLD)
	{
		m_coord_system = CoordSystem::LOCAL;
	}
	else
	{
		ASSERT(false);
	}
}


void Gizmo::togglePivot()
{
	if (m_pivot == Pivot::CENTER)
	{
		m_pivot = Pivot::OBJECT_PIVOT;
	}
	else if (m_pivot == Pivot::OBJECT_PIVOT)
	{
		m_pivot = Pivot::CENTER;
	}
	else
	{
		ASSERT(false);
	}
}


void Gizmo::setCameraRay(const Vec3& origin, const Vec3& cursor_dir)
{
	if (m_editor.getSelectedEntities().empty()) return;
	if (m_is_transforming) return;

	Matrix scale_mtx = Matrix::IDENTITY;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = m_scale;
	Matrix gizmo_mtx;
	getMatrix(gizmo_mtx);
	Matrix mtx = gizmo_mtx * scale_mtx;
	Vec3 pos = mtx.getTranslation();
	m_camera_dir = (origin - pos).normalized();

	m_transform_axis = Axis::NONE;

	if (m_mode == Mode::TRANSLATE)
	{
		Matrix triangle_mtx = mtx;

		if (dotProduct(gizmo_mtx.getXVector(), m_camera_dir) < 0) triangle_mtx.setXVector(-triangle_mtx.getXVector());
		if (dotProduct(gizmo_mtx.getYVector(), m_camera_dir) < 0) triangle_mtx.setYVector(-triangle_mtx.getYVector());
		if (dotProduct(gizmo_mtx.getZVector(), m_camera_dir) < 0) triangle_mtx.setZVector(-triangle_mtx.getZVector());

		float t, tmin = FLT_MAX;
		bool hit = Math::getRayTriangleIntersection(
			origin, cursor_dir, pos, pos + triangle_mtx.getXVector() * 0.5f, pos + triangle_mtx.getYVector() * 0.5f, &t);
		if (hit)
		{
			tmin = t;
			m_transform_axis = Axis::XY;
		}
		hit = Math::getRayTriangleIntersection(
			origin, cursor_dir, pos, pos + triangle_mtx.getYVector() * 0.5f, pos + triangle_mtx.getZVector() * 0.5f, &t);
		if (hit && t < tmin)
		{
			tmin = t;
			m_transform_axis = Axis::YZ;
		}
		hit = Math::getRayTriangleIntersection(
			origin, cursor_dir, pos, pos + triangle_mtx.getXVector() * 0.5f, pos + triangle_mtx.getZVector() * 0.5f, &t);
		if (hit && t < tmin)
		{
			m_transform_axis = Axis::XZ;
		}

		if (m_transform_axis != Axis::NONE) return;

		float x_dist = Math::getLineSegmentDistance(origin, cursor_dir, pos, pos + mtx.getXVector());
		float y_dist = Math::getLineSegmentDistance(origin, cursor_dir, pos, pos + mtx.getYVector());
		float z_dist = Math::getLineSegmentDistance(origin, cursor_dir, pos, pos + mtx.getZVector());

		float influenced_dist = m_scale * INFLUENCE_DISTANCE;
		if (x_dist > influenced_dist && y_dist > influenced_dist && z_dist > influenced_dist)
		{
			m_transform_axis = Axis::NONE;
			return;
		}

		if (x_dist < y_dist && x_dist < z_dist) m_transform_axis = Axis::X;
		else if (y_dist < z_dist) m_transform_axis = Axis::Y;
		else m_transform_axis = Axis::Z;

		return;
	}

	if (m_mode == Mode::ROTATE)
	{
		Vec3 hit;
		if (Math::getRaySphereIntersection(origin, cursor_dir, pos, m_scale, hit))
		{
			auto x = gizmo_mtx.getXVector();
			float x_dist = fabs(dotProduct(hit, x) - dotProduct(x, pos));
			
			auto y = gizmo_mtx.getYVector();
			float y_dist = fabs(dotProduct(hit, y) - dotProduct(y, pos));

			auto z = gizmo_mtx.getZVector();
			float z_dist = fabs(dotProduct(hit, z) - dotProduct(z, pos));

			float qq= m_scale * 0.15f;
			if (x_dist > qq && y_dist > qq && z_dist > qq)
			{
				m_transform_axis = Axis::NONE;
				return;
			}

			if (x_dist < y_dist && x_dist < z_dist) m_transform_axis = Axis::X;
			else if (y_dist < z_dist) m_transform_axis = Axis::Y;
			else m_transform_axis = Axis::Z;
		}
	}
}


bool Gizmo::isHit()
{
	if (m_transform_axis == Axis::NONE) return false;
	if (m_editor.getSelectedEntities().empty()) return false;

	return true;
}


void Gizmo::renderTranslateGizmo()
{
	Matrix scale_mtx = Matrix::IDENTITY;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = m_scale;
	Matrix gizmo_mtx;
	getMatrix(gizmo_mtx);
	Matrix mtx = gizmo_mtx * scale_mtx;

	RenderInterface::Vertex vertices[9];
	uint16 indices[9];
	vertices[0].position = Vec3(0, 0, 0);
	vertices[0].color = m_transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
	indices[0] = 0;
	vertices[1].position = Vec3(1, 0, 0);
	vertices[1].color = m_transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
	indices[1] = 1;
	vertices[2].position = Vec3(0, 0, 0);
	vertices[2].color = m_transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
	indices[2] = 2;
	vertices[3].position = Vec3(0, 1, 0);
	vertices[3].color = m_transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
	indices[3] = 3;
	vertices[4].position = Vec3(0, 0, 0);
	vertices[4].color = m_transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
	indices[4] = 4;
	vertices[5].position = Vec3(0, 0, 1);
	vertices[5].color = m_transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
	indices[5] = 5;

	m_editor.getRenderInterface()->render(mtx, indices, 6, vertices, 6, true);
	
	if (dotProduct(gizmo_mtx.getXVector(), m_camera_dir) < 0) mtx.setXVector(-mtx.getXVector());
	if (dotProduct(gizmo_mtx.getYVector(), m_camera_dir) < 0) mtx.setYVector(-mtx.getYVector());
	if (dotProduct(gizmo_mtx.getZVector(), m_camera_dir) < 0) mtx.setZVector(-mtx.getZVector());

	vertices[0].position = Vec3(0, 0, 0);
	vertices[0].color = m_transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
	indices[0] = 0;
	vertices[1].position = Vec3(0.5f, 0, 0);
	vertices[1].color = m_transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
	indices[1] = 1;
	vertices[2].position = Vec3(0, 0.5f, 0);
	vertices[2].color = m_transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
	indices[2] = 2;

	vertices[3].position = Vec3(0, 0, 0);
	vertices[3].color = m_transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
	indices[3] = 3;
	vertices[4].position = Vec3(0, 0.5f, 0);
	vertices[4].color = m_transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
	indices[4] = 4;
	vertices[5].position = Vec3(0, 0, 0.5f);
	vertices[5].color = m_transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
	indices[5] = 5;

	vertices[6].position = Vec3(0, 0, 0);
	vertices[6].color = m_transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
	indices[6] = 6;
	vertices[7].position = Vec3(0.5f, 0, 0);
	vertices[7].color = m_transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
	indices[7] = 7;
	vertices[8].position = Vec3(0, 0, 0.5f);
	vertices[8].color = m_transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
	indices[8] = 8;

	m_editor.getRenderInterface()->render(mtx, indices, 9, vertices, 9, false);
}


void Gizmo::renderQuarterRing(const Matrix& mtx, const Vec3& a, const Vec3& b, uint32 color)
{
	RenderInterface::Vertex vertices[1200];
	uint16 indices[1200];
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
};


void Gizmo::renderRotateGizmo()
{
	Matrix scale_mtx = Matrix::IDENTITY;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = m_scale;
	Matrix gizmo_mtx;
	getMatrix(gizmo_mtx);
	Matrix mtx = gizmo_mtx * scale_mtx;

	Vec3 pos = mtx.getTranslation();
	Vec3 right(1, 0, 0);
	Vec3 up(0, 1, 0);
	Vec3 dir(0, 0, 1);

	if (dotProduct(gizmo_mtx.getXVector(), m_camera_dir) < 0) right = -right;
	if (dotProduct(gizmo_mtx.getYVector(), m_camera_dir) < 0) up = -up;
	if (dotProduct(gizmo_mtx.getZVector(), m_camera_dir) < 0) dir = -dir;

	if (!m_is_transforming)
	{
		renderQuarterRing(mtx, right, up, m_transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR);
		renderQuarterRing(mtx, up, dir, m_transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR);
		renderQuarterRing(mtx, right, dir, m_transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR);
	}
	else
	{
		Vec3 axis1, axis2;
		switch (m_transform_axis)
		{
		case Axis::X:
			axis1 = up;
			axis2 = dir;
			break;
		case Axis::Y:
			axis1 = right;
			axis2 = dir;
			break;
		case Axis::Z:
			axis1 = right;
			axis2 = up;
			break;
		}
		renderQuarterRing(mtx, axis1, axis2, SELECTED_COLOR);
		renderQuarterRing(mtx, -axis1, axis2, SELECTED_COLOR);
		renderQuarterRing(mtx, -axis1, -axis2, SELECTED_COLOR);
		renderQuarterRing(mtx, axis1, -axis2, SELECTED_COLOR);
	}
}


void Gizmo::render()
{
	if (m_editor.getSelectedEntities().empty()) return;
	if (m_mode == Mode::TRANSLATE)
	{
		renderTranslateGizmo();
	}
	else
	{
		renderRotateGizmo();
	}
}


void Gizmo::stopTransform()
{
	m_is_transforming = false;
}


void Gizmo::startTransform(ComponentIndex camera, int x, int y)
{
	m_is_transforming = m_transform_axis != Axis::NONE;
	m_transform_point = getMousePlaneIntersection(camera, x, y);
	m_relx_accum = m_rely_accum = 0;
}


float Gizmo::computeRotateAngle(int relx, int rely, bool use_step)
{
	if (use_step)
	{
		m_relx_accum += relx;
		m_rely_accum += rely;
		if (m_relx_accum + m_rely_accum > 50)
		{
			m_relx_accum = m_rely_accum = 0;
			return Math::degreesToRadians(float(getStep()));
		}
		else if (m_relx_accum + m_rely_accum < -50)
		{
			m_relx_accum = m_rely_accum = 0;
			return -Math::degreesToRadians(float(getStep()));
		}
		else
		{
			return 0;
		}
	}
	return (relx + rely) / 100.0f;
}

void Gizmo::rotate(int relx, int rely, bool use_step)
{
	Universe* universe = m_editor.getUniverse();
	Array<Vec3> new_positions(m_editor.getAllocator());
	Array<Quat> new_rotations(m_editor.getAllocator());
	for (int i = 0, c = m_editor.getSelectedEntities().size(); i < c; ++i)
	{
		Vec3 pos = universe->getPosition(m_editor.getSelectedEntities()[i]);
		Matrix gizmo_mtx;
		getMatrix(gizmo_mtx);
		Vec3 axis;
		switch (m_transform_axis)
		{
			case Axis::X: axis = gizmo_mtx.getXVector(); break;
			case Axis::Y: axis = gizmo_mtx.getYVector(); break;
			case Axis::Z: axis = gizmo_mtx.getZVector(); break;
		}
		float angle = computeRotateAngle(relx, rely, use_step);

		Quat old_rot = universe->getRotation(m_editor.getSelectedEntities()[i]);
		Quat new_rot = old_rot * Quat(axis, angle);
		new_rot.normalize();
		new_rotations.push(new_rot);

		Vec3 pdif = gizmo_mtx.getTranslation() - pos;

		old_rot.conjugate();
		pos = -pdif;
		pos = new_rot * (old_rot * pos);
		pos += gizmo_mtx.getTranslation();

		new_positions.push(pos);
	}
	m_editor.setEntitiesPositionsAndRotations(&m_editor.getSelectedEntities()[0],
		&new_positions[0],
		&new_rotations[0],
		new_positions.size());
}


void Gizmo::transform(ComponentIndex camera, int x, int y, int relx, int rely, bool use_step)
{
	if (!m_is_transforming) return;

	if (m_mode == Mode::ROTATE)
	{
		rotate(relx, rely, use_step);
	}
	else
	{
		Vec3 intersection = getMousePlaneIntersection(camera, x, y);
		Vec3 delta = intersection - m_transform_point;
		if (!use_step || delta.length() > float(getStep()))
		{
			if (use_step) delta = delta.normalized() * float(getStep());

			Array<Vec3> new_positions(m_editor.getAllocator());
			for (int i = 0, ci = m_editor.getSelectedEntities().size(); i < ci; ++i)
			{
				Vec3 pos = m_editor.getUniverse()->getPosition(m_editor.getSelectedEntities()[i]);
				pos += delta;
				new_positions.push(pos);
			}
			m_editor.setEntitiesPositions(&m_editor.getSelectedEntities()[0],
				&new_positions[0],
				new_positions.size());
			if (m_is_autosnap_down) m_editor.snapDown();

			m_transform_point = intersection;
		}
	}
}


Vec3 Gizmo::getMousePlaneIntersection(ComponentIndex camera, int x, int y)
{
	Vec3 origin, dir;
	m_scene->getRay(camera, (float)x, (float)y, origin, dir);
	dir.normalize();
	Entity camera_entity = m_scene->getCameraEntity(camera);
	Matrix camera_mtx = m_universe->getPositionAndRotation(camera_entity);
	Matrix gizmo_mtx;
	getMatrix(gizmo_mtx);
	bool is_two_axed = m_transform_axis == Axis::XZ || m_transform_axis == Axis::XY ||
					   m_transform_axis == Axis::YZ;
	if (is_two_axed)
	{
		Vec3 plane_normal;
		switch (m_transform_axis)
		{
		case Axis::XZ:
			plane_normal = gizmo_mtx.getYVector();
			break;
		case Axis::XY:
			plane_normal = gizmo_mtx.getZVector();
			break;
		case Axis::YZ:
			plane_normal = gizmo_mtx.getXVector();
			break;
		}
		float t;
		if (Math::getRayPlaneIntersecion(origin, dir, gizmo_mtx.getTranslation(), plane_normal, t))
		{
			return origin + dir * t;
		}
		return origin;
	}
	Vec3 axis;
	switch (m_transform_axis)
	{
		case Axis::X:
			axis = gizmo_mtx.getXVector();
			break;
		case Axis::Y:
			axis = gizmo_mtx.getYVector();
			break;
		case Axis::Z:
			axis = gizmo_mtx.getZVector();
			break;
	}
	Vec3 pos = gizmo_mtx.getTranslation();
	Vec3 normal = crossProduct(crossProduct(dir, axis), dir);
	float d = dotProduct(origin - pos, normal) / dotProduct(axis, normal);
	return axis * d + pos;
}


*/


} // !namespace Lumix
