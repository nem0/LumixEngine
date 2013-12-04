#include "editor_icon.h"
#include "Horde3DUtils.h"
#include "core/matrix.h"
#include "graphics/renderer.h"
#include "core/crc32.h"


namespace Lux
{


H3DRes EditorIcon::s_geom;
H3DRes EditorIcon::s_materials[2];
static const uint32_t point_light_type = crc32("point_light");

void EditorIcon::create(Entity& entity, const Component& cmp)
{
	m_entity = entity;
	m_handle = h3dAddModelNode(H3DRootNode, "DynGeoModelNode", s_geom);
	int index = 0;
	if(cmp.type == point_light_type)
	{
		index = 1;
	}
	h3dAddMeshNode(m_handle, "DynGeoMesh", s_materials[index], 0, 6, 0, 3);

}


void EditorIcon::destroy()
{
	h3dRemoveNode(m_handle);
}


void EditorIcon::show()
{
	h3dSetNodeFlags(m_handle, h3dGetNodeFlags(m_handle) & ~H3DNodeFlags::Inactive, true);
}


void EditorIcon::hide()
{
	h3dSetNodeFlags(m_handle, H3DNodeFlags::Inactive, true);
}


void EditorIcon::update(Renderer* renderer)
{
	Matrix mtx;
	renderer->getCameraMatrix(mtx);

	float scale = renderer->getHalfFovTan() * (m_entity.getPosition() - mtx.getTranslation()).length() * 2;
	scale /= 3;
	Matrix scale_mtx = Matrix::IDENTITY;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;
	mtx = scale_mtx * mtx;

	mtx.setTranslation(m_entity.getPosition());
	h3dSetNodeTransMat(m_handle, &mtx.m11);
}


void EditorIcon::createResources(const char* base_path)
{
	float posData[] = {
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
	h3dutLoadResourcesFromDisk(base_path);
}


} // !namespace Lux