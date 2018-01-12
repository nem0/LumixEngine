#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"


struct lua_State;


namespace Lumix
{

struct IAllocator;
class OutputBlob;
class Path;

namespace Anim
{
struct ComponentInstance;
class ControllerResource;
}


struct AnimationScene : public IScene
{
	static AnimationScene* create(Engine& engine, IPlugin& plugin, Universe& universe, IAllocator& allocator);
	static void destroy(AnimationScene& scene);
	static void registerLuaAPI(lua_State* L);

	virtual const OutputBlob& getEventStream() const = 0;
	virtual Path getPropertyAnimation(Entity entity) = 0;
	virtual void setPropertyAnimation(Entity entity, const Path& path) = 0;
	virtual class Animation* getAnimableAnimation(Entity entity) = 0;
	virtual Path getAnimation(Entity entity) = 0;
	virtual void setAnimation(Entity entity, const Path& path) = 0;
	virtual float getAnimableTime(Entity entity) = 0;
	virtual void setAnimableTime(Entity entity, float time) = 0;
	virtual void updateAnimable(Entity entity, float time_delta) = 0;
	virtual void updateController(Entity entity, float time_delta) = 0;
	virtual float getAnimableTimeScale(Entity entity) = 0;
	virtual void setAnimableTimeScale(Entity entity, float time_scale) = 0;
	virtual float getAnimableStartTime(Entity entity) = 0;
	virtual void setAnimableStartTime(Entity entity, float time) = 0;
	virtual u8* getControllerInput(Entity entity) = 0;
	virtual void setControllerInput(Entity entity, int input_idx, int value) = 0;
	virtual void setControllerInput(Entity entity, int input_idx, float value) = 0;
	virtual void setControllerInput(Entity entity, int input_idx, bool value) = 0;
	virtual struct RigidTransform getControllerRootMotion(Entity entity) = 0;
	virtual void setControllerSource(Entity entity, const Path& path) = 0;
	virtual class Path getControllerSource(Entity entity) = 0;
	virtual Anim::ComponentInstance* getControllerRoot(Entity entity) = 0;
	virtual int getControllerInputIndex(Entity entity, const char* name) const = 0;
	virtual Entity getSharedControllerParent(Entity entity) = 0;
	virtual void setSharedControllerParent(Entity entity, Entity parent) = 0;
	virtual void applyControllerSet(Entity entity, const char* set_name) = 0;
	virtual void setControllerDefaultSet(Entity entity, int set) = 0;
	virtual int getControllerDefaultSet(Entity entity) = 0;
	virtual Anim::ControllerResource* getControllerResource(Entity entity) = 0;
	virtual float getAnimationLength(int animation_idx) = 0;
};


} // ~ namespace Lumix
