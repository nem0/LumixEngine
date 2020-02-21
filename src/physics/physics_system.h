#pragma once


#include "engine/plugin.h"


namespace physx
{

	class PxControllerManager;
	class PxCooking;
	class PxPhysics;

} // namespace physx


namespace Lumix
{


struct PhysicsSystem : IPlugin
{
	friend struct PhysicsScene;
	friend struct PhysicsSceneImpl;

	const char* getName() const override { return "physics"; }
	
	virtual physx::PxPhysics* getPhysics() = 0;
	virtual physx::PxCooking* getCooking() = 0;
};


} // namespace Lumix
