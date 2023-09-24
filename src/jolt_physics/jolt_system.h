#pragma once

#include "engine/plugin.h"

namespace Lumix {

struct JoltSystem : ISystem {
	virtual IAllocator& getAllocator() = 0;
};

} // namespace Lumix
