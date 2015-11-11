#pragma once


#include "lumix.h"
#include "core/resource.h"
#include "core/resource_manager_base.h"


namespace physx
{
	class PxGeometry;
}


namespace Lumix
{


class PhysicsSystem;


class LUMIX_PHYSICS_API PhysicsGeometryManager : public ResourceManagerBase
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
		virtual Resource* createResource(const Path& path) override;
		virtual void destroyResource(Resource& resource) override;

	private:
		IAllocator& m_allocator;
		PhysicsSystem& m_system;
};


class LUMIX_PHYSICS_API PhysicsGeometry : public Resource
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
		PhysicsGeometry(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		~PhysicsGeometry();

		physx::PxGeometry* getGeometry() { return m_geometry; }

	private:
		IAllocator& getAllocator();

		virtual void unload(void) override;
		virtual bool load(FS::IFile& file) override;

	private:
		physx::PxGeometry* m_geometry;
		bool m_is_convex;
};


} // namespace Lumix