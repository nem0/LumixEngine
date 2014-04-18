//#define _USE_MATH_DEFINES
//#include <cmath>
#include "core/crc32.h"
#include "core/event_manager.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "editor/gizmo.h"
#include "graphics/irender_device.h"
#include "graphics/model.h"
#include "graphics/renderer.h"
#include "universe/entity_moved_event.h"
#include "universe/universe.h"


namespace Lux
{


Gizmo::Gizmo()
{
//	m_handle = 0;
	m_selected_entity.index = -1;
}


Gizmo::~Gizmo()
{
}


void Gizmo::destroy()
{
	//h3dRemoveNode(m_handle);
}


void Gizmo::create(Renderer& renderer)
{
	m_scale = 1;
	m_renderer = &renderer;
	m_model = renderer.getModel("models/gizmo.msh");
}


void Gizmo::hide()
{
	//h3dSetNodeFlags(m_handle, h3dGetNodeFlags(m_handle) | H3DNodeFlags::Inactive, true);
}


void Gizmo::show()
{
	/*h3dSetNodeFlags(m_handle, h3dGetNodeFlags(m_handle) & ~H3DNodeFlags::Inactive, true);
	h3dSetNodeFlags(m_handle, h3dGetNodeFlags(m_handle) | H3DNodeFlags::NoCastShadow, true);	*/
}


void Gizmo::getMatrix(Matrix& mtx)
{
	m_selected_entity.getMatrix(mtx);
}


void Gizmo::updateScale(Component camera)
{
	if (m_selected_entity.isValid())
	{
		Matrix camera_mtx;
		camera.entity.getMatrix(camera_mtx);
		Matrix mtx;
		getMatrix(mtx);
		Vec3 pos = mtx.getTranslation();
		float fov;
		m_renderer->getCameraFov(camera, fov);
		float scale = tanf(fov * Math::PI / 180 * 0.5f) * (mtx.getTranslation() - camera_mtx.getTranslation()).length() * 2;
		scale /= 20 * mtx.getXVector().length();
		m_scale = scale;
	}
}


void Gizmo::setEntity(Entity entity)
{
	//h3dSetNodeFlags(m_handle, h3dGetNodeFlags(m_handle) | H3DNodeFlags::NoCastShadow, true);	
	m_selected_entity = entity;
	if(m_selected_entity.index != -1)
	{
		show();
	}
	else
	{
		hide();
	}
}


void Gizmo::setUniverse(Universe* universe)
{
	m_universe = universe;
}


RayCastModelHit Gizmo::castRay(const Vec3& origin, const Vec3& dir)
{
	if (m_selected_entity.isValid())
	{
		return m_model->castRay(origin, dir, m_selected_entity.getMatrix(), m_scale);
	}
	RayCastModelHit hit;
	hit.m_is_hit = false;
	return hit;
}


void Gizmo::render(Renderer& renderer, IRenderDevice& render_device)
{
	if(m_selected_entity.isValid())
	{
		Matrix scale_mtx = Matrix::IDENTITY;
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = m_scale;
		Matrix mtx = m_selected_entity.getMatrix() * scale_mtx;
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
	if(m_selected_entity.index != -1)
	{
		if(operation == TransformOperation::ROTATE && m_transform_mode != TransformMode::CAMERA_XZ)
		{
			Vec3 pos = m_selected_entity.getPosition();
			Matrix emtx;
			m_selected_entity.getMatrix(emtx);
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
			m_selected_entity.setRotation(m_selected_entity.getRotation() * Quat(axis, angle));
			m_selected_entity.setPosition(pos);
		}
		else
		{
			Vec3 intersection = getMousePlaneIntersection(camera, x, y);
			Vec3 delta = intersection - m_transform_point;
			m_transform_point = intersection;
			Matrix mtx;
			m_selected_entity.getMatrix(mtx);
			mtx.translate(delta);
			m_selected_entity.setMatrix(mtx);
		}
	}
}


Vec3 Gizmo::getMousePlaneIntersection(Component camera, int x, int y)
{
	Vec3 origin, dir;
	m_renderer->getRay(camera, (float)x, (float)y, origin, dir);	
	dir.normalize();
	Matrix camera_mtx;
	camera.entity.getMatrix(camera_mtx);
	if(m_transform_mode == TransformMode::CAMERA_XZ)
	{
		Vec3 a = crossProduct(Vec3(0, 1, 0), camera_mtx.getXVector());
		Vec3 b = crossProduct(Vec3(0, 1, 0), camera_mtx.getZVector());
		Vec3 plane_normal = crossProduct(a, b);
		return Math::getRayPlaneIntersecion(origin, dir, m_selected_entity.getPosition(), plane_normal);
	}
	Vec3 axis;
	switch(m_transform_mode)
	{
		case TransformMode::X:
			axis = m_selected_entity.getMatrix().getXVector();
			break;
		case TransformMode::Y:
			axis = m_selected_entity.getMatrix().getYVector();
			break;
		case TransformMode::Z:
			axis = m_selected_entity.getMatrix().getZVector();
			break;
	}
	Vec3 pos = m_selected_entity.getPosition();
	Vec3 normal = crossProduct(crossProduct(dir, axis), dir);
	float d = dotProduct(origin - pos, normal) / dotProduct(axis, normal);
	return axis * d + pos;
}


} // !namespace Lux