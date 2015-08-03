#include "physics_geometry_manager.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/resource_manager.h"
#include "core/vec3.h"
#include "physics/physics_system.h"
#include <PxPhysicsAPI.h>


namespace Lumix
{


	struct OutputStream : public physx::PxOutputStream
	{
		OutputStream(IAllocator& allocator)
			: allocator(allocator)
		{
			data = (uint8_t*)allocator.allocate(sizeof(uint8_t) * 4096);
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
				int new_capacity = Math::maxValue(size + (int)count, capacity + 4096);
				uint8_t* new_data = (uint8_t*)allocator.allocate(sizeof(uint8_t) * new_capacity);
				memcpy(new_data, data, size);
				allocator.deallocate(data);
				data = new_data;
				capacity = new_capacity;
			}
			memcpy(data + size, src, count);
			size += count;
			return count;
		}

		uint8_t* data;
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
				memcpy(dest, data + pos, count);
				pos += count;
				return count;
			}
			else
			{
				memcpy(dest, data + pos, size - pos);
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
		return m_allocator.newObject<PhysicsGeometry>(path, getOwner(), m_allocator);
	}


	void PhysicsGeometryManager::destroyResource(Resource& resource)
	{
		m_allocator.deleteObject(static_cast<PhysicsGeometry*>(&resource));
	}


	PhysicsGeometry::PhysicsGeometry(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
		, m_geometry(nullptr)
	{

	}

	PhysicsGeometry::~PhysicsGeometry()
	{
		getAllocator().deleteObject(m_geometry);
	}


	void PhysicsGeometry::loaded(FS::IFile& file, bool success, FS::FileSystem& fs)
	{
		if (success)
		{
			Header header;
			file.read(&header, sizeof(header));
			if (header.m_magic != HEADER_MAGIC || header.m_version > (uint32_t)Versions::LAST)
			{
				onFailure();
				fs.closeAsync(file);
				return;
			}

			PhysicsSystem& system = static_cast<PhysicsGeometryManager*>(m_resource_manager.get(ResourceManager::PHYSICS))->getSystem();
			
			uint32_t num_verts;
			Array<Vec3> verts(getAllocator());
			file.read(&num_verts, sizeof(num_verts));
			verts.resize(num_verts);
			file.read(&verts[0], sizeof(verts[0]) * verts.size());

			m_is_convex = header.m_convex != 0;
			if (!m_is_convex)
			{
				physx::PxTriangleMeshGeometry* geom = getAllocator().newObject<physx::PxTriangleMeshGeometry>();
				m_geometry = geom;
				uint32_t num_indices;
				Array<uint32_t> tris(getAllocator());
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
				physx::PxConvexMeshGeometry* geom = getAllocator().newObject<physx::PxConvexMeshGeometry>();
				m_geometry = geom;
				physx::PxConvexMeshDesc meshDesc;
				meshDesc.points.count = verts.size();
				meshDesc.points.stride = sizeof(Vec3);
				meshDesc.points.data = &verts[0];
				meshDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

				OutputStream writeBuffer(getAllocator());
				bool status = system.getCooking()->cookConvexMesh(meshDesc, writeBuffer);
				if (!status)
					return;

				InputStream readBuffer(writeBuffer.data, writeBuffer.size);
				physx::PxConvexMesh* mesh = system.getPhysics()->createConvexMesh(readBuffer);
				geom->convexMesh = mesh;
			}

			m_size = file.size();
			decrementDepCount();
		}
		else
		{
			onFailure();
		}

		fs.closeAsync(file);
	}


	IAllocator& PhysicsGeometry::getAllocator()
	{
		return static_cast<PhysicsGeometryManager*>(m_resource_manager.get(ResourceManager::PHYSICS))->getAllocator();
	}


	void PhysicsGeometry::doUnload(void)
	{
		getAllocator().deleteObject(m_geometry);
		m_geometry = nullptr;
		onEmpty();
	}


} // namespace Lumix