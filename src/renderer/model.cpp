#include "engine/lumix.h"
#include "renderer/model.h"

#include "engine/core/array.h"
#include "engine/core/crc32.h"
#include "engine/core/fs/file_system.h"
#include "engine/core/log.h"
#include "engine/core/path_utils.h"
#include "engine/core/profiler.h"
#include "engine/core/resource_manager.h"
#include "engine/core/resource_manager_base.h"
#include "engine/core/vec.h"
#include "renderer/material.h"
#include "renderer/model_manager.h"
#include "renderer/pose.h"

#include <cfloat>
#include <cmath>


namespace Lumix
{


Mesh::Mesh(const bgfx::VertexDecl& def,
		   Material* mat,
		   int attribute_array_offset,
		   int attribute_array_size,
		   int indices_offset,
		   int index_count,
		   const char* name,
		   IAllocator& allocator)
	: name(name, allocator)
	, vertex_def(def)
{
	this->material = mat;
	this->attribute_array_offset = attribute_array_offset;
	this->attribute_array_size = attribute_array_size;
	this->indices_offset = indices_offset;
	this->indices_count = index_count;
	this->instance_idx = -1;
}


void Mesh::set(const bgfx::VertexDecl& def,
	int attribute_array_offset,
	int attribute_array_size,
	int indices_offset,
	int index_count)
{
	this->vertex_def = def;
	this->attribute_array_offset = attribute_array_offset;
	this->attribute_array_size = attribute_array_size;
	this->indices_offset = indices_offset;
	this->indices_count = index_count;
}


Model::Model(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_bounding_radius()
	, m_allocator(allocator)
	, m_bone_map(m_allocator)
	, m_meshes(m_allocator)
	, m_bones(m_allocator)
	, m_indices(m_allocator)
	, m_vertices(m_allocator)
	, m_vertices_handle(BGFX_INVALID_HANDLE)
	, m_indices_handle(BGFX_INVALID_HANDLE)
{
	m_lods[0] = { -1, -1, -1 };
	m_lods[1] = { -1, -1, -1 };
	m_lods[2] = { -1, -1, -1 };
	m_lods[3] = { -1, -1, -1 };
}


Model::~Model()
{
	ASSERT(isEmpty());
}


RayCastModelHit Model::castRay(const Vec3& origin,
							   const Vec3& dir,
							   const Matrix& model_transform)
{
	RayCastModelHit hit;
	hit.m_is_hit = false;
	if (!isReady()) return hit;

	Matrix inv = model_transform;
	inv.inverse();
	Vec3 local_origin = inv.multiplyPosition(origin);
	Vec3 local_dir = static_cast<Vec3>(inv * Vec4(dir.x, dir.y, dir.z, 0));

	const Array<Vec3>& vertices = m_vertices;
	uint16* indices16 = (uint16*)&m_indices[0];
	uint32* indices32 = (uint32*)&m_indices[0];
	int vertex_offset = 0;
	bool is16 = m_flags & (uint32)Model::Flags::INDICES_16BIT;
	for (int mesh_index = 0; mesh_index < m_meshes.size(); ++mesh_index)
	{
		int indices_end = m_meshes[mesh_index].indices_offset + m_meshes[mesh_index].indices_count;
		for(int i = m_meshes[mesh_index].indices_offset; i < indices_end; i += 3)
		{
			Vec3 p0, p1, p2;
			if(is16)
			{
				p0 = vertices[vertex_offset + indices16[i]];
				p1 = vertices[vertex_offset + indices16[i + 1]];
				p2 = vertices[vertex_offset + indices16[i + 2]];
			}
			else
			{
				p0 = vertices[vertex_offset + indices32[i]];
				p1 = vertices[vertex_offset + indices32[i + 1]];
				p2 = vertices[vertex_offset + indices32[i + 2]];
			}
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

			if (!hit.m_is_hit || hit.m_t > t)
			{
				hit.m_is_hit = true;
				hit.m_t = t;
				hit.m_mesh = &m_meshes[mesh_index];
			}
		}
		vertex_offset += m_meshes[mesh_index].attribute_array_size /
						 m_meshes[mesh_index].vertex_def.getStride();
	}
	hit.m_origin = origin;
	hit.m_dir = dir;
	return hit;
}


LODMeshIndices Model::getLODMeshIndices(float squared_distance) const
{
	int i = 0;
	while (squared_distance >= m_lods[i].distance)
	{
		++i;
	}
	return{ m_lods[i].from_mesh, m_lods[i].to_mesh };
}


void Model::getPose(Pose& pose)
{
	ASSERT(pose.getCount() == getBoneCount());
	Vec3* pos = pose.getPositions();
	Quat* rot = pose.getRotations();
	for (int i = 0, c = getBoneCount(); i < c; ++i)
	{
		pos[i] = m_bones[i].position;
		rot[i] = m_bones[i].rotation;
	}
	pose.setIsAbsolute();
}


bool Model::parseVertexDef(FS::IFile& file, bgfx::VertexDecl* vertex_definition)
{
	vertex_definition->begin();

	uint32 attribute_count;
	file.read(&attribute_count, sizeof(attribute_count));

	for (uint32 i = 0; i < attribute_count; ++i)
	{
		char tmp[50];
		uint32 len;
		file.read(&len, sizeof(len));
		if (len > sizeof(tmp) - 1)
		{
			return false;
		}
		file.read(tmp, len);
		tmp[len] = '\0';

		if (compareString(tmp, "in_position") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
		}
		else if (compareString(tmp, "in_colors") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, false);
		}
		else if (compareString(tmp, "in_tex_coords") == 0)
		{
			vertex_definition->add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
		}
		else if (compareString(tmp, "in_normal") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Normal, 4, bgfx::AttribType::Uint8, true, true);
		}
		else if (compareString(tmp, "in_tangents") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Uint8, true, true);
		}
		else if (compareString(tmp, "in_weights") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float);
		}
		else if (compareString(tmp, "in_indices") == 0)
		{
			vertex_definition->add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Int16, false, true);
		}
		else
		{
			ASSERT(false);
			return false;
		}

		uint32 type;
		file.read(&type, sizeof(type));
	}

	vertex_definition->end();
	return true;
}


void Model::create(const bgfx::VertexDecl& def,
				   Material* material,
				   const int* indices_data,
				   int indices_size,
				   const void* attributes_data,
				   int attributes_size)
{
	ASSERT(!bgfx::isValid(m_vertices_handle));
	m_vertices_handle = bgfx::createVertexBuffer(bgfx::copy(attributes_data, attributes_size), def);
	m_vertices_size = attributes_size;

	ASSERT(!bgfx::isValid(m_indices_handle));
	auto* mem = bgfx::copy(indices_data, indices_size);
	m_indices_handle = bgfx::createIndexBuffer(mem, BGFX_BUFFER_INDEX32);

	m_meshes.emplace(def,
					 material,
					 0,
					 attributes_size,
					 0,
					 indices_size / int(sizeof(int)),
					 "default",
					 m_allocator);

	Model::LOD lod;
	lod.distance = FLT_MAX;
	lod.from_mesh = 0;
	lod.to_mesh = 0;
	m_lods[0] = lod;

	m_indices.resize(indices_size);
	copyMemory(&m_indices[0], indices_data, indices_size);

	m_vertices.resize(attributes_size / def.getStride());
	computeRuntimeData((const uint8*)attributes_data);

	onCreated(State::READY);
}


void Model::computeRuntimeData(const uint8* vertices)
{
	int index = 0;
	float bounding_radius_squared = 0;
	Vec3 min_vertex(0, 0, 0);
	Vec3 max_vertex(0, 0, 0);

	for (int i = 0; i < m_meshes.size(); ++i)
	{
		int mesh_vertex_count = m_meshes[i].attribute_array_size / m_meshes[i].vertex_def.getStride();
		int mesh_attributes_array_offset = m_meshes[i].attribute_array_offset;
		int mesh_vertex_size = m_meshes[i].vertex_def.getStride();
		int mesh_position_attribute_offset = m_meshes[i].vertex_def.getOffset(bgfx::Attrib::Position);
		for (int j = 0; j < mesh_vertex_count; ++j)
		{
			m_vertices[index] = *(const Vec3*)&vertices[mesh_attributes_array_offset + j * mesh_vertex_size +
														mesh_position_attribute_offset];
			bounding_radius_squared = Math::maximum(bounding_radius_squared,
				dotProduct(m_vertices[index], m_vertices[index]) > 0 ? m_vertices[index].squaredLength() : 0);
			min_vertex.x = Math::minimum(min_vertex.x, m_vertices[index].x);
			min_vertex.y = Math::minimum(min_vertex.y, m_vertices[index].y);
			min_vertex.z = Math::minimum(min_vertex.z, m_vertices[index].z);
			max_vertex.x = Math::maximum(max_vertex.x, m_vertices[index].x);
			max_vertex.y = Math::maximum(max_vertex.y, m_vertices[index].y);
			max_vertex.z = Math::maximum(max_vertex.z, m_vertices[index].z);
			++index;
		}
	}

	m_bounding_radius = sqrt(bounding_radius_squared);
	m_aabb = AABB(min_vertex, max_vertex);
}


bool Model::parseGeometry(FS::IFile& file)
{
	int32 indices_count = 0;
	file.read(&indices_count, sizeof(indices_count));
	if (indices_count <= 0) return false;

	int index_size = (m_flags & (uint32)Model::Flags::INDICES_16BIT) ? 2 : 4;
	m_indices.resize(indices_count * index_size);
	file.read(&m_indices[0], index_size * indices_count);

	int32 vertices_size = 0;
	file.read(&vertices_size, sizeof(vertices_size));
	if (vertices_size <= 0) return false;

	ASSERT(!bgfx::isValid(m_vertices_handle));
	const bgfx::Memory* vertices_mem = bgfx::alloc(vertices_size);
	file.read(vertices_mem->data, vertices_size);
	m_vertices_handle = bgfx::createVertexBuffer(vertices_mem, m_meshes[0].vertex_def);
	m_vertices_size = vertices_size;

	ASSERT(!bgfx::isValid(m_indices_handle));
	int indices_size = index_size * indices_count;
	const bgfx::Memory* mem = bgfx::copy(&m_indices[0], indices_size);
	m_indices_handle = bgfx::createIndexBuffer(mem, index_size == 4 ? BGFX_BUFFER_INDEX32 : 0);

	int vertex_count = 0;
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		vertex_count += m_meshes[i].attribute_array_size / m_meshes[i].vertex_def.getStride();
	}
	m_vertices.resize(vertex_count);

	computeRuntimeData(vertices_mem->data);

	return true;
}

bool Model::parseBones(FS::IFile& file)
{
	int bone_count;
	file.read(&bone_count, sizeof(bone_count));
	if (bone_count < 0)
	{
		return false;
	}
	m_bones.reserve(bone_count);
	for (int i = 0; i < bone_count; ++i)
	{
		Model::Bone& b = m_bones.emplace(m_allocator);
		int len;
		file.read(&len, sizeof(len));
		char tmp[MAX_PATH_LENGTH];
		if (len >= MAX_PATH_LENGTH)
		{
			return false;
		}
		file.read(tmp, len);
		tmp[len] = 0;
		b.name = tmp;
		m_bone_map.insert(crc32(b.name.c_str()), m_bones.size() - 1);
		file.read(&len, sizeof(len));
		if (len >= MAX_PATH_LENGTH)
		{
			return false;
		}
		if(len > 0)
		{
			file.read(tmp, len);
			tmp[len] = 0;
			b.parent = tmp;
		}
		else
		{
			b.parent = "";
		}
		file.read(&b.position.x, sizeof(float) * 3);
		file.read(&b.rotation.x, sizeof(float) * 4);
	}
	m_first_nonroot_bone_index = -1;
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
			if (b.parent_idx > i || b.parent_idx < 0)
			{
				g_log_error.log("Renderer") << "Invalid skeleton in "
											<< getPath().c_str();
				return false;
			}
			if (m_first_nonroot_bone_index == -1)
			{
				m_first_nonroot_bone_index = i;
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

bool Model::parseMeshes(FS::IFile& file)
{
	int object_count = 0;
	file.read(&object_count, sizeof(object_count));
	if (object_count <= 0) return false;

	m_meshes.reserve(object_count);
	char model_dir[MAX_PATH_LENGTH];
	PathUtils::getDir(model_dir, MAX_PATH_LENGTH, getPath().c_str());
	for (int i = 0; i < object_count; ++i)
	{
		int32 str_size;
		file.read(&str_size, sizeof(str_size));
		char material_name[MAX_PATH_LENGTH];
		file.read(material_name, str_size);
		if (str_size >= MAX_PATH_LENGTH) return false;

		material_name[str_size] = 0;

		char material_path[MAX_PATH_LENGTH];
		copyString(material_path, model_dir);
		catString(material_path, material_name);
		catString(material_path, ".mat");

		auto* material_manager = m_resource_manager.get(ResourceManager::MATERIAL);
		Material* material = static_cast<Material*>(material_manager->load(Path(material_path)));

		int32 attribute_array_offset = 0;
		file.read(&attribute_array_offset, sizeof(attribute_array_offset));
		int32 attribute_array_size = 0;
		file.read(&attribute_array_size, sizeof(attribute_array_size));
		int32 indices_offset = 0;
		file.read(&indices_offset, sizeof(indices_offset));
		int32 mesh_tri_count = 0;
		file.read(&mesh_tri_count, sizeof(mesh_tri_count));

		file.read(&str_size, sizeof(str_size));
		if (str_size >= MAX_PATH_LENGTH)
		{
			material_manager->unload(*material);
			return false;
		}

		char mesh_name[MAX_PATH_LENGTH];
		mesh_name[str_size] = 0;
		file.read(mesh_name, str_size);

		bgfx::VertexDecl def;
		parseVertexDef(file, &def);
		m_meshes.emplace(def,
						 material,
						 attribute_array_offset,
						 attribute_array_size,
						 indices_offset,
						 mesh_tri_count * 3,
						 mesh_name,
						 m_allocator);
		addDependency(*material);
	}
	return true;
}


bool Model::parseLODs(FS::IFile& file)
{
	int32 lod_count;
	file.read(&lod_count, sizeof(lod_count));
	if (lod_count <= 0 || lod_count > lengthOf(m_lods))
	{
		return false;
	}
	for (int i = 0; i < lod_count; ++i)
	{
		file.read(&m_lods[i].to_mesh, sizeof(m_lods[i].to_mesh));
		file.read(&m_lods[i].distance, sizeof(m_lods[i].distance));
		m_lods[i].from_mesh = i > 0 ? m_lods[i - 1].to_mesh + 1 : 0;
	}
	return true;
}


bool Model::load(FS::IFile& file)
{
	PROFILE_FUNCTION();
	FileHeader header;
	file.read(&header, sizeof(header));

	if (header.magic != FILE_MAGIC)
	{
		g_log_warning.log("Renderer") << "Corrupted model " << getPath().c_str();
		return false;
	}

	if(header.version > (uint32)FileVersion::LATEST)
	{
		g_log_warning.log("Renderer") << "Unsupported version of model " << getPath().c_str();
		return false;
	}

	m_flags = 0;
	if(header.version > (uint32)FileVersion::WITH_FLAGS)
	{
		file.read(&m_flags, sizeof(m_flags));
	}

	if (parseMeshes(file) && parseGeometry(file) && parseBones(file) && parseLODs(file))
	{
		m_size = file.size();
		return true;
	}

	g_log_warning.log("Renderer") << "Error loading model " << getPath().c_str();
	return false;
}

void Model::unload(void)
{
	auto* material_manager = m_resource_manager.get(ResourceManager::MATERIAL);
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		removeDependency(*m_meshes[i].material);
		material_manager->unload(*m_meshes[i].material);
	}
	m_meshes.clear();
	m_bones.clear();

	if(bgfx::isValid(m_vertices_handle)) bgfx::destroyVertexBuffer(m_vertices_handle);
	if(bgfx::isValid(m_indices_handle)) bgfx::destroyIndexBuffer(m_indices_handle);
	m_indices_handle = BGFX_INVALID_HANDLE;
	m_vertices_handle = BGFX_INVALID_HANDLE;
}


} // ~namespace Lumix
