#pragma once


#include "core/lumix.h"
#include "core/string.h"
#include "core/vec3.h"
#include "engine/iplugin.h"
#include "universe/universe.h"


namespace Lumix
{


class Engine;
class RenderScene;


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
		static PhysicsScene* create(PhysicsSystem& system, Universe& universe, Engine& engine, IAllocator& allocator);
		static void destroy(PhysicsScene* scene);
		
		virtual ~PhysicsScene() {}
		virtual void update(float time_delta) = 0;
		virtual void render(RenderScene& render_scene) = 0;
		virtual bool raycast(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result) = 0;
		virtual PhysicsSystem& getSystem() const = 0;
		
		virtual bool isDynamic(ComponentNew cmp) = 0;
		virtual void setIsDynamic(ComponentNew cmp, bool) = 0;
		virtual Vec3 getHalfExtents(ComponentNew cmp) = 0;
		virtual void setHalfExtents(ComponentNew cmp, const Vec3& size) = 0;
		virtual void getShapeSource(ComponentNew cmp, string& str) = 0;
		virtual void setShapeSource(ComponentNew cmp, const string& str) = 0;
		virtual void getHeightmap(ComponentNew cmp, string& str) = 0;
		virtual void setHeightmap(ComponentNew cmp, const string& str) = 0;
		virtual float getHeightmapXZScale(ComponentNew cmp) = 0;
		virtual void setHeightmapXZScale(ComponentNew cmp, float scale) = 0;
		virtual float getHeightmapYScale(ComponentNew cmp) = 0;
		virtual void setHeightmapYScale(ComponentNew cmp, float scale) = 0;

		virtual void moveController(ComponentNew cmp, const Vec3& v, float dt) = 0;
		virtual ComponentNew getController(const Entity& entity) = 0;
};


} // !namespace Lumix
