#pragma once


#include "lumix.h"
#include "iplugin.h"


namespace Lumix
{

extern "C" {
LUMIX_ANIMATION_API IPlugin* createPlugin(Engine& engine);
}

} // ~ namespace Lumix
