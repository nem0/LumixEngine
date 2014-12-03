#pragma once


#include "core/lumix.h"
#include "engine/iplugin.h"
#include "core/string.h"


namespace Lumix
{
	
	class AnimationScene : public IScene
	{
		public:
			virtual Component getAnimable(const Entity& entity) = 0;
			virtual void playAnimation(const Component& cmp, const char* path) = 0;
	};

	extern "C"
	{
		LUMIX_ANIMATION_API IPlugin* createPlugin(Engine& engine);
	}

}// ~ namespace Lumix 
