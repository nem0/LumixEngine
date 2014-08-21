#pragma once


#include "core/lumix.h"
#include "engine/iplugin.h"
#include "core/string.h"


namespace Lumix
{
	namespace FS
	{
		class FileSystem;
	};

	class Animation;
	class Engine;
	struct Entity;
	class ISerializer;
	class Universe;

	class LUMIX_ANIMATION_API AnimationScene : public IScene
	{
		public:
			virtual void setFrame(Component cmp, int frame) = 0;
			virtual bool isManual(Component cmp) = 0;
			virtual void setManual(Component cmp, bool is_manual) = 0;
			virtual void getPreview(Component cmp, string& path) = 0;
			virtual void setPreview(Component cmp, const string& path) = 0;

			virtual void playAnimation(const Component& cmp, const char* path) = 0;
			virtual void setAnimationFrame(const Component& cmp, int frame) = 0;
			virtual int getFrameCount(const Component& cmp) const = 0;
			virtual void update(float time_delta) = 0;
	};


	class LUMIX_ANIMATION_API AnimationSystem : public IPlugin
	{
		public:
			static AnimationSystem* createInstance();
	};


	extern "C"
	{
		LUMIX_ANIMATION_API IPlugin* createPlugin();
	}

}// ~ namespace Lumix 
