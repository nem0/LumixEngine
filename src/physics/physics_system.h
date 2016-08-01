#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"


namespace physx
{

	class PxControllerManager;
	class PxCooking;
	class PxPhysics;

} // namespace physx


namespace Lumix
{


class PhysicsSystem : public IPlugin
{
	friend class PhysicsScene;
	friend struct PhysicsSceneImpl;
	public:
		const char* getName() const override { return "physics"; }
		
		virtual physx::PxPhysics* getPhysics() = 0;
		virtual physx::PxCooking* getCooking() = 0;

	protected:
		PhysicsSystem() {}
};


} // namespace Lumix
