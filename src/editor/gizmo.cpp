#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/transient_geometry.h"
#include "universe/universe.h"
#include <cfloat>


namespace Lumix
{


static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const float INFLUENCE_DISTANCE = 0.3f;


struct Vertex
{
	Vec3 position;
	uint32_t color;
	float u, v;
};


Gizmo::Gizmo(WorldEditor& editor)
	: m_editor(editor)
{
	m_vertex_decl.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.end();

	m_shader = nullptr;
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
	m_editor.getEngine().getResourceManager().get(ResourceManager::SHADER)->unload(*m_shader);
}


void Gizmo::create()
{
	m_scale = 1;
	m_shader = static_cast<Shader*>(m_editor.getEngine()
									  .getResourceManager()
									  .get(ResourceManager::SHADER)
									  ->load(Path("shaders/debugline.shd")));
	m_scene = static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
}


void Gizmo::getMatrix(Matrix& mtx)
{
	getEnityMatrix(mtx, 0);
}


void Gizmo::getEnityMatrix(Matrix& mtx, int selection_index)
{
	if (m_pivot == Pivot::OBJECT_PIVOT)
	{
		mtx = m_universe->getMatrix(
			m_editor.getSelectedEntities()[selection_index]);
	}
	else if (m_pivot == Pivot::CENTER)
	{
		mtx = m_universe->getMatrix(
			m_editor.getSelectedEntities()[selection_index]);
		ComponentIndex cmp = m_scene->getRenderableComponent(
			m_editor.getSelectedEntities()[selection_index]);
		if (cmp >= 0)
		{
			Model* model = m_scene->getRenderableModel(cmp);
			Vec3 center =
				(model->getAABB().getMin() + model->getAABB().getMax()) * 0.5f;
			mtx.translate(mtx * center);
		}
		else
		{
			mtx = m_universe->getMatrix(
				m_editor.getSelectedEntities()[selection_index]);
		}
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
	if (!m_editor.getSelectedEntities().empty())
	{
		Entity entity = m_scene->getCameraEntity(camera);
		Vec3 camera_pos = m_universe->getPosition(entity);
		Matrix mtx;
		getMatrix(mtx);
		Vec3 pos = mtx.getTranslation();
		float fov = m_scene->getCameraFOV(camera);
		float scale = tanf(fov * Math::PI / 180 * 0.5f) *
					  (mtx.getTranslation() - camera_pos).length() * 2;
		scale /= 10 * mtx.getXVector().length();
		m_scale = scale;
	}
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

		float t, tmin = FLT_MAX;
		bool hit = Math::getRayTriangleIntersection(
			origin, cursor_dir, pos, pos + mtx.getXVector() * 0.5f, pos + mtx.getYVector() * 0.5f, &t);
		if (hit)
		{
			tmin = t;
			m_transform_axis = Axis::XY;
		}
		hit = Math::getRayTriangleIntersection(
			origin, cursor_dir, pos, pos + mtx.getYVector() * 0.5f, pos + mtx.getZVector() * 0.5f, &t);
		if (hit && t < tmin)
		{
			tmin = t;
			m_transform_axis = Axis::YZ;
		}
		hit = Math::getRayTriangleIntersection(
			origin, cursor_dir, pos, pos + mtx.getXVector() * 0.5f, pos + mtx.getZVector() * 0.5f, &t);
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


bool Gizmo::castRay(const Vec3& origin, const Vec3& dir)
{
	if (m_transform_axis == Axis::NONE) return false;
	if (m_editor.getSelectedEntities().empty()) return false;

	Matrix scale_mtx = Matrix::IDENTITY;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = m_scale;
	Matrix gizmo_mtx;
	getMatrix(gizmo_mtx);
	Matrix mtx = gizmo_mtx * scale_mtx;
	Vec3 pos = mtx.getTranslation();

	if (m_mode == Mode::TRANSLATE)
	{
		float x_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getXVector());
		float y_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getYVector());
		float z_dist = Math::getLineSegmentDistance(origin, dir, pos, pos + mtx.getZVector());

		float influenced_dist = m_scale * INFLUENCE_DISTANCE;
		return x_dist < influenced_dist || y_dist < influenced_dist || z_dist < influenced_dist;
	}

	if (m_mode == Mode::ROTATE)
	{
		Vec3 hit;
		return Math::getRaySphereIntersection(origin, dir, pos, m_scale * 1.1f, hit);
	}

	return false;
}


void Gizmo::renderTranslateGizmo(PipelineInstance& pipeline)
{
	if (!m_shader->isReady()) return;

	Matrix scale_mtx = Matrix::IDENTITY;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = m_scale;
	Matrix gizmo_mtx;
	getMatrix(gizmo_mtx);
	Matrix mtx = gizmo_mtx * scale_mtx;
	
	Vertex vertices[9];
	uint16_t indices[9];
	vertices[0].position = Vec3(0, 0, 0);
	vertices[0].color = m_transform_axis == Axis::X ? 0xff00ffff : 0xff0000ff;
	indices[0] = 0;
	vertices[1].position = Vec3(1, 0, 0);
	vertices[1].color = m_transform_axis == Axis::X ? 0xff00ffff : 0xff0000ff;
	indices[1] = 1;
	vertices[2].position = Vec3(0, 0, 0);
	vertices[2].color = m_transform_axis == Axis::Y ? 0xff00ffff : 0xff00ff00;
	indices[2] = 2;
	vertices[3].position = Vec3(0, 1, 0);
	vertices[3].color = m_transform_axis == Axis::Y ? 0xff00ffff : 0xff00ff00;
	indices[3] = 3;
	vertices[4].position = Vec3(0, 0, 0);
	vertices[4].color = m_transform_axis == Axis::Z ? 0xff00ffff : 0xffff0000;
	indices[4] = 4;
	vertices[5].position = Vec3(0, 0, 1);
	vertices[5].color = m_transform_axis == Axis::Z ? 0xff00ffff : 0xffff0000;
	indices[5] = 5;

	Lumix::TransientGeometry geom(vertices, 6, m_vertex_decl, indices, 6);
	pipeline.render(geom,
		mtx,
		BGFX_STATE_PT_LINES | BGFX_STATE_DEPTH_TEST_LEQUAL,
		m_shader->getInstance(0).m_program_handles[pipeline.getPassIdx()]);


	vertices[0].position = Vec3(0, 0, 0);
	vertices[0].color = m_transform_axis == Axis::XY ? 0xff00ffff : 0xffff0000;
	indices[0] = 0;
	vertices[1].position = Vec3(0.5f, 0, 0);
	vertices[1].color = m_transform_axis == Axis::XY ? 0xff00ffff : 0xffff0000;
	indices[1] = 1;
	vertices[2].position = Vec3(0, 0.5f, 0);
	vertices[2].color = m_transform_axis == Axis::XY ? 0xff00ffff : 0xffff0000;
	indices[2] = 2;

	vertices[3].position = Vec3(0, 0, 0);
	vertices[3].color = m_transform_axis == Axis::YZ ? 0xff00ffff : 0xff0000ff;
	indices[3] = 3;
	vertices[4].position = Vec3(0, 0.5f, 0);
	vertices[4].color = m_transform_axis == Axis::YZ ? 0xff00ffff : 0xff0000ff;
	indices[4] = 4;
	vertices[5].position = Vec3(0, 0, 0.5f);
	vertices[5].color = m_transform_axis == Axis::YZ ? 0xff00ffff : 0xff0000ff;
	indices[5] = 5;

	vertices[6].position = Vec3(0, 0, 0);
	vertices[6].color = m_transform_axis == Axis::XZ ? 0xff00ffff : 0xff00ff00;
	indices[6] = 6;
	vertices[7].position = Vec3(0.5f, 0, 0);
	vertices[7].color = m_transform_axis == Axis::XZ ? 0xff00ffff : 0xff00ff00;
	indices[7] = 7;
	vertices[8].position = Vec3(0, 0, 0.5f);
	vertices[8].color = m_transform_axis == Axis::XZ ? 0xff00ffff : 0xff00ff00;
	indices[8] = 8;

	Lumix::TransientGeometry geom2(vertices, 9, m_vertex_decl, indices, 9);
	auto program_handle = m_shader->getInstance(0).m_program_handles[pipeline.getPassIdx()];
	pipeline.render(geom2, mtx, BGFX_STATE_DEPTH_TEST_LEQUAL, program_handle);
}


void Gizmo::renderQuarterRing(PipelineInstance& pipeline, const Matrix& mtx, const Vec3& a, const Vec3& b, uint32_t color)
{
	Vertex vertices[1200];
	uint16_t indices[1200];
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

	Lumix::TransientGeometry ring_geom(vertices, offset, m_vertex_decl, indices, offset);
	pipeline.render(ring_geom,
		mtx,
		BGFX_STATE_DEPTH_TEST_LEQUAL,
		m_shader->getInstance(0).m_program_handles[pipeline.getPassIdx()]);

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
	Lumix::TransientGeometry plane_geom(vertices, offset, m_vertex_decl, indices, offset);
	pipeline.render(plane_geom,
		mtx,
		BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_PT_LINES,
		m_shader->getInstance(0).m_program_handles[pipeline.getPassIdx()]);

};


void Gizmo::renderRotateGizmo(PipelineInstance& pipeline)
{
	if (!m_shader->isReady()) return;

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

	const uint32_t SELECTED_COLOR = 0xff00ffff;

	if (!m_is_transforming)
	{
		renderQuarterRing(pipeline, mtx, right, up, m_transform_axis == Axis::Z ? SELECTED_COLOR : 0xffff0000);
		renderQuarterRing(pipeline, mtx, up, dir, m_transform_axis == Axis::X ? SELECTED_COLOR : 0xff0000ff);
		renderQuarterRing(pipeline, mtx, right, dir, m_transform_axis == Axis::Y ? SELECTED_COLOR : 0xff00ff00);
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
		renderQuarterRing(pipeline, mtx, axis1, axis2, SELECTED_COLOR);
		renderQuarterRing(pipeline, mtx, -axis1, axis2, SELECTED_COLOR);
		renderQuarterRing(pipeline, mtx, -axis1, -axis2, SELECTED_COLOR);
		renderQuarterRing(pipeline, mtx, axis1, -axis2, SELECTED_COLOR);
	}
}


void Gizmo::render(PipelineInstance& pipeline)
{
	if (m_editor.getSelectedEntities().empty()) return;
	if (m_mode == Mode::TRANSLATE)
	{
		renderTranslateGizmo(pipeline);
	}
	else
	{
		renderRotateGizmo(pipeline);
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
	Matrix camera_mtx = m_universe->getMatrix(camera_entity);
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


} // !namespace Lumix
