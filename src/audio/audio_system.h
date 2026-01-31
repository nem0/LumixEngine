#pragma once


#include "engine/plugin.h"


namespace black
{


struct AudioSystem : ISystem
{
	virtual struct AudioDevice& getDevice() = 0;
	virtual Engine& getEngine() = 0;
};


} // namespace black