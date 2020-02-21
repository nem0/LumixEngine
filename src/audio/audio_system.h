#pragma once


#include "engine/plugin.h"


namespace Lumix
{


struct AudioSystem : IPlugin
{
	virtual struct AudioDevice& getDevice() = 0;
	virtual Engine& getEngine() = 0;
};


} // namespace Lumix