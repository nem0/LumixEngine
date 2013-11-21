#include "physics_system.h"
#include <PxPhysicsAPI.h>
#include "physics_scene.h"
#include "universe/component_event.h"
#include "cooking/PxCooking.h"
#include "universe/entity_moved_event.h"
#include "physics_system_impl.h"


#ifndef DISABLE_PHYSICS


#pragma comment(lib, "PhysXVisualDebuggerSDKCHECKED.lib")
#pragma comment(lib, "PhysX3CHECKED_x86.lib")
#pragma comment(lib, "PhysX3CommonCHECKED_x86.lib")
#pragma comment(lib, "PhysX3ExtensionsCHECKED.lib")
#pragma comment(lib, "PhysX3CharacterKinematicCHECKED_x86.lib")
#pragma comment(lib, "PhysX3CookingCHECKED_x86.lib")


#endif // DISABLE_PHYSICS

namespace Lux
{


#ifndef DISABLE_PHYSICS


struct CustomErrorCallback : public physx::PxErrorCallback
{
	virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line);
};


bool PhysicsSystem::create()
{
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
	return m_impl->connect2VisualDebugger();
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

	const char*     pvd_host_ip = "127.0.0.1";
	int             port        = 5425;
	unsigned int    timeout     = 100; 
	physx::PxVisualDebuggerConnectionFlags connectionFlags = physx::PxVisualDebuggerExt::getAllConnectionFlags();

	PVD::PvdConnection* theConnection = physx::PxVisualDebuggerExt::createConnection(physics->getPvdConnectionManager(), pvd_host_ip, port, timeout, connectionFlags);
	return theConnection != NULL;
}


void CustomErrorCallback::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line)
{
	printf(message);
}


#else // DISABLE_PHYSICS


bool PhysicsSystem::create() { return true; }
void PhysicsSystem::destroy() {}


#endif // DISABLE_PHYSICS


} // !namespace Lux



