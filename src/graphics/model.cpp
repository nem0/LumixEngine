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
	file_system.openAsync(file_system.getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, cb);
}


void Model::loaded(FS::IFile* file, bool success)
{
	if(success)
	{
		int object_count = 0;
		file->read(&object_count, sizeof(object_count));
		if(object_count != 1)
		{
			return;
		}
		int tri_count = 0;
		file->read(&tri_count, sizeof(tri_count));
		PODArray<float> data;
		int data_size = 16 * sizeof(float) * tri_count * 3;
		data.resize(data_size / sizeof(float));
		file->read(&data[0], data_size); 
		Mesh mesh(NULL, 0, tri_count * 3);
		m_geometry = LUX_NEW(Geometry);
		m_geometry->copy(&data[0], data_size);
	
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

		int str_size;
		file->read(&str_size, sizeof(str_size));
		char material_name[MAX_PATH];
		file->read(material_name, str_size);
		material_name[str_size] = 0;
		char material_path[MAX_PATH];
		strcpy(material_path, "materials/");
		strcat(material_path, material_name);
		strcat(material_path, ".mat");
		mesh.setMaterial(m_renderer.loadMaterial(material_path));
		m_meshes.push(mesh);
		for(int i = 0; i < m_bones.size(); ++i)
		{
			m_bones[i].rotation.toMatrix(m_bones[i].inv_bind_matrix);
			m_bones[i].inv_bind_matrix.translate(m_bones[i].position);
		}
		for(int i = 0; i < m_bones.size(); ++i)
		{
			m_bones[i].inv_bind_matrix.fastInverse();
		}
	}
	/// TODO close file somehow
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
