#include "physics_geometry_manager.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/resource_manager.h"
#include "core/string.h"
#include "core/vec.h"
#include "physics/physics_system.h"
#include <PxPhysicsAPI.h>


namespace Lumix
{


	struct OutputStream : public physx::PxOutputStream
	{
		explicit OutputStream(IAllocator& allocator)
			: allocator(allocator)
		{
			data = (uint8*)allocator.allocate(sizeof(uint8) * 4096);
			capacity = 4096;
			size = 0;
		}

		~OutputStream()
		{
			allocator.deallocate(data);
		}


		virtual physx::PxU32 write(const void* src, physx::PxU32 count)
		{
			if (size + (int)count > capacity)
			{
				int new_capacity = Math::maximum(size + (int)count, capacity + 4096);
				uint8* new_data = (uint8*)allocator.allocate(sizeof(uint8) * new_capacity);
				copyMemory(new_data, data, size);
				allocator.deallocate(data);
				data = new_data;
				capacity = new_capacity;
			}
			copyMemory(data + size, src, count);
			size += count;
			return count;
		}

		uint8* data;
		IAllocator& allocator;
		int capacity;
		int size;
	};


	struct InputStream : public physx::PxInputStream
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
		return LUMIX_NEW(m_allocator, PhysicsGeometry)(path, getOwner(), m_allocator);
	}


	void PhysicsGeometryManager::destroyResource(Resource& resource)
	{
		LUMIX_DELETE(m_allocator, static_cast<PhysicsGeometry*>(&resource));
	}


	PhysicsGeometry::PhysicsGeometry(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
		, m_geometry(nullptr)
	{

	}

	PhysicsGeometry::~PhysicsGeometry()
	{
		LUMIX_DELETE(getAllocator(), m_geometry);
	}


	bool PhysicsGeometry::load(FS::IFile& file)
	{
		Header header;
		file.read(&header, sizeof(header));
		if (header.m_magic != HEADER_MAGIC || header.m_version > (uint32)Versions::LAST)
		{
			return false;
		}

		auto* phy_manager = m_resource_manager.get(ResourceManager::PHYSICS);
		PhysicsSystem& system = static_cast<PhysicsGeometryManager*>(phy_manager)->getSystem();

		uint32 num_verts;
		Array<Vec3> verts(getAllocator());
		file.read(&num_verts, sizeof(num_verts));
		verts.resize(num_verts);
		file.read(&verts[0], sizeof(verts[0]) * verts.size());

		m_is_convex = header.m_convex != 0;
		if (!m_is_convex)
		{
			physx::PxTriangleMeshGeometry* geom =
				LUMIX_NEW(getAllocator(), physx::PxTriangleMeshGeometry)();
			m_geometry = geom;
			uint32 num_indices;
			Array<uint32> tris(getAllocator());
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
			system.getCooking()->cookTriangleMesh(meshDesc, writeBuffer);

			InputStream readBuffer(writeBuffer.data, writeBuffer.size);
			geom->triangleMesh = system.getPhysics()->createTriangleMesh(readBuffer);
		}
		else
		{
			physx::PxConvexMeshGeometry* geom =
				LUMIX_NEW(getAllocator(), physx::PxConvexMeshGeometry)();
			m_geometry = geom;
			physx::PxConvexMeshDesc meshDesc;
			meshDesc.points.count = verts.size();
			meshDesc.points.stride = sizeof(Vec3);
			meshDesc.points.data = &verts[0];
			meshDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

			OutputStream writeBuffer(getAllocator());
			bool status = system.getCooking()->cookConvexMesh(meshDesc, writeBuffer);
			if (!status)
			{
				LUMIX_DELETE(getAllocator(), geom);
				m_geometry = nullptr;
				return false;
			}

			InputStream readBuffer(writeBuffer.data, writeBuffer.size);
			physx::PxConvexMesh* mesh = system.getPhysics()->createConvexMesh(readBuffer);
			geom->convexMesh = mesh;
		}

		m_size = file.size();
		return true;
	}


	IAllocator& PhysicsGeometry::getAllocator()
	{
		return static_cast<PhysicsGeometryManager*>(m_resource_manager.get(ResourceManager::PHYSICS))->getAllocator();
	}


	void PhysicsGeometry::unload(void)
	{
		LUMIX_DELETE(getAllocator(), m_geometry);
		m_geometry = nullptr;
	}


} // namespace Lumix