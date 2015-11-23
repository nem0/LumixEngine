#pragma once


#include "engine/iplugin.h"


namespace Lumix
{


class AudioDevice;


class AudioSystem : public IPlugin
{
	public:
		virtual class ClipManager& getClipManager() = 0;
		virtual AudioDevice& getDevice() = 0;
};


} // namespace Lumix