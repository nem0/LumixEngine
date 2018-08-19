#pragma once
#include "engine/lumix.h"
#include "engine/iplugin.h"


struct dtCrowdAgent;


namespace Lumix
{


struct IAllocator;
template <typename T> class DelegateList;


class NavigationScene : public IScene
{
public:
	static NavigationScene* create(Engine& engine, IPlugin& system, Universe& universe, IAllocator& allocator);
	static void destroy(NavigationScene& scene);

	virtual bool isFinished(EntityRef entity) = 0;
	virtual bool navigate(EntityRef entity, const struct Vec3& dest, float speed, float stop_distance) = 0;
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
	virtual bool generateNavmesh() = 0;
	virtual bool generateTile(int x, int z, bool keep_data) = 0;
	virtual bool generateTileAt(const Vec3& pos, bool keep_data) = 0;
	virtual bool load(const char* path) = 0;
	virtual bool save(const char* path) = 0;
	virtual int getPolygonCount() = 0;
	virtual void debugDrawNavmesh(const Vec3& pos, bool inner_boundaries, bool outer_boundaries, bool portals) = 0;
	virtual void debugDrawCompactHeightfield() = 0;
	virtual void debugDrawHeightfield() = 0;
	virtual void debugDrawContours() = 0;
	virtual void debugDrawPath(EntityRef entity) = 0;
	virtual const dtCrowdAgent* getDetourAgent(EntityRef entity) = 0;
	virtual bool isNavmeshReady() const = 0;
	virtual bool hasDebugDrawData() const = 0;
	virtual DelegateList<void(float)>& onUpdate() = 0;
	virtual void setGeneratorParams(float cell_size,
		float cell_height,
		float agent_radius,
		float agent_height,
		float walkable_angle,
		float max_climb) = 0;

};


} // namespace Lumix
