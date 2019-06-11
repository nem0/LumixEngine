#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"


struct lua_State;


namespace Lumix
{

struct IAllocator;
class OutputMemoryStream;
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

	virtual const OutputMemoryStream& getEventStream() const = 0;
	virtual Path getPropertyAnimation(EntityRef entity) = 0;
	virtual void setPropertyAnimation(EntityRef entity, const Path& path) = 0;
	virtual bool isPropertyAnimatorEnabled(EntityRef entity) = 0;
	virtual void enablePropertyAnimator(EntityRef entity, bool enabled) = 0;
	virtual class Animation* getAnimableAnimation(EntityRef entity) = 0;
	virtual Path getAnimation(EntityRef entity) = 0;
	virtual void setAnimation(EntityRef entity, const Path& path) = 0;
	virtual float getAnimableTime(EntityRef entity) = 0;
	virtual void setAnimableTime(EntityRef entity, float time) = 0;
	virtual void updateAnimable(EntityRef entity, float time_delta) = 0;
	virtual void updateController(EntityRef entity, float time_delta) = 0;
	virtual float getAnimableTimeScale(EntityRef entity) = 0;
	virtual void setAnimableTimeScale(EntityRef entity, float time_scale) = 0;
	virtual float getAnimableStartTime(EntityRef entity) = 0;
	virtual void setAnimableStartTime(EntityRef entity, float time) = 0;
	virtual u8* getControllerInput(EntityRef entity) = 0;
	virtual void setControllerInput(EntityRef entity, int input_idx, int value) = 0;
	virtual void setControllerInput(EntityRef entity, int input_idx, float value) = 0;
	virtual void setControllerInput(EntityRef entity, int input_idx, bool value) = 0;
	virtual struct LocalRigidTransform getControllerRootMotion(EntityRef entity) = 0;
	virtual void setControllerSource(EntityRef entity, const Path& path) = 0;
	virtual class Path getControllerSource(EntityRef entity) = 0;
	virtual Anim::ComponentInstance* getControllerRoot(EntityRef entity) = 0;
	virtual int getControllerInputIndex(EntityRef entity, const char* name) const = 0;
	virtual EntityPtr getSharedControllerParent(EntityRef entity) = 0;
	virtual void setSharedControllerParent(EntityRef entity, EntityRef parent) = 0;
	virtual void applyControllerSet(EntityRef entity, const char* set_name) = 0;
	virtual void setControllerDefaultSet(EntityRef entity, int set) = 0;
	virtual int getControllerDefaultSet(EntityRef entity) = 0;
	virtual Anim::ControllerResource* getControllerResource(EntityRef entity) = 0;
	virtual float getAnimationLength(int animation_idx) = 0;
};


} // ~ namespace Lumix
