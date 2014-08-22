#include "core/lumix.h"
#include "graphics/model.h"

#include "core/array.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec3.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/pose.h"
#include "graphics/renderer.h"


namespace Lumix
{

	
Model::~Model()
{
	ASSERT(isEmpty());
}


RayCastModelHit Model::castRay(const Vec3& origin, const Vec3& dir, const Matrix& model_transform, float scale)
{
	RayCastModelHit hit;
	hit.m_is_hit = false;
	if(!m_geometry)
	{
		return hit;
	}
	
	Matrix inv = model_transform;
	inv.multiply3x3(scale);
	inv.inverse();
	Vec3 local_origin = inv.multiplyPosition(origin);
	Vec3 local_dir = static_cast<Vec3>(inv * Vec4(dir.x, dir.y, dir.z, 0));

	const Array<Vec3>& vertices = m_geometry->getVertices();
	const Array<int32_t>& indices = m_geometry->getIndices();

	int32_t last_hit_index = -1;
	for(int i = 0; i < indices.size(); i += 3)
	{
		Vec3 p0 = vertices[indices[i]];
		Vec3 p1 = vertices[indices[i+1]];
		Vec3 p2 = vertices[indices[i+2]];
		Vec3 normal = crossProduct(p1 - p0, p2 - p0);
		float q = dotProduct(normal, local_dir);
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


bool Model::parseVertexDef(FS::IFile* file, VertexDef* vertex_definition)
{
	ASSERT(vertex_definition);
	int vertex_def_size = 0;
	file->read(&vertex_def_size, sizeof(vertex_def_size));
	char tmp[16];
	ASSERT(vertex_def_size < 16);
	if (vertex_def_size >= 16)
	{
		g_log_error.log("renderer") << "Model file corrupted " << getPath().c_str();
		return false;
	}
	file->read(tmp, vertex_def_size);
	vertex_definition->parse(tmp, vertex_def_size);
	return true;
}

bool Model::parseGeometry(FS::IFile* file, const VertexDef& vertex_definition)
{
	int32_t indices_count = 0;
	file->read(&indices_count, sizeof(indices_count));
	if (indices_count <= 0)
	{
		return false;
	}
	Array<int32_t> indices;
	indices.resize(indices_count);
	file->read(&indices[0], sizeof(indices[0]) * indices_count);
	
	int32_t vertices_count = 0;
	file->read(&vertices_count, sizeof(vertices_count));
	if (vertices_count <= 0)
	{
		return false;
	}
	Array<float> vertices;
	vertices.resize(vertices_count * vertex_definition.getVertexSize() / sizeof(vertices[0]));
	file->read(&vertices[0], sizeof(vertices[0]) * vertices.size());
	
	m_geometry = LUMIX_NEW(Geometry);
	m_geometry->copy((uint8_t*)&vertices[0], sizeof(float) * vertices.size(), indices, vertex_definition);
	return true;
}

bool Model::parseBones(FS::IFile* file)
{
	int bone_count;
	file->read(&bone_count, sizeof(bone_count));
	if (bone_count < 0)
	{
		return false;
	}
	m_bones.reserve(bone_count);
	for (int i = 0; i < bone_count; ++i)
	{
		Model::Bone& b = m_bones.pushEmpty();
		int len;
		file->read(&len, sizeof(len));
		char tmp[MAX_PATH];
		if (len >= MAX_PATH)
		{
			return false;
		}
		file->read(tmp, len);
		tmp[len] = 0;
		b.name = tmp;
		m_bone_map.insert(crc32(b.name.c_str()), m_bones.size() - 1);
		file->read(&len, sizeof(len));
		if (len >= MAX_PATH)
		{
			return false;
		}
		file->read(tmp, len);
		tmp[len] = 0;
		b.parent = tmp;
		file->read(&b.position.x, sizeof(float)* 3);
		file->read(&b.rotation.x, sizeof(float)* 4);
	}
	for (int i = 0; i < bone_count; ++i)
	{
		Model::Bone& b = m_bones[i];
		if (b.parent.length() == 0)
		{
			b.parent_idx = -1;
		}
		else
		{
			b.parent_idx = getBoneIdx(b.parent.c_str());
			if (b.parent_idx < 0)
			{
				g_log_error.log("renderer") << "Invalid skeleton in " << getPath().c_str();
			}
		}
	}
	for (int i = 0; i < m_bones.size(); ++i)
	{
		m_bones[i].rotation.toMatrix(m_bones[i].inv_bind_matrix);
		m_bones[i].inv_bind_matrix.translate(m_bones[i].position);
	}
	for (int i = 0; i < m_bones.size(); ++i)
	{
		m_bones[i].inv_bind_matrix.fastInverse();
	}
	return true;
}

int Model::getBoneIdx(const char* name)
{
	for (int i = 0, c = m_bones.size(); i < c; ++i)
	{
		if (m_bones[i].name == name)
		{
			return i;
		}
	}
	return -1;
}

bool Model::parseMeshes(FS::IFile* file)
{
	int object_count = 0;
	file->read(&object_count, sizeof(object_count));
	ASSERT(object_count > 0);
	if (object_count <= 0)
	{
		return false;
	}
	int32_t mesh_vertex_offset = 0;
	m_meshes.reserve(object_count);
	char model_dir[LUMIX_MAX_PATH];
	PathUtils::getDir(model_dir, LUMIX_MAX_PATH, m_path.c_str());
	for (int i = 0; i < object_count; ++i)
	{
		int32_t str_size;
		file->read(&str_size, sizeof(str_size));
		char material_name[LUMIX_MAX_PATH];
		file->read(material_name, str_size);
		if (str_size >= LUMIX_MAX_PATH)
		{
			return false;
		}
		material_name[str_size] = 0;
		
		base_string<char, StackAllocator<LUMIX_MAX_PATH> > material_path;
		material_path = model_dir;
		material_path += material_name;
		material_path += ".mat";

		int32_t mesh_tri_count = 0;
		file->read(&mesh_tri_count, sizeof(mesh_tri_count));

		file->read(&str_size, sizeof(str_size));
		if (str_size >= LUMIX_MAX_PATH)
		{
			return false;
		}
		char mesh_name[LUMIX_MAX_PATH];
		mesh_name[str_size] = 0;
		file->read(mesh_name, str_size);

		Material* material = static_cast<Material*>(m_resource_manager.get(ResourceManager::MATERIAL)->load(material_path.c_str()));
		Mesh mesh(material, mesh_vertex_offset, mesh_tri_count * 3, mesh_name);
		mesh_vertex_offset += mesh_tri_count * 3;
		m_meshes.push(mesh);
		addDependency(*material);
	}
	return true;
}

void Model::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{ 
	PROFILE_FUNCTION();
	if(success)
	{
		VertexDef vertex_definition;
		if (parseVertexDef(file, &vertex_definition) 
			&& parseGeometry(file, vertex_definition)
			&& parseBones(file)
			&& parseMeshes(file))
		{
			m_bounding_radius = m_geometry->getBoundingRadius();
			m_size = file->size();
			decrementDepCount();
		}
		else
		{
			onFailure();
			fs.close(file);
			return;
		}
	}
	else
	{
		g_log_warning.log("renderer") << "Error loading model " << m_path.c_str();
		onFailure();
	}

	fs.close(file);
}

void Model::doUnload(void)
{
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		removeDependency(*m_meshes[i].getMaterial());
		m_resource_manager.get(ResourceManager::MATERIAL)->unload(*m_meshes[i].getMaterial());
	}
	m_meshes.clear();
	m_bones.clear();
	LUMIX_DELETE(m_geometry);
	m_geometry = NULL;

	m_size = 0;
	onEmpty();
}

FS::ReadCallback Model::getReadCallback()
{
	FS::ReadCallback rc;
	rc.bind<Model, &Model::loaded>(this);
	return rc;
}



} // ~namespace Lumix
