#pragma once


#include "engine/lumix.h"
#include "engine/resource.h"


namespace physx
{
	class PxTriangleMesh;
	class PxConvexMesh;
}


namespace Lumix
{


struct PhysicsSystem;


struct PhysicsGeometry final : Resource
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
		PhysicsGeometry(const Path& path, ResourceManager& resource_manager, PhysicsSystem& system, IAllocator& allocator);
		~PhysicsGeometry();

		ResourceType getType() const override { return TYPE; }


	public:
		physx::PxTriangleMesh* tri_mesh;
		physx::PxConvexMesh* convex_mesh;

	private:
		PhysicsSystem& system;
		IAllocator& allocator;

		void unload() override;
		bool load(u64 size, const u8* mem) override;

};


} // namespace Lumix