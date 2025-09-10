#pragma once

#include "core/allocator.h"
#include "core/math.h"

#include "engine/plugin.h"


struct dtCrowdAgent;


//@ module NavigationModule navigation "Navigation"
namespace Lumix
{


struct IAllocator;


//@ component_struct label "Zone" name Zone
struct NavmeshZone {
	enum Flags {
		AUTOLOAD = 1 << 0,
		DETAILED = 1 << 1
	};
	Vec3 extents;						//@ property 
	u64 guid;
	u32 flags;

	float cell_size = 0.3f;				//@ property min 0
	float cell_height = 0.1f;			//@ property min 0
	float walkable_slope_angle = 60.f;	//@ property min 0
	float agent_height = 1.8f;			//@ property min 0
	float agent_radius = 0.3f;			//@ property min 0
	float max_climb = 0.3f;				//@ property min 0
};
//@ end

struct NavmeshBuildJob {
	virtual ~NavmeshBuildJob() {}
	virtual bool isFinished() = 0;
	virtual float getProgress() = 0;
};

struct NavigationModule : IModule {
	static UniquePtr<NavigationModule> create(Engine& engine, ISystem& system, World& world, IAllocator& allocator);
	static void reflect();

	virtual NavmeshZone& getZone(EntityRef entity) = 0;
	//@ component Zone id navmesh_zone
	virtual bool loadZone(EntityRef zone_entity) = 0;	//@ alias load
	virtual void debugDrawNavmesh(EntityRef zone, const DVec3& pos, bool inner_boundaries, bool outer_boundaries, bool portals) = 0; //@ alias drawNavmesh
	virtual void debugDrawCompactHeightfield(EntityRef zone) = 0;	//@ alias drawCompactHeightfield
	virtual void debugDrawHeightfield(EntityRef zone) = 0;			//@ alias drawHeightfield
	virtual void debugDrawContours(EntityRef zone) = 0;				//@ alias drawContours
	virtual NavmeshBuildJob* generateNavmesh(EntityRef zone) = 0;
	virtual bool getZoneAutoload(EntityRef entity) = 0;
	virtual void setZoneAutoload(EntityRef entity, bool value) = 0;
	virtual bool getZoneDetailed(EntityRef entity) = 0;
	virtual void setZoneDetailed(EntityRef entity, bool value) = 0;
	virtual bool saveZone(EntityRef zone_entity) = 0;
	//@ end
	virtual void createZone(EntityRef entity) = 0;
	virtual void destroyZone(EntityRef entity) = 0;

	//@ component Agent id navmesh_agent
	virtual void setAgentRadius(EntityRef entity, float radius) = 0;
	virtual float getAgentRadius(EntityRef entity) = 0;
	virtual void setAgentHeight(EntityRef entity, float height) = 0;
	virtual float getAgentHeight(EntityRef entity) = 0;
	virtual bool getAgentMoveEntity(EntityRef entity) = 0;
	virtual void setAgentMoveEntity(EntityRef entity, bool value) = 0;
	virtual bool navigate(EntityRef entity, const struct DVec3& dest, float speed, float stop_distance) = 0;
	virtual void cancelNavigation(EntityRef entity) = 0;
	virtual void debugDrawPath(EntityRef agent_entity, bool include_polygons) = 0;	//@ alias drawPath
	virtual float getAgentSpeed(EntityRef entity) = 0;
	//@ end
	virtual void createAgent(EntityRef entity) = 0;
	virtual void destroyAgent(EntityRef entity) = 0;
	
	virtual bool isFinished(EntityRef entity) = 0;
	virtual float getAgentYawDiff(EntityRef entity) = 0;
	virtual void free(NavmeshBuildJob* job) = 0;
	virtual bool generateTileAt(EntityRef zone, const DVec3& pos, bool keep_data) = 0;
	virtual const dtCrowdAgent* getDetourAgent(EntityRef entity) = 0;
	virtual bool isNavmeshReady(EntityRef zone) const = 0;
	virtual bool hasDebugDrawData(EntityRef zoneko) const = 0;
};


} // namespace Lumix
