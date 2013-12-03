#pragma once


#include "core/lux.h"
#include "core/string.h"
#include "core/vec3.h"
#include "universe/universe.h"


namespace Lux
{


struct RaycastHit
{
	Vec3 position;
	Vec3 normal;
	Entity entity;
};


class LUX_PHYSICS_API PhysicsScene
{
	friend class PhysicsSystem;
	public:
		PhysicsScene();
		
		bool create(PhysicsSystem& system, Universe& universe);
		void destroy();
		void update(float time_delta);
		bool raycast(const Vec3& origin, const Vec3& dir, float distance, RaycastHit& result);
		Component createActor(Entity entity);
		Component createController(Entity entity);
		void destroyActor(Component cmp);
		PhysicsSystem& getSystem() const;
		void getShapeSource(Component cmp, string& str);
		void setShapeSource(Component cmp, const string& str);
		void getIsDynamic(Component cmp, bool& is);
		void setIsDynamic(Component cmp, const bool& is);
		void moveController(Component cmp, const Vec3& v, float dt);
	
		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer);

	private:
		struct PhysicsSceneImpl* m_impl;
};


} // !namespace Lux
