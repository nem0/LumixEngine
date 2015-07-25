#pragma once


#include "core/lumix.h"
#include "engine/iplugin.h"
#include "core/string.h"


namespace Lumix
{

extern "C" {
LUMIX_ANIMATION_API IPlugin* createPlugin(Engine& engine);
}

} // ~ namespace Lumix
