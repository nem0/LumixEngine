#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"
#include "engine/vec.h"

#ifdef STATIC_PLUGINS
	#define LUMIX_PHYSICS_API
#elif defined BUILDING_PHYSICS
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_PHYSICS_API LUMIX_LIBRARY_IMPORT
#endif


struct lua_State;


namespace Lumix
{


class Engine;
class IAllocator;
class Path;
class PhysicsSystem;
class RenderScene;
class Universe;


struct RaycastHit
{
	Vec3 position;
	Vec3 normal;
	Entity entity;
};


class LUMIX_PHYSICS_API PhysicsScene : public IScene
{
public:
	static PhysicsScene* create(PhysicsSystem& system, Universe& context, Engine& engine, IAllocator& allocator);
	static void destroy(PhysicsScene* scene);
	static void registerLuaAPI(lua_State* L);

	virtual ~PhysicsScene() {}
	virtual void render(RenderScene& render_scene) = 0;
	virtual Entity raycast(const Vec3& origin, const Vec3& dir) = 0;
	virtual bool raycastEx(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result) = 0;
	virtual PhysicsSystem& getSystem() const = 0;

	virtual ComponentHandle getActorComponent(Entity entity) = 0;
	virtual void setActorLayer(ComponentHandle cmp, int layer) = 0;
	virtual int getActorLayer(ComponentHandle cmp) = 0;
	virtual bool isDynamic(ComponentHandle cmp) = 0;
	virtual void setIsDynamic(ComponentHandle cmp, bool) = 0;
	virtual Vec3 getHalfExtents(ComponentHandle cmp) = 0;
	virtual void setHalfExtents(ComponentHandle cmp, const Vec3& size) = 0;
	virtual Path getShapeSource(ComponentHandle cmp) = 0;
	virtual void setShapeSource(ComponentHandle cmp, const Path& str) = 0;
	virtual Path getHeightmap(ComponentHandle cmp) = 0;
	virtual void setHeightmap(ComponentHandle cmp, const Path& path) = 0;
	virtual float getHeightmapXZScale(ComponentHandle cmp) = 0;
	virtual void setHeightmapXZScale(ComponentHandle cmp, float scale) = 0;
	virtual float getHeightmapYScale(ComponentHandle cmp) = 0;
	virtual void setHeightmapYScale(ComponentHandle cmp, float scale) = 0;
	virtual int getHeightfieldLayer(ComponentHandle cmp) = 0;
	virtual void setHeightfieldLayer(ComponentHandle cmp, int layer) = 0;

	virtual float getDistanceJointDamping(ComponentHandle cmp) = 0;
	virtual void setDistanceJointDamping(ComponentHandle cmp, float value) = 0;
	virtual float getDistanceJointStiffness(ComponentHandle cmp) = 0;
	virtual void setDistanceJointStiffness(ComponentHandle cmp, float value) = 0;
	virtual float getDistanceJointTolerance(ComponentHandle cmp) = 0;
	virtual void setDistanceJointTolerance(ComponentHandle cmp, float value) = 0;
	virtual Entity getDistanceJointConnectedBody(ComponentHandle cmp) = 0;
	virtual void setDistanceJointConnectedBody(ComponentHandle cmp, Entity entity) = 0;
	virtual Vec2 getDistanceJointLimits(ComponentHandle cmp) = 0;
	virtual void setDistanceJointLimits(ComponentHandle cmp, const Vec2& value) = 0;
	virtual Vec3 getDistanceJointLinearForce(ComponentHandle cmp) = 0;
	virtual int getDistanceJointCount() = 0;
	virtual ComponentHandle getDistanceJointComponent(int index) = 0;
	virtual Entity getDistanceJointEntity(ComponentHandle cmp) = 0;

	virtual Entity getHingeJointConnectedBody(ComponentHandle cmp) = 0;
	virtual void setHingeJointConnectedBody(ComponentHandle cmp, Entity entity) = 0;
	virtual float getHingeJointDamping(ComponentHandle cmp) = 0;
	virtual void setHingeJointDamping(ComponentHandle cmp, float value) = 0;
	virtual float getHingeJointStiffness(ComponentHandle cmp) = 0;
	virtual void setHingeJointStiffness(ComponentHandle cmp, float value) = 0;
	virtual Vec3 getHingeJointAxisPosition(ComponentHandle cmp) = 0;
	virtual void setHingeJointAxisPosition(ComponentHandle cmp, const Vec3& value) = 0;
	virtual Vec3 getHingeJointAxisDirection(ComponentHandle cmp) = 0;
	virtual void setHingeJointAxisDirection(ComponentHandle cmp, const Vec3& value) = 0;
	virtual bool getHingeJointUseLimit(ComponentHandle cmp) = 0;
	virtual void setHingeJointUseLimit(ComponentHandle cmp, bool use_limit) = 0;
	virtual Vec2 getHingeJointLimit(ComponentHandle cmp) = 0;
	virtual void setHingeJointLimit(ComponentHandle cmp, const Vec2& limit) = 0;
	virtual Vec3 getHingeJointConnectedBodyInitialPosition(ComponentHandle cmp) = 0;
	virtual int getHingeJointCount() = 0;
	virtual ComponentHandle getHingeJointComponent(int index) = 0;
	virtual Entity getHingeJointEntity(ComponentHandle cmp) = 0;

	virtual void applyForceToActor(ComponentHandle cmp, const Vec3& force) = 0;
	virtual float getActorSpeed(ComponentHandle cmp) = 0;
	virtual void putToSleep(ComponentHandle cmp) = 0;

	virtual void moveController(ComponentHandle cmp, const Vec3& v) = 0;
	virtual ComponentHandle getController(Entity entity) = 0;
	virtual int getControllerLayer(ComponentHandle cmp) = 0;
	virtual void setControllerLayer(ComponentHandle cmp, int layer) = 0;
	virtual float getControllerRadius(ComponentHandle cmp) = 0;
	virtual float getControllerHeight(ComponentHandle cmp) = 0;

	virtual const char* getCollisionLayerName(int index) = 0;
	virtual void setCollisionLayerName(int index, const char* name) = 0;
	virtual bool canLayersCollide(int layer1, int layer2) = 0;
	virtual void setLayersCanCollide(int layer1, int layer2, bool can_collide) = 0;
	virtual int getCollisionsLayersCount() const = 0;
	virtual void addCollisionLayer() = 0;
	virtual void removeCollisionLayer() = 0;
};


} // !namespace Lumix
