#include "model.h"

#include "pose.h"
#include "core/profiler.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/log.h"


namespace Lumix
{


Model::Model(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_bounding_radius()
	, m_sizeX(0)
	, m_sizeY(0)
	, m_sizeZ(0),
	m_data(allocator)
{

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
	if (!isReady())
	{
		return hit;
	}

	Matrix inv = model_transform;
	inv.inverse();
	Vec3 local_origin = inv.multiplyPosition(origin);
	Vec3 local_dir = static_cast<Vec3>(inv * Vec4(dir.x, dir.y, dir.z, 0));

	const Array<Vec3>& vertices = m_vertices;
	const Array<int32>& indices = m_indices;
	int vertex_offset = 0;
	for (int mesh_index = 0; mesh_index < m_meshes.size(); ++mesh_index)
	{
		int indices_end = m_meshes[mesh_index].indices_offset +
			m_meshes[mesh_index].indices_count;
		for (int i = m_meshes[mesh_index].indices_offset; i < indices_end;
			 i += 3)
		{
			Vec3 p0 = vertices[vertex_offset + indices[i]];
			Vec3 p1 = vertices[vertex_offset + indices[i + 1]];
			Vec3 p2 = vertices[vertex_offset + indices[i + 2]];
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


bool Model::parseData(FS::IFile& file)
{
	file.read(&m_sizeX, sizeof(m_sizeX));
	file.read(&m_sizeY, sizeof(m_sizeY));
	file.read(&m_sizeZ, sizeof(m_sizeZ));
	int size = m_sizeX * m_sizeY * m_sizeZ;

	m_data.resize(size);

	file.read(m_data.begin(), size * sizeof(uint8));
}


bool Model::load(FS::IFile& file)
{
	PROFILE_FUNCTION();
	FileHeader header;
	file.read(&header, sizeof(header));
	if (header.m_magic == FILE_MAGIC
		&& header.m_version <= (uint32)FileVersion::LATEST
		&& parseData(file))
	{
		m_size = file.size();
		return true;
	}

	g_log_warning.log("Renderer") << "Error loading model " << getPath().c_str();
	return false;
}

void Model::unload(void)
{
	//m_allocator

	auto* material_manager = m_resource_manager.get(ResourceManager::MATERIAL);
	for (int i = 0; i < m_meshes.size(); ++i)
	{
		removeDependency(*m_meshes[i].material);
		material_manager->unload(*m_meshes[i].material);
	}
	m_meshes.clear();
	m_bones.clear();

	if (bgfx::isValid(m_vertices_handle)) bgfx::destroyVertexBuffer(m_vertices_handle);
	if (bgfx::isValid(m_indices_handle)) bgfx::destroyIndexBuffer(m_indices_handle);
	m_indices_handle = BGFX_INVALID_HANDLE;
	m_vertices_handle = BGFX_INVALID_HANDLE;
}


} // namespace Lumix
