#include "physics_geometry_manager.h"
#include "engine/fs/file_system.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/vec.h"
#include "physics/physics_system.h"
#include <PxPhysicsAPI.h>


namespace Lumix
{


static const ResourceType PHYSICS_TYPE("physics");


struct OutputStream LUMIX_FINAL : public physx::PxOutputStream
{
	explicit OutputStream(IAllocator& allocator)
		: allocator(allocator)
	{
		data = (u8*)allocator.allocate(sizeof(u8) * 4096);
		capacity = 4096;
		size = 0;
	}

	~OutputStream() { allocator.deallocate(data); }


	virtual physx::PxU32 write(const void* src, physx::PxU32 count)
	{
		if (size + (int)count > capacity)
		{
			int new_capacity = Math::maximum(size + (int)count, capacity + 4096);
			u8* new_data = (u8*)allocator.allocate(sizeof(u8) * new_capacity);
			copyMemory(new_data, data, size);
			allocator.deallocate(data);
			data = new_data;
			capacity = new_capacity;
		}
		copyMemory(data + size, src, count);
		size += count;
		return count;
	}

	u8* data;
	IAllocator& allocator;
	int capacity;
	int size;
};


struct InputStream LUMIX_FINAL : public physx::PxInputStream
{
	InputStream(unsigned char* data, int size)
	{
		this->data = data;
		this->size = size;
		pos = 0;
	}

	virtual physx::PxU32 read(void* dest, physx::PxU32 count)
	{
		if (pos + (int)count <= size)
		{
			copyMemory(dest, data + pos, count);
			pos += count;
			return count;
		}
		else
		{
			copyMemory(dest, data + pos, size - pos);
			int real_count = size - pos;
			pos = size;
			return real_count;
		}
	}


	int pos;
	int size;
	unsigned char* data;
};


Resource* PhysicsGeometryManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, PhysicsGeometry)(path, *this, m_allocator);
}


void PhysicsGeometryManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<PhysicsGeometry*>(&resource));
}


PhysicsGeometry::PhysicsGeometry(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, convex_mesh(nullptr)
	, tri_mesh(nullptr)
{
}

PhysicsGeometry::~PhysicsGeometry()
{
}


bool PhysicsGeometry::load(FS::IFile& file)
{
	Header header;
	file.read(&header, sizeof(header));
	if (header.m_magic != HEADER_MAGIC)
	{
		g_log_warning.log("Physics") << "Corrupted geometry " << getPath().c_str();
		return false;
	}

	if(header.m_version > (u32)Versions::LAST)
	{
		g_log_warning.log("Physics") << "Unsupported version of geometry " << getPath().c_str();
		return false;
	}

	PhysicsSystem& system = static_cast<PhysicsGeometryManager&>(m_resource_manager).getSystem();

	i32 num_verts;
	Array<Vec3> verts(getAllocator());
	file.read(&num_verts, sizeof(num_verts));
	verts.resize(num_verts);
	file.read(&verts[0], sizeof(verts[0]) * verts.size());

	bool is_convex = header.m_convex != 0;
	if (is_convex)
	{
		physx::PxConvexMeshDesc meshDesc;
		meshDesc.points.count = verts.size();
		meshDesc.points.stride = sizeof(Vec3);
		meshDesc.points.data = &verts[0];
		meshDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

		OutputStream writeBuffer(getAllocator());
		bool status = system.getCooking()->cookConvexMesh(meshDesc, writeBuffer);
		if (!status)
		{
			convex_mesh = nullptr;
			return false;
		}

		InputStream readBuffer(writeBuffer.data, writeBuffer.size);
		convex_mesh = system.getPhysics()->createConvexMesh(readBuffer);
		tri_mesh = nullptr;
	}
	else
	{
		u32 num_indices;
		Array<u32> tris(getAllocator());
		file.read(&num_indices, sizeof(num_indices));
		tris.resize(num_indices);
		file.read(&tris[0], sizeof(tris[0]) * tris.size());

		physx::PxTriangleMeshDesc meshDesc;
		meshDesc.points.count = num_verts;
		meshDesc.points.stride = sizeof(physx::PxVec3);
		meshDesc.points.data = &verts[0];

		meshDesc.triangles.count = num_indices / 3;
		meshDesc.triangles.stride = 3 * sizeof(physx::PxU32);
		meshDesc.triangles.data = &tris[0];

		OutputStream writeBuffer(getAllocator());
		if (!system.getCooking()->cookTriangleMesh(meshDesc, writeBuffer))
		{
			tri_mesh = nullptr;
			return false;
		}

		InputStream readBuffer(writeBuffer.data, writeBuffer.size);
		tri_mesh = system.getPhysics()->createTriangleMesh(readBuffer);
		convex_mesh = nullptr;
	}

	m_size = file.size();
	return true;
}


IAllocator& PhysicsGeometry::getAllocator()
{
	return static_cast<PhysicsGeometryManager&>(m_resource_manager).getAllocator();
}


void PhysicsGeometry::unload()
{
	if (convex_mesh) convex_mesh->release();
	if (tri_mesh) tri_mesh->release();
	convex_mesh = nullptr;
	tri_mesh = nullptr;
}


} // namespace Lumix