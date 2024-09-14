#include <cooking/PxConvexMeshDesc.h>
#include <foundation/PxIO.h>
#include <geometry/PxTriangleMesh.h>
#include <PxMaterial.h>
#include <PxPhysics.h>

#include "core/log.h"
#include "core/math.h"
#include "core/profiler.h"
#include "core/stream.h"
#include "core/string.h"
#include "core/tokenizer.h"
#include "engine/resource_manager.h"
#include "physics_resources.h"
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


const ResourceType PhysicsGeometry::TYPE("physics_geometry");


PhysicsGeometry::PhysicsGeometry(const Path& path, ResourceManager& resource_manager, PhysicsSystem& system, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, system(system)
	, convex_mesh(nullptr)
	, tri_mesh(nullptr)
{
}

PhysicsGeometry::~PhysicsGeometry() = default;


bool PhysicsGeometry::load(Span<const u8> mem) {
	PROFILE_FUNCTION();
	Header header;
	InputMemoryStream file(mem);
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


PhysicsMaterial::PhysicsMaterial(const Path& path, ResourceManager& resource_manager, PhysicsSystem& system, IAllocator& allocator)
	: Resource(path, resource_manager, allocator) 
	, system(system)
{
	material = system.getPhysics()->createMaterial(0.5f, 0.5f, 0.1f);
}

PhysicsMaterial::~PhysicsMaterial() {
	if (material) {
		material->release();
		material = nullptr;
	}
}

ResourceType PhysicsMaterial::TYPE("physics_material");

void PhysicsMaterial::unload() {}

struct PhysicsMaterialLoadData {
	float static_friction = 0.5f;
	float dynamic_friction = 0.5f;
	float restitution = 0.1f;
};

void PhysicsMaterial::serialize(OutputMemoryStream& blob) {
	PhysicsMaterialLoadData data;
	data.static_friction = material->getStaticFriction();
	data.dynamic_friction = material->getDynamicFriction();
	data.restitution = material->getRestitution();
	blob.write(data);
}

void PhysicsMaterial::deserialize(InputMemoryStream& blob) {
	PhysicsMaterialLoadData data;
	blob.read(data);

	material->setStaticFriction(data.static_friction);
	material->setDynamicFriction(data.dynamic_friction);
	material->setRestitution(data.restitution);
}

bool PhysicsMaterial::load(Span<const u8> mem) {
	Tokenizer tokenizer(StringView((const char*)mem.begin(), mem.length()), getPath().c_str());
	for (;;) {
		Tokenizer::Token token = tokenizer.tryNextToken();
		switch (token.type) {
			case Tokenizer::Token::EOF: return true;
			case Tokenizer::Token::ERROR: return false;
			default: break;
		}

		if (token == "static_friction") {
			float v;
			if (!tokenizer.consume(v)) return false;
			material->setStaticFriction(v);
		}
		else if (token == "dynamic_friction") {
			float v;
			if (!tokenizer.consume(v)) return false;
			material->setDynamicFriction(v);
		}
		else if (token == "restitution") {
			float v;
			if (!tokenizer.consume(v)) return false;
			material->setRestitution(v);
		}
		else {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): Unknown token ", token.value);
			tokenizer.logErrorPosition(token.value.begin);
			return false;
		}
	}
}

PhysicsMaterialManager::PhysicsMaterialManager(PhysicsSystem& system, IAllocator& allocator)
	: ResourceManager(allocator)
	, system(system)
	, allocator(allocator)
{}

PhysicsMaterialManager::~PhysicsMaterialManager() {
}

Resource* PhysicsMaterialManager::createResource(const Path& path) {
	return LUMIX_NEW(m_allocator, PhysicsMaterial)(path, *this, system, allocator);
}

void PhysicsMaterialManager::destroyResource(Resource& resource) {
	LUMIX_DELETE(m_allocator, static_cast<PhysicsMaterial*>(&resource));
}

} // namespace Lumix