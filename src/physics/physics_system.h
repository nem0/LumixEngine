#pragma once


#include "lumix.h"
#include "iplugin.h"


namespace physx
{

	class PxControllerManager;
	class PxCooking;
	class PxPhysics;

} // namespace physx


namespace Lumix
{


class LUMIX_PHYSICS_API PhysicsSystem : public IPlugin
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


} // !namespace Lumix
