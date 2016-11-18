#pragma once


#include "engine/lumix.h"
#include "engine/matrix.h"
#include "engine/iplugin.h"


namespace Lumix
{


class AnimationScene : public IScene
{
	public:
		virtual class Animation* getAnimableAnimation(ComponentHandle cmp) = 0;
		virtual float getAnimableTime(ComponentHandle cmp) = 0;
		virtual void setAnimableTime(ComponentHandle cmp, float time) = 0;
		virtual void updateAnimable(ComponentHandle cmp, float time_delta) = 0;
		virtual uint8* getControllerInput(ComponentHandle cmp) = 0;
		virtual void setControllerInput(ComponentHandle cmp, int input_idx, float value) = 0;
		virtual Transform getControllerRootMotion(ComponentHandle cmp) = 0;
};


} // ~ namespace Lumix
