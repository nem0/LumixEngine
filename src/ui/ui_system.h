#pragma once

#include "engine/plugin.h"

namespace Lumix {

//@ object
struct UISystem : ISystem {
	virtual Engine& getEngine() = 0;
	//@ function
	virtual void enableCursor(bool enable) = 0;
	virtual bool isCursorEnabled() const = 0;
};

} // namespace Lumix