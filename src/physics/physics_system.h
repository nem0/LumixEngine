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

struct CollisionLayers {
	u32 filter[32];
	char names[32][30];
	u32 count = 0;
};

struct PhysicsSystem : IPlugin
{
	friend struct PhysicsScene;
	friend struct PhysicsSceneImpl;

	const char* getName() const override { return "physics"; }
	
	virtual physx::PxPhysics* getPhysics() = 0;
	virtual physx::PxCooking* getCooking() = 0;
	virtual CollisionLayers& getCollisionLayers() = 0;
	virtual const char* getCollisionLayerName(int index) = 0;
	virtual void setCollisionLayerName(int index, const char* name) = 0;
	virtual bool canLayersCollide(int layer1, int layer2) = 0;
	virtual void setLayersCanCollide(int layer1, int layer2, bool can_collide) = 0;
	virtual int getCollisionsLayersCount() const = 0;
	virtual void addCollisionLayer() = 0;
	virtual void removeCollisionLayer() = 0;
};


} // namespace Lumix
