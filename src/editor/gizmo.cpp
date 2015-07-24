//#define _USE_MATH_DEFINES
//#include <cmath>
#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/render_scene.h"
#include "universe/universe.h"


namespace Lumix
{


static const uint32_t RENDERABLE_HASH = crc32("renderable");


Gizmo::Gizmo(WorldEditor& editor)
	: m_editor(editor)
{
	m_model = NULL;
	m_pivot_mode = PivotMode::OBJECT_PIVOT;
	m_coord_system = CoordSystem::LOCAL;
}


Gizmo::~Gizmo()
{
}


void Gizmo::destroy()
{
	m_editor.getEngine().getResourceManager().get(ResourceManager::MODEL)->unload(*m_model);
}


void Gizmo::create()
{
	m_scale = 1;
	m_model = static_cast<Model*>(m_editor.getEngine().getResourceManager().get(ResourceManager::MODEL)->load(Path("models/editor/gizmo.msh")));
}


void Gizmo::getMatrix(Matrix& mtx)
{
	getEnityMatrix(mtx, 0);
}


void Gizmo::getEnityMatrix(Matrix& mtx, int selection_index)
{
	if(m_pivot_mode == PivotMode::OBJECT_PIVOT)
	{
		mtx = m_universe->getMatrix(
			m_editor.getSelectedEntities()[selection_index]);
	}
	else if(m_pivot_mode == PivotMode::CENTER)
	{
		mtx = m_universe->getMatrix(
			m_editor.getSelectedEntities()[selection_index]);
		ComponentOld cmp = m_editor.getComponent(m_editor.getSelectedEntities()[selection_index], RENDERABLE_HASH);
		if(cmp.isValid())
		{
			Model* model = static_cast<RenderScene*>(cmp.scene)->getRenderableModel(cmp.index);
			Vec3 center = (model->getAABB().getMin() + model->getAABB().getMax()) * 0.5f;
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

	if(m_coord_system == CoordSystem::WORLD)
	{
		Vec3 pos = mtx.getTranslation();
		mtx = Matrix::IDENTITY;
		mtx.setTranslation(pos);
	}
}


void Gizmo::updateScale(ComponentOld camera)
{
	if (!m_editor.getSelectedEntities().empty())
	{
		Vec3 camera_pos = m_universe->getPosition(camera.entity);
		Matrix mtx;
		getMatrix(mtx);
		Vec3 pos = mtx.getTranslation();
		float fov = static_cast<RenderScene*>(camera.scene)->getCameraFOV(camera.index);
		float scale = tanf(fov * Math::PI / 180 * 0.5f) * (mtx.getTranslation() - camera_pos).length() * 2;
		scale /= 20 * mtx.getXVector().length();
		m_scale = scale;
	}
}


void Gizmo::setUniverse(Universe* universe)
{
	m_universe = universe;
}


void Gizmo::toggleCoordSystem()
{
	if(m_coord_system == CoordSystem::LOCAL)
	{
		m_coord_system = CoordSystem::WORLD;
	}
	else if(m_coord_system == CoordSystem::WORLD)
	{
		m_coord_system = CoordSystem::LOCAL;
	}
	else
	{
		ASSERT(false);
	}
}


void Gizmo::togglePivotMode()
{
	if(m_pivot_mode == PivotMode::CENTER)
	{
		m_pivot_mode = PivotMode::OBJECT_PIVOT;
	}
	else if(m_pivot_mode == PivotMode::OBJECT_PIVOT)
	{
		m_pivot_mode = PivotMode::CENTER;
	}
	else
	{
		ASSERT(false);
	}
}


RayCastModelHit Gizmo::castRay(const Vec3& origin, const Vec3& dir)
{
	if (!m_editor.getSelectedEntities().empty())
	{
		Matrix mtx;
		getMatrix(mtx);
		return m_model->castRay(origin, dir, mtx, m_scale);
	}
	RayCastModelHit hit;
	hit.m_is_hit = false;
	return hit;
}


void Gizmo::render(PipelineInstance& pipeline)
{
	if(!m_editor.getSelectedEntities().empty())
	{
		Matrix scale_mtx = Matrix::IDENTITY;
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = m_scale;
		Matrix gizmo_mtx;
		getMatrix(gizmo_mtx);
		Matrix mtx = gizmo_mtx * scale_mtx;
		pipeline.renderModel(*m_model, mtx);
	}
}


void Gizmo::startTransform(ComponentOld camera, int x, int y, TransformMode mode)
{
	m_transform_mode = mode;
	m_transform_point = getMousePlaneIntersection(camera, x, y);
	m_relx_accum = m_rely_accum = 0;
}



float Gizmo::computeRotateAngle(int relx, int rely, int flags)
{
	float angle = 0;
	if(flags & (int)Flags::FIXED_STEP)
	{
		m_relx_accum += relx;
		m_rely_accum += rely;
		if(m_relx_accum + m_rely_accum > 50)
		{
			angle = (float)Math::PI / 4;
			m_relx_accum = m_rely_accum = 0;
		}
		else if(m_relx_accum + m_rely_accum < -50)
		{
			angle = -(float)Math::PI / 4;
			m_relx_accum = m_rely_accum = 0;
		}
		else 
		{
			angle = 0;
		}
	}
	else
	{
		angle = (relx + rely) / 100.0f;
	}
	return angle;
}

void Gizmo::rotate(int relx, int rely, int flags)
{
	Universe* universe = m_editor.getUniverse();
	Array<Vec3> new_positions(m_editor.getAllocator());
	Array<Quat> new_rotations(m_editor.getAllocator());
	for(int i = 0, c = m_editor.getSelectedEntities().size(); i < c; ++i)
	{
		Vec3 pos = universe->getPosition(m_editor.getSelectedEntities()[i]);
		Matrix emtx;
		getEnityMatrix(emtx, i);
		Vec3 axis;
		switch(m_transform_mode)
		{
			case TransformMode::X:
				axis = emtx.getXVector();
				break;
			case TransformMode::Y:
				axis = emtx.getYVector();
				break;
			case TransformMode::Z:
				axis = emtx.getZVector();
				break;
		}
		float angle = computeRotateAngle(relx, rely, flags);
		
		Quat old_rot = universe->getRotation(m_editor.getSelectedEntities()[i]);
		Quat new_rot = old_rot * Quat(axis, angle);
		new_rot.normalize();
		new_rotations.push(new_rot);

		Vec3 pdif = emtx.getTranslation() - pos;
		
		old_rot.conjugate();
		pos = -pdif;
		pos = new_rot * (old_rot * pos);
		pos += emtx.getTranslation();

		new_positions.push(pos);
	}
	m_editor.setEntityPositionAndRotaion(m_editor.getSelectedEntities(), new_positions, new_rotations);
}


void Gizmo::transform(ComponentOld camera, TransformOperation operation, int x, int y, int relx, int rely, int flags)
{
	if(operation == TransformOperation::ROTATE && m_transform_mode != TransformMode::CAMERA_XZ)
	{
		rotate(relx, rely, flags);
	}
	else
	{
		Vec3 intersection = getMousePlaneIntersection(camera, x, y);
		Vec3 delta = intersection - m_transform_point;
		Array<Vec3> new_positions(m_editor.getAllocator());
		for(int i = 0, ci = m_editor.getSelectedEntities().size(); i < ci; ++i)
		{
			Vec3 pos = m_editor.getUniverse()->getPosition(m_editor.getSelectedEntities()[i]);
			pos += delta;
			new_positions.push(pos);
		}
		m_editor.setEntitiesPositions(m_editor.getSelectedEntities(), new_positions);
		m_transform_point = intersection;
	}
}


Vec3 Gizmo::getMousePlaneIntersection(ComponentOld camera, int x, int y)
{
	Vec3 origin, dir;
	RenderScene* scene = static_cast<RenderScene*>(camera.scene);
	scene->getRay(camera.index, (float)x, (float)y, origin, dir);
	dir.normalize();
	Matrix camera_mtx = m_universe->getMatrix(camera.entity);
	Matrix gizmo_mtx;
	getMatrix(gizmo_mtx);
	if(m_transform_mode == TransformMode::CAMERA_XZ)
	{
		Vec3 a = crossProduct(Vec3(0, 1, 0), camera_mtx.getXVector());
		Vec3 b = crossProduct(Vec3(0, 1, 0), camera_mtx.getZVector());
		Vec3 plane_normal = crossProduct(a, b);
		float t;
		if (Math::getRayPlaneIntersecion(origin, dir, gizmo_mtx.getTranslation(), plane_normal, t))
		{
			return origin + dir * t;
		}
		return origin;
	}
	Vec3 axis;
	switch(m_transform_mode)
	{
		case TransformMode::X:
			axis = gizmo_mtx.getXVector();
			break;
		case TransformMode::Y:
			axis = gizmo_mtx.getYVector();
			break;
		case TransformMode::Z:
			axis = gizmo_mtx.getZVector();
			break;
	}
	Vec3 pos = gizmo_mtx.getTranslation();
	Vec3 normal = crossProduct(crossProduct(dir, axis), dir);
	float d = dotProduct(origin - pos, normal) / dotProduct(axis, normal);
	return axis * d + pos;
}


} // !namespace Lumix
