#pragma once

#include "engine/plugin.h"

namespace Lumix {

//@ object
struct GUINGSystem : ISystem {
	virtual Engine& getEngine() = 0;
	// TODO: Add GUI NG system functions
};

} // namespace Lumix