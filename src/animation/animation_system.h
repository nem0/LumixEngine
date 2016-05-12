#pragma once


#include "engine/lumix.h"
#include "engine/iplugin.h"


namespace Lumix
{


class AnimationScene : public IScene
{
	public:
		virtual class Animation* getAnimableAnimation(ComponentIndex cmp) = 0;
		virtual float getAnimableTime(ComponentIndex cmp) = 0;
		virtual void setAnimableTime(ComponentIndex cmp, float time) = 0;
		virtual void updateAnimable(ComponentIndex cmp, float time_delta) = 0;
};


} // ~ namespace Lumix
