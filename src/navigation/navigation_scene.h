#pragma once
#include "engine/lumix.h"
#include "engine/math.h"
#include "engine/plugin.h"


struct dtCrowdAgent;


namespace Lumix
{


struct IAllocator;
template <typename T> struct DelegateList;


struct NavmeshZone {
	Vec3 extents;
};


struct NavigationScene : IScene
{
	static NavigationScene* create(Engine& engine, IPlugin& system, Universe& universe, IAllocator& allocator);
	static void destroy(NavigationScene& scene);

	virtual NavmeshZone& getZone(EntityRef entity) = 0;
	virtual bool isFinished(EntityRef entity) = 0;
	virtual bool navigate(EntityRef entity, const struct DVec3& dest, float speed, float stop_distance) = 0;
	virtual void cancelNavigation(EntityRef entity) = 0;
	virtual void setActorActive(EntityRef entity, bool active) = 0;
	virtual float getAgentSpeed(EntityRef entity) = 0;
	virtual float getAgentYawDiff(EntityRef entity) = 0;
	virtual void setAgentRadius(EntityRef entity, float radius) = 0;
	virtual float getAgentRadius(EntityRef entity) = 0;
	virtual void setAgentHeight(EntityRef entity, float height) = 0;
	virtual float getAgentHeight(EntityRef entity) = 0;
	virtual void setAgentRootMotion(EntityRef entity, const Vec3& root_motion) = 0;
	virtual bool useAgentRootMotion(EntityRef entity) = 0;
	virtual void setUseAgentRootMotion(EntityRef entity, bool use_root_motion) = 0;
	virtual bool isGettingRootMotionFromAnim(EntityRef entity) = 0;
	virtual void setIsGettingRootMotionFromAnim(EntityRef entity, bool is) = 0;
	virtual bool generateNavmesh(EntityRef zone) = 0;
	virtual bool generateTileAt(EntityRef zone, const DVec3& pos, bool keep_data) = 0;
	virtual bool load(EntityRef zone_entity, const char* path) = 0;
	virtual bool save(EntityRef zone_entity, const char* path) = 0;
	virtual void debugDrawNavmesh(EntityRef zone, const DVec3& pos, bool inner_boundaries, bool outer_boundaries, bool portals) = 0;
	virtual void debugDrawCompactHeightfield(EntityRef zone) = 0;
	virtual void debugDrawHeightfield(EntityRef zone) = 0;
	virtual void debugDrawContours(EntityRef zone) = 0;
	virtual void debugDrawPath(EntityRef agent_entity) = 0;
	virtual const dtCrowdAgent* getDetourAgent(EntityRef entity) = 0;
	virtual bool isNavmeshReady(EntityRef zone) const = 0;
	virtual bool hasDebugDrawData(EntityRef zoneko) const = 0;
	virtual void setGeneratorParams(float cell_size,
		float cell_height,
		float agent_radius,
		float agent_height,
		float walkable_angle,
		float max_climb) = 0;

};


} // namespace Lumix
