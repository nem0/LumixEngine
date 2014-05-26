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
#include "universe/component_event.h"
#include "universe/entity_moved_event.h"


namespace Lux
{


static const uint32_t box_rigid_actor_type = crc32("box_rigid_actor");
static const uint32_t controller_type = crc32("physical_controller");


extern "C" IPlugin* createPlugin()
{
	return LUX_NEW(PhysicsSystem)();
}


struct CustomErrorCallback : public physx::PxErrorCallback
{
	virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line);
};


void PhysicsSystem::onCreateUniverse(Universe& universe)
{
	m_impl->m_scene = LUX_NEW(PhysicsScene)();
	m_impl->m_scene->create(*this, universe);
}


void PhysicsSystem::onDestroyUniverse(Universe& universe)
{
	m_impl->m_scene->destroy();
	LUX_DELETE(m_impl->m_scene);
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
	if(component_type == controller_type)
	{
		return m_impl->m_scene->createController(entity);
	}
	else if(component_type == box_rigid_actor_type)
	{
		return m_impl->m_scene->createBoxRigidActor(entity);
	}
	return Component::INVALID;
}


void PhysicsSystem::update(float dt)
{
	m_impl->m_scene->update(dt);
}


bool PhysicsSystem::create(Engine& engine)
{
	engine.getEditorServer()->registerProperty("box_rigid_actor", LUX_NEW(PropertyDescriptor<PhysicsScene>)(crc32("dynamic"), &PhysicsScene::getIsDynamic, &PhysicsScene::setIsDynamic));
	engine.getEditorServer()->registerProperty("box_rigid_actor", LUX_NEW(PropertyDescriptor<PhysicsScene>)(crc32("size"), &PhysicsScene::getHalfExtents, &PhysicsScene::setHalfExtents));
	engine.getEditorServer()->registerCreator(box_rigid_actor_type, *this);
	engine.getEditorServer()->registerCreator(controller_type, *this);

	m_impl = LUX_NEW(PhysicsSystemImpl);
	m_impl->m_allocator = LUX_NEW(physx::PxDefaultAllocator)();
	m_impl->m_error_callback = LUX_NEW(CustomErrorCallback)();
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
	LUX_DELETE(m_impl->m_allocator);
	LUX_DELETE(m_impl->m_error_callback);
	LUX_DELETE(m_impl);
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
	g_log_error.log("PhysX", message);
}


} // !namespace Lux



