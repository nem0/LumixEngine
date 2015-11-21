#pragma once


#include "engine/iplugin.h"


namespace Lumix
{


class AudioSystem : public IPlugin
{
	public:
		virtual class ClipManager& getClipManager() = 0;
};


} // namespace Lumix