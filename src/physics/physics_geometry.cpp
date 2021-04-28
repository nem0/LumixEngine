#include <cooking/PxConvexMeshDesc.h>
#include <foundation/PxIO.h>
#include <geometry/PxTriangleMesh.h>
#include <PxPhysics.h>

#include "physics_geometry.h"
#include "engine/log.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "engine/math.h"
#include "physics/physics_system.h"


namespace Lumix
{


struct InputStream final : physx::PxInputStream
{
	InputStream(InputMemoryStream& blob) : blob(blob) {}

	physx::PxU32 read(void* dest, physx::PxU32 count) override {
		if (blob.read(dest, count)) return count;
		return 0;
	}

	InputMemoryStream& blob;
};


const ResourceType PhysicsGeometry::TYPE("physics");


PhysicsGeometry::PhysicsGeometry(const Path& path, ResourceManager& resource_manager, PhysicsSystem& system, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, system(system)
	, convex_mesh(nullptr)
	, tri_mesh(nullptr)
{
}

PhysicsGeometry::~PhysicsGeometry() = default;


bool PhysicsGeometry::load(u64 size, const u8* mem)
{
	PROFILE_FUNCTION();
	Header header;
	InputMemoryStream file(mem, size);
 	file.read(&header, sizeof(header));

	if (header.m_magic != HEADER_MAGIC) {
 		logError("Corrupted geometry ", getPath());
 		return false;
 	}
 
	if (header.m_version <= (u32)Versions::COOKED) {
		logError(getPath(), ": built version too old, please rebuild your data.");
		return false;
	}

	if (header.m_version > (u32)Versions::LAST) {
		logError("Unsupported version of geometry ", getPath());
		return false;
	}


	const bool is_convex = header.m_convex != 0;
	InputStream read_buffer(file);
	if (is_convex) {
		convex_mesh = system.getPhysics()->createConvexMesh(read_buffer);
	} else {
		tri_mesh = system.getPhysics()->createTriangleMesh(read_buffer);
 	}
 
	return true;
}


void PhysicsGeometry::unload()
{
	if (convex_mesh) convex_mesh->release();
	if (tri_mesh) tri_mesh->release();
	convex_mesh = nullptr;
	tri_mesh = nullptr;
}


} // namespace Lumix