#include "editor_icon.h"
#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "graphics/irender_device.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"

#include <Windows.h>
#include <gl/GL.h>

namespace Lux
{

/*
H3DRes EditorIcon::s_geom;
H3DRes EditorIcon::s_materials[2];
static const uint32_t point_light_type = crc32("point_light");
*/
void EditorIcon::create(Renderer& renderer, Entity& entity, const Component&)
{
	m_entity = entity;
	m_model = renderer.getModel("models/icon.msh");
	/*m_handle = h3dAddModelNode(H3DRootNode, "DynGeoModelNode", s_geom);
	int index = 0;
	if(cmp.type == point_light_type)
	{
		index = 1;
	}
	h3dAddMeshNode(m_handle, "DynGeoMesh", s_materials[index], 0, 6, 0, 3);*/
	//ASSERT(false);

}


void EditorIcon::destroy()
{
}


void EditorIcon::show()
{
	//h3dSetNodeFlags(m_handle, h3dGetNodeFlags(m_handle) & ~H3DNodeFlags::Inactive, true);
	ASSERT(false);
}


void EditorIcon::hide()
{
	//h3dSetNodeFlags(m_handle, h3dGetNodeFlags(m_handle) | H3DNodeFlags::Inactive, true);
	ASSERT(false);
}



float EditorIcon::hit(const Vec3& origin, const Vec3& dir) const
{
	RayCastModelHit hit = m_model->castRay(origin, dir, m_matrix, m_scale);
	return hit.m_is_hit ? hit.m_t : -1;
}


void EditorIcon::render(Renderer* renderer, IRenderDevice& render_device)
{
	Component camera = render_device.getPipeline().getCamera(0);
	Lux::Matrix mtx = camera.entity.getMatrix();
	
	float fov;
	renderer->getCameraFov(camera, fov);
	float scale = tan(fov * Math::PI / 180 * 0.5f) * (m_entity.getPosition() - mtx.getTranslation()).length() / 20;

	mtx.setTranslation(m_entity.getPosition());
	Matrix scale_mtx = Matrix::IDENTITY;
	m_matrix = mtx;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;
	mtx = mtx * scale_mtx;
	m_scale = scale;

	if (m_model->isReady())
	{
		renderer->renderModel(*m_model, mtx);
	}
}


} // !namespace Lux