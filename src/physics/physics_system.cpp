#include "physics_system.h"
#include <PxPhysicsAPI.h>
#include "physics_scene.h"
#include "universe/component_event.h"
#include "cooking/PxCooking.h"
#include "universe/entity_moved_event.h"
#include "physics_system_impl.h"
#include "editor/editor_properties.h"
#include "editor/property_descriptor.h"
#include "core/crc32.h"


#pragma comment(lib, "PhysXVisualDebuggerSDKCHECKED.lib")
#pragma comment(lib, "PhysX3CHECKED_x86.lib")
#pragma comment(lib, "PhysX3CommonCHECKED_x86.lib")
#pragma comment(lib, "PhysX3ExtensionsCHECKED.lib")
#pragma comment(lib, "PhysX3CharacterKinematicCHECKED_x86.lib")
#pragma comment(lib, "PhysX3CookingCHECKED_x86.lib")


namespace Lux
{


static const uint32_t physical_type = crc32("physical");
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
	m_impl->scene = new PhysicsScene();
	m_impl->scene->create(*this, universe);

}


void PhysicsSystem::onDestroyUniverse(Universe& universe)
{
	m_impl->scene->destroy();
	delete m_impl->scene;
	m_impl->scene = 0;
}


void PhysicsSystem::serialize(ISerializer& serializer)
{
	m_impl->scene->serialize(serializer);
}


void PhysicsSystem::deserialize(ISerializer& serializer)
{
	m_impl->scene->deserialize(serializer);
}


Component PhysicsSystem::createComponent(uint32_t component_type, const Entity& entity)
{
	if(component_type == controller_type)
	{
		return m_impl->scene->createController(entity);
	}
	else if(component_type == physical_type)
	{
		return m_impl->scene->createActor(entity);
	}
	return Component::INVALID;
}


void PhysicsSystem::update(float dt)
{
	m_impl->scene->update(dt);
}


bool PhysicsSystem::create(EditorPropertyMap& properties, ComponentCreatorList& creators)
{
	properties[crc32("physical")].push_back(PropertyDescriptor("source", (PropertyDescriptor::Getter)&PhysicsScene::getShapeSource, (PropertyDescriptor::Setter)&PhysicsScene::setShapeSource, PropertyDescriptor::FILE));
	properties[crc32("physical")].push_back(PropertyDescriptor("dynamic", (PropertyDescriptor::BoolGetter)&PhysicsScene::getIsDynamic, (PropertyDescriptor::BoolSetter)&PhysicsScene::setIsDynamic));

	creators.insert(physical_type, this);
	creators.insert(controller_type, this);

	m_impl = new PhysicsSystemImpl;
	m_impl->allocator = new physx::PxDefaultAllocator();
	m_impl->error_callback = new CustomErrorCallback();
	m_impl->foundation = PxCreateFoundation(
		PX_PHYSICS_VERSION,
		*m_impl->allocator,
		*m_impl->error_callback
	);

	m_impl->physics = PxCreatePhysics(
		PX_PHYSICS_VERSION,
		*m_impl->foundation,
		physx::PxTolerancesScale()
	);
	
	m_impl->controller_manager = PxCreateControllerManager(*m_impl->foundation);
	m_impl->cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_impl->foundation, physx::PxCookingParams());
	m_impl->connect2VisualDebugger();
	return true;
}


void PhysicsSystem::destroy()
{
	m_impl->controller_manager->release();
	m_impl->cooking->release();
	m_impl->physics->release();
	m_impl->foundation->release();
	delete m_impl->allocator;
	delete m_impl->error_callback;
	delete m_impl;
	m_impl = 0;
}


bool PhysicsSystemImpl::connect2VisualDebugger()
{
	if(physics->getPvdConnectionManager() == NULL)
		return false;

	const char* pvd_host_ip = "127.0.0.1";
	int port = 5425;
	unsigned int timeout = 100; 
	physx::PxVisualDebuggerConnectionFlags connectionFlags = physx::PxVisualDebuggerExt::getAllConnectionFlags();

	PVD::PvdConnection* theConnection = physx::PxVisualDebuggerExt::createConnection(physics->getPvdConnectionManager(), pvd_host_ip, port, timeout, connectionFlags);
	return theConnection != NULL;
}


void CustomErrorCallback::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line)
{
	printf(message);
}


} // !namespace Lux



