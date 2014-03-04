#include "graphics/model.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/pod_array.h"
#include "core/vec3.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/pose.h"
#include "graphics/renderer.h"
/*#include "graphics/vertex_buffer.h"
#include "graphics/material.h"
#include "common/manager.h"
#include "graphics/shader.h"
#include "graphics/pose.h"
#include "system/resource_manager_bucket.h"*/


namespace Lux
{

	
Model::~Model()
{
	LUX_DELETE(m_geometry);
}

	
void Model::load(const char* path, FS::FileSystem& file_system)
{
	FS::ReadCallback cb;
	cb.bind<Model, &Model::loaded>(this);
	m_path = path;
	file_system.openAsync(file_system.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, cb);
}


RayCastModelHit Model::castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform)
{
	RayCastModelHit hit;
	hit.m_is_hit = false;
	if(!m_geometry)
	{
		return hit;
	}
	
	Matrix inv = model_transform;
	inv.fastInverse();
	Vec3 local_origin = inv.mutliplyPosition(origin);
	Vec3 local_dir = static_cast<Vec3>(inv * Vec4(dir.x, dir.y, dir.z, 0));

	const PODArray<Vec3>& vertices = m_geometry->getVertices();

	int32_t last_hit_index = -1;
	for(int i = 0; i < vertices.size(); i += 3)
	{
		Vec3 p0 = vertices[i];
		Vec3 p1 = vertices[i+1];
		Vec3 p2 = vertices[i+2];
		Vec3 normal = crossProduct(p1 - p0, p2 - p0);
		float q = dotProduct(normal, dir);
		if(q == 0)
		{
			continue;
		}
		float d = -dotProduct(normal, p0);
		float t = -(dotProduct(normal, local_origin) + d) / q;
		if(t < 0)
		{
			continue;
		}
		Vec3 hit_point = local_origin + dir * t;

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

		if(!hit.m_is_hit || hit.m_t > t)
		{
			hit.m_is_hit = true;
			hit.m_t = t;
			last_hit_index = i;
		}
	}

	if(last_hit_index != -1)
	{
		for(int i = 0; i < m_meshes.size(); ++i)
		{
			if(last_hit_index < m_meshes[i].getStart() + m_meshes[i].getCount())
			{
				hit.m_mesh = &m_meshes[i];
				break;
			}
		}
	}
	hit.m_origin = origin;
	hit.m_dir = dir;
	return hit;
}


void Model::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{ 
	/// TODO refactor
	if(success)
	{
		int object_count = 0;
		file->read(&object_count, sizeof(object_count));

		VertexDef vertex_defition;
		int vertex_def_size = 0;
		file->read(&vertex_def_size, sizeof(vertex_def_size));
		char tmp[16];
		ASSERT(vertex_def_size < 16);
		file->read(tmp, vertex_def_size);
		vertex_defition.parse(tmp, vertex_def_size);

		int tri_count = 0;
		file->read(&tri_count, sizeof(tri_count));
		PODArray<uint8_t> data;
		int data_size = vertex_defition.getVertexSize() * tri_count * 3;
		data.resize(data_size);
		file->read(&data[0], data_size); 
		m_geometry = LUX_NEW(Geometry);
		m_geometry->copy(&data[0], data_size, vertex_defition);
		m_bounding_radius = m_geometry->getBoundingRadius();

		int bone_count;	
		file->read(&bone_count, sizeof(bone_count));
		for(int i = 0; i < bone_count; ++i)
		{
			Bone& b = m_bones.pushEmpty();
			int len;
			file->read(&len, sizeof(len));
			char tmp[MAX_PATH];
			file->read(tmp, len);
			tmp[len] = 0;
			b.name = tmp;
			file->read(&len, sizeof(len));
			file->read(tmp, len);
			tmp[len] = 0;
			b.parent = tmp;
			file->read(&b.position.x, sizeof(float) * 3);
			file->read(&b.rotation.x, sizeof(float) * 4);
		}
		for(int i = 0; i < m_bones.size(); ++i)
		{
			m_bones[i].rotation.toMatrix(m_bones[i].inv_bind_matrix);
			m_bones[i].inv_bind_matrix.translate(m_bones[i].position);
		}
		for(int i = 0; i < m_bones.size(); ++i)
		{
			m_bones[i].inv_bind_matrix.fastInverse();
		}
		int32_t mesh_vertex_offset = 0;
		for(int i = 0; i < object_count; ++i)
		{
			int32_t str_size;
			file->read(&str_size, sizeof(str_size));
			char material_name[MAX_PATH];
			file->read(material_name, str_size);
			material_name[str_size] = 0;
			char material_path[MAX_PATH];
			strcpy(material_path, "materials/");
			strcat(material_path, material_name);
			strcat(material_path, ".mat");
			int32_t mesh_tri_count = 0;
			file->read(&mesh_tri_count, sizeof(mesh_tri_count));
			file->read(&str_size, sizeof(str_size));
			char mesh_name[MAX_PATH];
			mesh_name[str_size] = 0;
			file->read(mesh_name, str_size);
			Mesh mesh(m_renderer.loadMaterial(material_path), mesh_vertex_offset, mesh_tri_count * 3, mesh_name);
			mesh_vertex_offset += mesh_tri_count * 3;
			m_meshes.push(mesh);
		}
		m_on_loaded.invoke();
	}

	fs.close(file);
}


void Model::getPose(Pose& pose)
{
	ASSERT(pose.getCount() == getBoneCount());
	Vec3* pos =	pose.getPositions();
	Quat* rot = pose.getRotations();
	Matrix mtx;
	for(int i = 0, c = getBoneCount(); i < c; ++i) 
	{
		mtx = m_bones[i].inv_bind_matrix;
		mtx.fastInverse();
		mtx.getTranslation(pos[i]);
		mtx.getRotation(rot[i]);
	}
}


} // ~namespace Lux
