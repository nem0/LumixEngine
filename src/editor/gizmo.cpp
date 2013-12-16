#define _USE_MATH_DEFINES
#include <cmath>
#include "gizmo.h"
#include "Horde3DUtils.h"
#include "core/matrix.h"
#include "universe/universe.h"
#include "core/math_utils.h"
#include "graphics/renderer.h"
#include "core/quat.h"
#include "core/event_manager.h"
#include "universe/entity_moved_event.h"


namespace Lux
{


Gizmo::Gizmo()
{
	m_handle = 0;
	m_selected_entity.index = -1;
}


Gizmo::~Gizmo()
{
}


void Gizmo::create(const char* base_path, Renderer& renderer)
{
	m_renderer = &renderer;
	H3DRes res = h3dAddResource(H3DResTypes::SceneGraph, "models/tgizmo/tgizmo.scene.xml", 0);
	h3dutLoadResourcesFromDisk(base_path);
	m_handle = h3dAddNodes(H3DRootNode, res);
	h3dSetNodeFlags(m_handle, H3DNodeFlags::NoCastShadow, true);
}


void Gizmo::destroy()
{
	h3dRemoveNode(m_handle);
}


void Gizmo::hide()
{
	h3dSetNodeFlags(m_handle, H3DNodeFlags::Inactive, true);
}


void Gizmo::show()
{
	h3dSetNodeFlags(m_handle, h3dGetNodeFlags(m_handle) & ~H3DNodeFlags::Inactive, true);
}


void Gizmo::setMatrix(const Matrix& mtx)
{
	h3dSetNodeTransMat(m_handle, &mtx.m11);
}


void Gizmo::getMatrix(Matrix& mtx)
{
	const float* tmp;
	h3dGetNodeTransMats(m_handle, 0, &tmp);
	for(int i = 0; i < 16; ++i)
	{
		(&mtx.m11)[i] = tmp[i];
	}
}


void Gizmo::updateScale()
{
	Matrix camera_mtx;
	m_renderer->getCameraMatrix(camera_mtx);
	Matrix mtx;
	getMatrix(mtx);
	Vec3 pos = mtx.getTranslation();
	float scale = m_renderer->getHalfFovTan() * (mtx.getTranslation() - camera_mtx.getTranslation()).length() * 2;
	scale /= 20 * mtx.getXVector().length();
	Matrix scale_mtx = Matrix::IDENTITY;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;
	mtx = scale_mtx * mtx;
	mtx.setTranslation(pos);
	setMatrix(mtx);
}


void Gizmo::setEntity(Entity entity)
{
	h3dSetNodeFlags(m_handle, H3DNodeFlags::NoCastShadow, true);
	m_selected_entity = entity;
	if(m_selected_entity.index != -1)
	{
		setMatrix(m_selected_entity.getMatrix());
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
	if(m_universe)
	{
		m_universe->getEventManager()->registerListener(EntityMovedEvent::type, this, &Gizmo::onEvent);
	}
}


void Gizmo::onEvent(void* data, Event& evt)
{
	if(evt.getType() == EntityMovedEvent::type)
	{
		Entity e = static_cast<EntityMovedEvent&>(evt).entity;
		if(e == static_cast<Gizmo*>(data)->m_selected_entity)
		{
			static_cast<Gizmo*>(data)->setMatrix(e.getMatrix());
		}
	}
}


void Gizmo::startTransform(int x, int y, TransformMode mode)
{
	m_transform_mode = mode;
	m_transform_point = getMousePlaneIntersection(x, y);
	m_relx_accum = m_rely_accum = 0;
}


void Gizmo::transform(TransformOperation operation, int x, int y, int relx, int rely, int flags)
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
					angle = (float)M_PI / 4;
					m_relx_accum = m_rely_accum = 0;
				}
				else if(m_relx_accum + m_rely_accum < -50)
				{
					angle = -(float)M_PI / 4;
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
			Vec3 intersection = getMousePlaneIntersection(x, y);
			Vec3 delta = intersection - m_transform_point;
			m_transform_point = intersection;
			Matrix mtx;
			m_selected_entity.getMatrix(mtx);
			mtx.translate(delta);
			m_selected_entity.setMatrix(mtx);
			setMatrix(mtx);
		}
	}
}


Vec3 Gizmo::getMousePlaneIntersection(int x, int y)
{
	Vec3 origin, dir;
	m_renderer->getRay(x, y, origin, dir);	
	dir.normalize();
	Matrix camera_mtx;
	m_renderer->getCameraMatrix(camera_mtx);
	if(m_transform_mode == TransformMode::CAMERA_XZ)
	{
		Vec3 a = crossProduct(Vec3(0, 1, 0), camera_mtx.getXVector());
		Vec3 b = crossProduct(Vec3(0, 1, 0), camera_mtx.getZVector());
		Vec3 plane_normal = crossProduct(a, b);
		return getRayPlaneIntersecion(origin, dir, m_selected_entity.getPosition(), plane_normal);
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