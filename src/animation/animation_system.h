#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"


namespace Lumix
{

namespace Anim
{
struct ComponentInstance;
}

struct AnimationSystem : public IPlugin
{
	virtual u8 getEventTypeRuntime(u32 persistent) const = 0;
	virtual u32 getEventTypePersistent(u8 runtime) const = 0;
	virtual u8 createEventType(const char* event_name) = 0;
};


struct AnimationScene : public IScene
{
	virtual class Animation* getAnimableAnimation(ComponentHandle cmp) = 0;
	virtual float getAnimableTime(ComponentHandle cmp) = 0;
	virtual void setAnimableTime(ComponentHandle cmp, float time) = 0;
	virtual void updateAnimable(ComponentHandle cmp, float time_delta) = 0;
	virtual u8* getControllerInput(ComponentHandle cmp) = 0;
	virtual void setControllerInput(ComponentHandle cmp, int input_idx, float value) = 0;
	virtual void setControllerInput(ComponentHandle cmp, int input_idx, bool value) = 0;
	virtual struct Transform getControllerRootMotion(ComponentHandle cmp) = 0;
	virtual class Path getControllerSource(ComponentHandle cmp) = 0;
	virtual Anim::ComponentInstance* getControllerRoot(ComponentHandle cmp) = 0;
	virtual int getControllerInputIndex(ComponentHandle cmp, const char* name) const = 0;
};


} // ~ namespace Lumix
