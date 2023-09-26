#pragma once


#include "engine/plugin.h"

namespace physx {
	class PxControllerManager;
	class PxPhysics;
} // namespace physx

namespace Lumix {

struct CollisionLayers {
	u32 filter[32];
	char names[32][30];
	u32 count = 0;
};

struct PhysicsSystem : ISystem {
	friend struct PhysicsModule;
	friend struct PhysicsModuleImpl;

	const char* getName() const override { return "physics"; }
	
	virtual physx::PxPhysics* getPhysics() = 0;
	virtual CollisionLayers& getCollisionLayers() = 0;
	virtual const char* getCollisionLayerName(int index) = 0;
	virtual void setCollisionLayerName(int index, const char* name) = 0;
	virtual bool canLayersCollide(int layer1, int layer2) = 0;
	virtual void setLayersCanCollide(int layer1, int layer2, bool can_collide) = 0;
	virtual int getCollisionsLayersCount() const = 0;
	virtual void addCollisionLayer() = 0;
	virtual void removeCollisionLayer() = 0;
	virtual bool cookTriMesh(Span<const struct Vec3> verts, Span<const u32> indices, struct IOutputStream& blob) = 0;
	virtual bool cookConvex(Span<const Vec3> verts, IOutputStream& blob) = 0;
};


} // namespace Lumix
