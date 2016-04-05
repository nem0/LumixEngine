#pragma once


#include "lumix.h"
#include "core/iallocator.h"
#include "core/path.h"
#include "core/vec.h"
#include "iplugin.h"


struct lua_State;


namespace Lumix
{


class Engine;
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
	friend class PhysicsSystem;
	public:
		static PhysicsScene* create(PhysicsSystem& system, Universe& context, Engine& engine, IAllocator& allocator);
		static void destroy(PhysicsScene* scene);
		static void registerLuaAPI(lua_State* L);

		virtual ~PhysicsScene() {}
		virtual void render(RenderScene& render_scene) = 0;
		virtual Entity raycast(const Vec3& origin, const Vec3& dir) = 0;
		virtual bool raycastEx(const Vec3& origin,
			const Vec3& dir,
			float distance,
			RaycastHit& result) = 0;
		virtual PhysicsSystem& getSystem() const = 0;

		virtual ComponentIndex getActorComponent(Entity entity) = 0;
		virtual void setActorLayer(ComponentIndex cmp, int layer) = 0;
		virtual int getActorLayer(ComponentIndex cmp) = 0;
		virtual bool isDynamic(ComponentIndex cmp) = 0;
		virtual void setIsDynamic(ComponentIndex cmp, bool) = 0;
		virtual Vec3 getHalfExtents(ComponentIndex cmp) = 0;
		virtual void setHalfExtents(ComponentIndex cmp, const Vec3& size) = 0;
		virtual Path getShapeSource(ComponentIndex cmp) = 0;
		virtual void setShapeSource(ComponentIndex cmp, const Path& str) = 0;
		virtual Path getHeightmap(ComponentIndex cmp) = 0;
		virtual void setHeightmap(ComponentIndex cmp, const Path& path) = 0;
		virtual float getHeightmapXZScale(ComponentIndex cmp) = 0;
		virtual void setHeightmapXZScale(ComponentIndex cmp, float scale) = 0;
		virtual float getHeightmapYScale(ComponentIndex cmp) = 0;
		virtual void setHeightmapYScale(ComponentIndex cmp, float scale) = 0;
		virtual int getHeightfieldLayer(ComponentIndex cmp) = 0;
		virtual void setHeightfieldLayer(ComponentIndex cmp, int layer) = 0;

		virtual void applyForceToActor(ComponentIndex cmp, const Vec3& force) = 0;
		virtual float getActorSpeed(ComponentIndex cmp) = 0;
		virtual void putToSleep(ComponentIndex cmp) = 0;

		virtual void moveController(ComponentIndex cmp, const Vec3& v, float dt) = 0;
		virtual ComponentIndex getController(Entity entity) = 0;
		virtual int getControllerLayer(ComponentIndex cmp) = 0;
		virtual void setControllerLayer(ComponentIndex cmp, int layer) = 0;
		virtual float getControllerRadius(ComponentIndex cmp) = 0;
		virtual float getControllerHeight(ComponentIndex cmp) = 0;

		virtual const char* getCollisionLayerName(int index) = 0;
		virtual void setCollisionLayerName(int index, const char* name) = 0;
		virtual bool canLayersCollide(int layer1, int layer2) = 0;
		virtual void setLayersCanCollide(int layer1, int layer2, bool can_collide) = 0;
		virtual int getCollisionsLayersCount() const = 0;
		virtual void addCollisionLayer() = 0;
		virtual void removeCollisionLayer() = 0;
};


} // !namespace Lumix
