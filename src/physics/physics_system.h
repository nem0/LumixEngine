#pragma once


#include "core/lumix.h"
#include "engine/iplugin.h"


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
		virtual const char* getName() const override { return "physics"; }
		
		virtual physx::PxControllerManager* getControllerManager() = 0;
		virtual physx::PxPhysics* getPhysics() = 0;
		virtual physx::PxCooking* getCooking() = 0;

	protected:
		PhysicsSystem() {}
};


extern "C"
{
	LUMIX_PHYSICS_API IPlugin* createPlugin(Engine& engine);
}


} // !namespace Lumix
