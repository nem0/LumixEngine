#pragma once


#include "engine/lumix.h"
#include "engine/resource.h"
#include "engine/resource_manager_base.h"


namespace physx
{
	class PxTriangleMesh;
	class PxConvexMesh;
}


namespace Lumix
{


class PhysicsSystem;


class PhysicsGeometryManager LUMIX_FINAL : public ResourceManagerBase
{
	public:
		PhysicsGeometryManager(PhysicsSystem& system, IAllocator& allocator)
			: ResourceManagerBase(allocator)
			, m_allocator(allocator)
			, m_system(system)
		{}
		~PhysicsGeometryManager() {}
		IAllocator& getAllocator() { return m_allocator; }
		PhysicsSystem& getSystem() { return m_system; }

	protected:
		Resource* createResource(const Path& path) override;
		void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		PhysicsSystem& m_system;
};


class PhysicsGeometry LUMIX_FINAL : public Resource
{
	public:
		static const uint32 HEADER_MAGIC = 0x5f4c5046; // '_LPF'
		struct Header
		{
			uint32 m_magic;
			uint32 m_version;
			uint32 m_convex;
		};

		enum class Versions : uint32
		{
			FIRST,

			LAST
		};

	public:
		PhysicsGeometry(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
		~PhysicsGeometry();

	public:
		physx::PxTriangleMesh* tri_mesh;
		physx::PxConvexMesh* convex_mesh;

	private:
		IAllocator& getAllocator();

		void unload(void) override;
		bool load(FS::IFile& file) override;

};


} // namespace Lumix