#pragma once


#include "engine/plugin.h"


namespace Lumix
{


struct AudioSystem : ISystem
{
	virtual struct AudioDevice& getDevice() = 0;
	virtual Engine& getEngine() = 0;
};


} // namespace Lumix