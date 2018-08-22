#pragma once


#include "engine/lumix.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace physx
{
	class PxTriangleMesh;
	class PxConvexMesh;
}


namespace Lumix
{


class PhysicsSystem;


class PhysicsGeometryManager final : public ResourceManager
{
	public:
		PhysicsGeometryManager(PhysicsSystem& system, IAllocator& allocator)
			: ResourceManager(allocator)
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


class PhysicsGeometry final : public Resource
{
	public:
		static const u32 HEADER_MAGIC = 0x5f4c5046; // '_LPF'
		static const ResourceType TYPE;
		struct Header
		{
			u32 m_magic;
			u32 m_version;
			u32 m_convex;
		};

		enum class Versions : u32
		{
			FIRST,

			LAST
		};

	public:
		PhysicsGeometry(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);
		~PhysicsGeometry();

		ResourceType getType() const override { return TYPE; }


	public:
		physx::PxTriangleMesh* tri_mesh;
		physx::PxConvexMesh* convex_mesh;

	private:
		IAllocator& getAllocator();

		void unload() override;
		bool load(FS::IFile& file) override;

};


} // namespace Lumix