#pragma once


#include <PxPhysicsAPI.h>


namespace Lumix
{

	
struct PhysicsSystemImpl
{
	bool connect2VisualDebugger();

	physx::PxPhysics*			m_physics;
	physx::PxFoundation*		m_foundation;
	physx::PxControllerManager*	m_controller_manager;
	physx::PxAllocatorCallback*	m_allocator;
	physx::PxErrorCallback*		m_error_callback;
	physx::PxCooking*			m_cooking;
	class Engine*				m_engine;
};


};