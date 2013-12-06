#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "core/crc32.h"
#include "editor/editor_server.h"
#include "editor/property_descriptor.h"
#include "engine/engine.h"
#include "physics/physics_scene.h"
#include "physics/physics_system_impl.h"
#include "universe/component_event.h"
#include "universe/entity_moved_event.h"


#pragma comment(lib, "PhysXVisualDebuggerSDKCHECKED.lib")
#pragma comment(lib, "PhysX3CHECKED_x86.lib")
#pragma comment(lib, "PhysX3CommonCHECKED_x86.lib")
#pragma comment(lib, "PhysX3ExtensionsCHECKED.lib")
#pragma comment(lib, "PhysX3CharacterKinematicCHECKED_x86.lib")
#pragma comment(lib, "PhysX3CookingCHECKED_x86.lib")


namespace Lux
{


static const uint32_t box_rigid_actor_type = crc32("box_rigid_actor");
static const uint32_t controller_type = crc32("physical_controller");


extern "C" IPlugin* createPlugin()
{
	return new PhysicsSystem();
}


struct CustomErrorCallback : public physx::PxErrorCallback
{
	virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line);
};


void PhysicsSystem::onCreateUniverse(Universe& universe)
{
	m_impl->m_scene = new PhysicsScene();
	m_impl->m_scene->create(*this, universe);

}


void PhysicsSystem::onDestroyUniverse(Universe& universe)
{
	m_impl->m_scene->destroy();
	delete m_impl->m_scene;
	m_impl->m_scene = 0;
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
	engine.getEditorServer()->registerProperty("box_rigid_actor", PropertyDescriptor(crc32("dynamic"), (PropertyDescriptor::BoolGetter)&PhysicsScene::getIsDynamic, (PropertyDescriptor::BoolSetter)&PhysicsScene::setIsDynamic));
	engine.getEditorServer()->registerProperty("box_rigid_actor", PropertyDescriptor(crc32("size"), (PropertyDescriptor::Vec3Getter)&PhysicsScene::getHalfExtents, (PropertyDescriptor::Vec3Setter)&PhysicsScene::setHalfExtents));
	engine.getEditorServer()->registerCreator(box_rigid_actor_type, *this);
	engine.getEditorServer()->registerCreator(controller_type, *this);

	m_impl = new PhysicsSystemImpl;
	m_impl->m_allocator = new physx::PxDefaultAllocator();
	m_impl->m_error_callback = new CustomErrorCallback();
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
	delete m_impl->m_allocator;
	delete m_impl->m_error_callback;
	delete m_impl;
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
	printf(message);
}


} // !namespace Lux



