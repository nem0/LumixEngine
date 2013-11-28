#pragma once


#include <PxPhysicsAPI.h>


namespace Lux
{

	
struct PhysicsSystemImpl
{
	bool connect2VisualDebugger();

	physx::PxPhysics*			physics;
	physx::PxFoundation*		foundation;
	physx::PxControllerManager*	controller_manager;
	physx::PxAllocatorCallback*	allocator;
	physx::PxErrorCallback*		error_callback;
	physx::PxCooking*			cooking;
	class PhysicsScene*			scene;
};


};