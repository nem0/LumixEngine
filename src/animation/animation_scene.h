#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"


struct lua_State;


namespace Lumix
{

class Animation;
struct IAllocator;
class OutputMemoryStream;
class Path;

namespace Anim
{
class Controller;
}


struct Animable
{
	float time;
	float time_scale;
	float start_time;
	Animation* animation;
	EntityRef entity;
};


struct AnimationScene : public IScene
{
	static AnimationScene* create(Engine& engine, IPlugin& plugin, Universe& universe, IAllocator& allocator);
	static void destroy(AnimationScene& scene);
	static void registerLuaAPI(lua_State* L);

	virtual const OutputMemoryStream& getEventStream() const = 0;
	virtual Path getPropertyAnimation(EntityRef entity) = 0;
	virtual void setPropertyAnimation(EntityRef entity, const Path& path) = 0;
	virtual bool isPropertyAnimatorEnabled(EntityRef entity) = 0;
	virtual void enablePropertyAnimator(EntityRef entity, bool enabled) = 0;
	virtual class Animation* getAnimableAnimation(EntityRef entity) = 0;
	virtual Path getAnimation(EntityRef entity) = 0;
	virtual void setAnimation(EntityRef entity, const Path& path) = 0;
	virtual void updateAnimable(EntityRef entity, float time_delta) = 0;
	virtual void updateController(EntityRef entity, float time_delta) = 0;
	virtual Animable& getAnimable(EntityRef entity) = 0;
	virtual void setControllerInput(EntityRef entity, int input_idx, int value) = 0;
	virtual void setControllerInput(EntityRef entity, int input_idx, float value) = 0;
	virtual void setControllerInput(EntityRef entity, int input_idx, bool value) = 0;
	virtual struct LocalRigidTransform getControllerRootMotion(EntityRef entity) = 0;
	virtual void setControllerSource(EntityRef entity, const Path& path) = 0;
	virtual class Path getControllerSource(EntityRef entity) = 0;
	virtual int getControllerInputIndex(EntityRef entity, const char* name) const = 0;
	virtual void applyControllerSet(EntityRef entity, const char* set_name) = 0;
	virtual void setControllerDefaultSet(EntityRef entity, int set) = 0;
	virtual int getControllerDefaultSet(EntityRef entity) = 0;
	virtual Anim::Controller* getControllerResource(EntityRef entity) = 0;
	virtual float getAnimationLength(int animation_idx) = 0;
};


} // ~ namespace Lumix
