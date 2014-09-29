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
#include "graphics/irender_device.h"
#include "graphics/model.h"
#include "graphics/renderer.h"
#include "universe/universe.h"


namespace Lumix
{


Gizmo::Gizmo(WorldEditor& editor)
	: m_editor(editor)
{
	m_model = NULL;
}


Gizmo::~Gizmo()
{
}


void Gizmo::destroy()
{
	m_renderer->getEngine().getResourceManager().get(ResourceManager::MODEL)->unload(*m_model);
}


void Gizmo::create(Renderer& renderer)
{
	m_scale = 1;
	m_renderer = &renderer;
	m_model = static_cast<Model*>(renderer.getEngine().getResourceManager().get(ResourceManager::MODEL)->load("models/editor/gizmo.msh"));
}


void Gizmo::getMatrix(Matrix& mtx)
{
	m_editor.getSelectedEntities()[0].getMatrix(mtx);
}


void Gizmo::updateScale(Component camera)
{
	if (!m_editor.getSelectedEntities().empty())
	{
		Matrix camera_mtx;
		camera.entity.getMatrix(camera_mtx);
		Matrix mtx;
		getMatrix(mtx);
		Vec3 pos = mtx.getTranslation();
		float fov = static_cast<RenderScene*>(camera.scene)->getCameraFOV(camera);
		float scale = tanf(fov * Math::PI / 180 * 0.5f) * (mtx.getTranslation() - camera_mtx.getTranslation()).length() * 2;
		scale /= 20 * mtx.getXVector().length();
		m_scale = scale;
	}
}


void Gizmo::setUniverse(Universe* universe)
{
	m_universe = universe;
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


void Gizmo::render(Renderer& renderer, IRenderDevice& render_device)
{
	if(!m_editor.getSelectedEntities().empty())
	{
		Matrix scale_mtx = Matrix::IDENTITY;
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = m_scale;
		Matrix gizmo_mtx;
		getMatrix(gizmo_mtx);
		Matrix mtx = gizmo_mtx * scale_mtx;
		renderer.renderModel(*m_model, mtx, render_device.getPipeline());
	}
}


void Gizmo::startTransform(Component camera, int x, int y, TransformMode mode)
{
	m_transform_mode = mode;
	m_transform_point = getMousePlaneIntersection(camera, x, y);
	m_relx_accum = m_rely_accum = 0;
}


void Gizmo::transform(Component camera, TransformOperation operation, int x, int y, int relx, int rely, int flags)
{
	if(operation == TransformOperation::ROTATE && m_transform_mode != TransformMode::CAMERA_XZ)
	{
		Array<Vec3> new_positions;
		Array<Quat> new_rotations;
		for(int i = 0, c = m_editor.getSelectedEntities().size(); i < c; ++i)
		{
			Vec3 pos = m_editor.getSelectedEntities()[i].getPosition();
			Matrix emtx;
			m_editor.getSelectedEntities()[i].getMatrix(emtx);
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
			float angle = 0;
			if(flags & Flags::FIXED_STEP)
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
			Quat new_rot = m_editor.getSelectedEntities()[i].getRotation() * Quat(axis, angle);
			new_rot.normalize();
			new_rotations.push(new_rot);
			new_positions.push(pos);
		}
		m_editor.setEntityPositionAndRotaion(m_editor.getSelectedEntities(), new_positions, new_rotations);
	}
	else
	{
		Vec3 intersection = getMousePlaneIntersection(camera, x, y);
		Vec3 delta = intersection - m_transform_point;
		Array<Vec3> new_positions;
		for(int i = 0, ci = m_editor.getSelectedEntities().size(); i < ci; ++i)
		{
			Vec3 pos = m_editor.getSelectedEntities()[i].getPosition();
			pos += delta;
			new_positions.push(pos);
		}
		m_editor.setEntitiesPositions(m_editor.getSelectedEntities(), new_positions);
		m_transform_point = intersection;
	}
}


Vec3 Gizmo::getMousePlaneIntersection(Component camera, int x, int y)
{
	Vec3 origin, dir;
	RenderScene* scene = static_cast<RenderScene*>(camera.scene);
	scene->getRay(camera, (float)x, (float)y, origin, dir);
	dir.normalize();
	Matrix camera_mtx;
	camera.entity.getMatrix(camera_mtx);
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
