#pragma once


#include "engine/iplugin.h"


namespace Lumix
{


class AudioScene : public IScene
{
	public:
		static AudioScene* createInstance(IPlugin& system,
			Engine& engine,
			Universe& universe,
			class IAllocator& allocator);
		static void destroyInstance(AudioScene* scene);
};


} // namespace Lumix