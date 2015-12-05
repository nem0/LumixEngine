#pragma once


#include "lumix.h"
#include "core/delegate_list.h"
#include "core/iallocator.h"
#include "core/vec.h"
#include "iplugin.h"


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
		static PhysicsScene* create(PhysicsSystem& system, UniverseContext& context, Engine& engine, IAllocator& allocator);
		static void destroy(PhysicsScene* scene);
		
		virtual ~PhysicsScene() {}
		virtual void update(float time_delta) = 0;
		virtual void render(RenderScene& render_scene) = 0;
		virtual bool raycast(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result) = 0;
		virtual PhysicsSystem& getSystem() const = 0;
		
		virtual bool isDynamic(ComponentIndex cmp) = 0;
		virtual void setIsDynamic(ComponentIndex cmp, bool) = 0;
		virtual Vec3 getHalfExtents(ComponentIndex cmp) = 0;
		virtual void setHalfExtents(ComponentIndex cmp, const Vec3& size) = 0;
		virtual const char* getShapeSource(ComponentIndex cmp) = 0;
		virtual void setShapeSource(ComponentIndex cmp, const char* str) = 0;
		virtual const char* getHeightmap(ComponentIndex cmp) = 0;
		virtual void setHeightmap(ComponentIndex cmp, const char* str) = 0;
		virtual float getHeightmapXZScale(ComponentIndex cmp) = 0;
		virtual void setHeightmapXZScale(ComponentIndex cmp, float scale) = 0;
		virtual float getHeightmapYScale(ComponentIndex cmp) = 0;
		virtual void setHeightmapYScale(ComponentIndex cmp, float scale) = 0;
		virtual DelegateList<void(Entity, Entity)>& onContact() = 0;
		virtual ComponentIndex getActorComponent(Entity entity) = 0;

		virtual void applyForceToActor(ComponentIndex cmp, const Vec3& force) = 0;
		virtual float getActorSpeed(ComponentIndex cmp) = 0;
		virtual void putToSleep(ComponentIndex cmp) = 0;

		virtual void moveController(ComponentIndex cmp, const Vec3& v, float dt) = 0;
		virtual ComponentIndex getController(Entity entity) = 0;
		virtual float getControllerRadius(ComponentIndex cmp) = 0;
		virtual float getControllerHeight(ComponentIndex cmp) = 0;
};


} // !namespace Lumix
