#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"


namespace Lumix
{

class OutputBlob;
class Path;

namespace Anim
{
struct ComponentInstance;
class ControllerResource;
}


struct AnimationScene : public IScene
{
	virtual const OutputBlob& getEventStream() const = 0;
	virtual class Animation* getAnimableAnimation(ComponentHandle cmp) = 0;
	virtual Path getAnimation(ComponentHandle cmp) = 0;
	virtual void setAnimation(ComponentHandle cmp, const Path& path) = 0;
	virtual float getAnimableTime(ComponentHandle cmp) = 0;
	virtual void setAnimableTime(ComponentHandle cmp, float time) = 0;
	virtual void updateAnimable(ComponentHandle cmp, float time_delta) = 0;
	virtual void updateController(ComponentHandle cmp, float time_delta) = 0;
	virtual Entity getControllerEntity(ComponentHandle cmp) = 0;
	virtual float getAnimableTimeScale(ComponentHandle cmp) = 0;
	virtual void setAnimableTimeScale(ComponentHandle cmp, float time_scale) = 0;
	virtual float getAnimableStartTime(ComponentHandle cmp) = 0;
	virtual void setAnimableStartTime(ComponentHandle cmp, float time) = 0;
	virtual u8* getControllerInput(ComponentHandle cmp) = 0;
	virtual void setControllerInput(ComponentHandle cmp, int input_idx, float value) = 0;
	virtual void setControllerInput(ComponentHandle cmp, int input_idx, bool value) = 0;
	virtual struct RigidTransform getControllerRootMotion(ComponentHandle cmp) = 0;
	virtual void setControllerSource(ComponentHandle cmp, const Path& path) = 0;
	virtual class Path getControllerSource(ComponentHandle cmp) = 0;
	virtual Anim::ComponentInstance* getControllerRoot(ComponentHandle cmp) = 0;
	virtual int getControllerInputIndex(ComponentHandle cmp, const char* name) const = 0;
	virtual Entity getSharedControllerParent(ComponentHandle cmp) = 0;
	virtual void setSharedControllerParent(ComponentHandle cmp, Entity parent) = 0;
	virtual void applyControllerSet(ComponentHandle cmp, const char* set_name) = 0;
	virtual void setControllerDefaultSet(ComponentHandle cmp, int set) = 0;
	virtual int getControllerDefaultSet(ComponentHandle cmp) = 0;
	virtual Anim::ControllerResource* getControllerResource(ComponentHandle cmp) = 0;
};


} // ~ namespace Lumix
