#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "core/crc32.h"
#include "core/log.h"
#include "editor/editor_server.h"
#include "editor/property_descriptor.h"
#include "engine/engine.h"
#include "physics/physics_scene.h"
#include "physics/physics_system_impl.h"


namespace Lumix
{


static const uint32_t BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32_t MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
static const uint32_t CONTROLLER_HASH = crc32("physical_controller");
static const uint32_t HEIGHTFIELD_HASH = crc32("physical_heightfield");


extern "C" IPlugin* createPlugin()
{
	return LUMIX_NEW(PhysicsSystem)();
}


struct CustomErrorCallback : public physx::PxErrorCallback
{
	virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line);
};


void PhysicsSystem::onCreateUniverse(Universe& universe)
{
	m_impl->m_scene = LUMIX_NEW(PhysicsScene)();
	m_impl->m_scene->create(*this, universe, *m_impl->m_engine);
}


void PhysicsSystem::onDestroyUniverse(Universe& universe)
{
	m_impl->m_scene->destroy();
	LUMIX_DELETE(m_impl->m_scene);
	m_impl->m_scene = NULL;
}


void PhysicsSystem::serialize(ISerializer& serializer)
{
	m_impl->m_scene->serialize(serializer);
}


void PhysicsSystem::deserialize(ISerializer& serializer)
{
	m_impl->m_scene->deserialize(serializer);
}


void PhysicsSystem::sendMessage(const char* message)
{
	if(strcmp("render", message) == 0)
	{
		m_impl->m_scene->render();
	}
}


Component PhysicsSystem::createComponent(uint32_t component_type, const Entity& entity)
{
	if (component_type == HEIGHTFIELD_HASH)
	{
		return m_impl->m_scene->createHeightfield(entity);
	}
	else if (component_type == CONTROLLER_HASH)
	{
		return m_impl->m_scene->createController(entity);
	}
	else if (component_type == BOX_ACTOR_HASH)
	{
		return m_impl->m_scene->createBoxRigidActor(entity);
	}
	else if (component_type == MESH_ACTOR_HASH)
	{
		return m_impl->m_scene->createMeshRigidActor(entity);
	}
	return Component::INVALID;
}


void PhysicsSystem::update(float dt)
{
	m_impl->m_scene->update(dt);
}


PhysicsScene* PhysicsSystem::getScene() const
{
	return m_impl->m_scene;
}


class AssertNullAllocator : public physx::PxAllocatorCallback
{
	public:
		virtual void* allocate(size_t size, const char* typeName, const char* filename, int line) override
		{
			void* ret = _aligned_malloc(size, 16);
			//g_log_info.log("PhysX") << "Allocated " << size << " bytes for " << typeName << " from " << filename << "(" << line << ")";
			ASSERT(ret);
			return ret;
		}
		virtual void deallocate(void* ptr) override
		{
			_aligned_free(ptr);
		}
};


bool PhysicsSystem::create(Engine& engine)
{
	engine.getEditorServer()->registerProperty("box_rigid_actor", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)(crc32("dynamic"), &PhysicsScene::getIsDynamic, &PhysicsScene::setIsDynamic));
	engine.getEditorServer()->registerProperty("box_rigid_actor", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)(crc32("size"), &PhysicsScene::getHalfExtents, &PhysicsScene::setHalfExtents));
	engine.getEditorServer()->registerProperty("mesh_rigid_actor", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)(crc32("source"), &PhysicsScene::getShapeSource, &PhysicsScene::setShapeSource, IPropertyDescriptor::FILE));
	engine.getEditorServer()->registerProperty("physical_heightfield", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)(crc32("heightmap"), &PhysicsScene::getHeightmap, &PhysicsScene::setHeightmap, IPropertyDescriptor::FILE));
	engine.getEditorServer()->registerProperty("physical_heightfield", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)(crc32("xz_scale"), &PhysicsScene::getHeightmapXZScale, &PhysicsScene::setHeightmapXZScale));
	engine.getEditorServer()->registerProperty("physical_heightfield", LUMIX_NEW(PropertyDescriptor<PhysicsScene>)(crc32("y_scale"), &PhysicsScene::getHeightmapYScale, &PhysicsScene::setHeightmapYScale));
	engine.getEditorServer()->registerCreator(HEIGHTFIELD_HASH, *this);
	engine.getEditorServer()->registerCreator(BOX_ACTOR_HASH, *this);
	engine.getEditorServer()->registerCreator(MESH_ACTOR_HASH, *this);
	engine.getEditorServer()->registerCreator(CONTROLLER_HASH, *this);

	m_impl = LUMIX_NEW(PhysicsSystemImpl);
	m_impl->m_allocator = LUMIX_NEW(AssertNullAllocator)();
	m_impl->m_error_callback = LUMIX_NEW(CustomErrorCallback)();
	m_impl->m_engine = &engine;
	m_impl->m_foundation = PxCreateFoundation(
		PX_PHYSICS_VERSION,
		*m_impl->m_allocator,
		*m_impl->m_error_callback
	);

	m_impl->m_physics = PxCreatePhysics(
		PX_PHYSICS_VERSION,
		*m_impl->m_foundation,
		physx::PxTolerancesScale()
	);
	
	m_impl->m_controller_manager = PxCreateControllerManager(*m_impl->m_foundation);
	m_impl->m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_impl->m_foundation, physx::PxCookingParams());
	m_impl->connect2VisualDebugger();
	return true;
}


void PhysicsSystem::destroy()
{
	m_impl->m_controller_manager->release();
	m_impl->m_cooking->release();
	m_impl->m_physics->release();
	m_impl->m_foundation->release();
	LUMIX_DELETE(m_impl->m_allocator);
	LUMIX_DELETE(m_impl->m_error_callback);
	LUMIX_DELETE(m_impl);
	m_impl = 0;
}


bool PhysicsSystemImpl::connect2VisualDebugger()
{
	if(m_physics->getPvdConnectionManager() == NULL)
		return false;

	const char* pvd_host_ip = "127.0.0.1";
	int port = 5425;
	unsigned int timeout = 100; 
	physx::PxVisualDebuggerConnectionFlags connectionFlags = physx::PxVisualDebuggerExt::getAllConnectionFlags();

	PVD::PvdConnection* theConnection = physx::PxVisualDebuggerExt::createConnection(m_physics->getPvdConnectionManager(), pvd_host_ip, port, timeout, connectionFlags);
	return theConnection != NULL;
}


void CustomErrorCallback::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line)
{
	g_log_error.log("PhysX") << message;
}


} // !namespace Lumix



