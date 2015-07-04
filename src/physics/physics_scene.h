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
		
		virtual bool isDynamic(Component cmp) = 0;
		virtual void setIsDynamic(Component cmp, bool) = 0;
		virtual Vec3 getHalfExtents(Component cmp) = 0;
		virtual void setHalfExtents(Component cmp, const Vec3& size) = 0;
		virtual void getShapeSource(Component cmp, string& str) = 0;
		virtual void setShapeSource(Component cmp, const string& str) = 0;
		virtual void getHeightmap(Component cmp, string& str) = 0;
		virtual void setHeightmap(Component cmp, const string& str) = 0;
		virtual float getHeightmapXZScale(Component cmp) = 0;
		virtual void setHeightmapXZScale(Component cmp, float scale) = 0;
		virtual float getHeightmapYScale(Component cmp) = 0;
		virtual void setHeightmapYScale(Component cmp, float scale) = 0;

		virtual void moveController(Component cmp, const Vec3& v, float dt) = 0;
		virtual Component getController(const Entity& entity) = 0;
};


} // !namespace Lumix
