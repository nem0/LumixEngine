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
void EditorIcon::create(Renderer& renderer, Entity& entity, const Component& cmp)
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



float EditorIcon::hit(Renderer& renderer, Component camera, const Vec3& origin, const Vec3& dir) const
{
	RayCastModelHit hit = m_model->castRay(origin, dir, m_matrix, m_scale);
	if (hit.m_is_hit)
	{
		return hit.m_t;
	}
	return -1;
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


void EditorIcon::createResources(const char* base_path)
{
	/*float posData[] = {
		-0.1f,  -0.1f, 0,
		0.1f,  -0.1f, 0,
		-0.1f, 0.1f, 0,
		0.1f, 0.1f, 0
	};
 
	unsigned int indexData[] = { 0, 1, 2, 2, 1, 3 };
	short normalData[] = {
		0, 0, 1,
		0, 0, 1,
		0, 0, 1,
		0, 0, 1
	};
 
	float uvData[] = {
		0, 0,
		1, 0,
		0, 1,
		1, 1
	};
	s_geom = h3dutCreateGeometryRes("EditoRenderableGeom", 4, 6, posData, indexData, normalData, 0, 0, uvData, 0 );
	s_materials[0] = h3dAddResource(H3DResTypes::Material, "materials\\entity.material.xml", 0);
	s_materials[1] = h3dAddResource(H3DResTypes::Material, "materials\\point_light.material.xml", 0);
	h3dutLoadResourcesFromDisk(base_path);*/
}


} // !namespace Lux