#include "editor_icon.h"
#include "core/crc32.h"
#include "core/math_utils.h"
#include "core/matrix.h"
#include "graphics/irender_device.h"
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
void EditorIcon::create(Entity& entity, const Component& cmp)
{
	m_entity = entity;
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
	Lux::Matrix mtx = camera.entity.getMatrix();

	float fov;
	renderer.getCameraFov(camera, fov);
	float scale = tan(fov * Math::PI / 180.0f * 0.5f) * (m_entity.getPosition() - mtx.getTranslation()).length() / 20;

	mtx.setTranslation(m_entity.getPosition());
	Matrix scale_mtx = Matrix::IDENTITY;
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;
	mtx = mtx * scale_mtx;

	Vec3 p[6];
	p[0].set(-1, -1, 0);
	p[1].set(-1, 1, 0);
	p[2].set(1, 1, 0);
	p[3] = p[0];
	p[4] = p[2];
	p[5].set(1, -1, 0);

	Matrix inv = mtx;
	inv.inverse();
	Vec3 local_origin = inv.mutliplyPosition(origin);
	Vec3 local_dir = static_cast<Vec3>(inv * Vec4(dir.x, dir.y, dir.z, 0));

	int32_t last_hit_index = -1;
	for (int i = 0; i < 6; i += 3)
	{
		Vec3 p0 = p[i];
		Vec3 p1 = p[i + 1];
		Vec3 p2 = p[i + 2];
		Vec3 normal = crossProduct(p1 - p0, p2 - p0);
		float q = dotProduct(normal, local_dir);
		if (q == 0)
		{
			continue;
		}
		float d = -dotProduct(normal, p0);
		float t = -(dotProduct(normal, local_origin) + d) / q;
		if (t < 0)
		{
			continue;
		}
		Vec3 hit_point = local_origin + local_dir * t;

		Vec3 edge0 = p1 - p0;
		Vec3 VP0 = hit_point - p0;
		if (dotProduct(normal, crossProduct(edge0, VP0)) < 0)
		{
			continue;
		}

		Vec3 edge1 = p2 - p1;
		Vec3 VP1 = hit_point - p1;
		if (dotProduct(normal, crossProduct(edge1, VP1)) < 0)
		{
			continue;
		}

		Vec3 edge2 = p0 - p2;
		Vec3 VP2 = hit_point - p2;
		if (dotProduct(normal, crossProduct(edge2, VP2)) < 0)
		{
			continue;
		}

		return t;
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
	scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;
	mtx = mtx * scale_mtx;

	glPushMatrix();
	glMultMatrixf(&mtx.m11);
	glBegin(GL_QUADS);
		glVertex3f(-1, -1, 0);
		glVertex3f(-1, 1, 0);
		glVertex3f(1, 1, 0);
		glVertex3f(1, -1, 0);
	glEnd();
	glPopMatrix();
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