#pragma once
#include "engine/lumix.h"
#include "engine/iplugin.h"


struct dtCrowdAgent;


namespace Lumix
{


template <typename T> class DelegateList;


class NavigationScene : public IScene
{
public:
	virtual bool isFinished(Entity entity) = 0;
	virtual bool navigate(Entity entity, const struct Vec3& dest, float speed, float stop_distance) = 0;
	virtual void cancelNavigation(Entity entity) = 0;
	virtual void setActorActive(Entity entity, bool active) = 0;
	virtual float getAgentSpeed(Entity entity) = 0;
	virtual float getAgentYawDiff(Entity entity) = 0;
	virtual void setAgentRadius(ComponentHandle cmp, float radius) = 0;
	virtual float getAgentRadius(ComponentHandle cmp) = 0;
	virtual void setAgentHeight(ComponentHandle cmp, float height) = 0;
	virtual float getAgentHeight(ComponentHandle cmp) = 0;
	virtual void setAgentRootMotion(Entity, const Vec3& root_motion) = 0;
	virtual bool useAgentRootMotion(ComponentHandle cmp) = 0;
	virtual void setUseAgentRootMotion(ComponentHandle cmp, bool use_root_motion) = 0;
	virtual bool isGettingRootMotionFromAnim(ComponentHandle cmp) = 0;
	virtual void setIsGettingRootMotionFromAnim(ComponentHandle cmp, bool is) = 0;
	virtual bool generateNavmesh() = 0;
	virtual bool generateTile(int x, int z, bool keep_data) = 0;
	virtual bool generateTileAt(const Vec3& pos, bool keep_data) = 0;
	virtual bool load(const char* path) = 0;
	virtual bool save(const char* path) = 0;
	virtual void debugDrawNavmesh(const Vec3& pos, bool inner_boundaries, bool outer_boundaries, bool portals) = 0;
	virtual void debugDrawCompactHeightfield() = 0;
	virtual void debugDrawHeightfield() = 0;
	virtual void debugDrawContours() = 0;
	virtual void debugDrawPath(Entity entity) = 0;
	virtual const dtCrowdAgent* getDetourAgent(Entity entity) = 0;
	virtual bool isNavmeshReady() const = 0;
	virtual bool hasDebugDrawData() const = 0;
	virtual DelegateList<void(float)>& onUpdate() = 0;
};


} // namespace Lumix
