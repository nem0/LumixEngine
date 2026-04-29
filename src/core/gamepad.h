#pragma once

#include "core/core.h"

namespace Lumix {

template <typename T> struct Span;

namespace gamepad {

enum class UID : u32 {};

struct Event {
	enum class Axis {
		LTHUMB,
		RTHUMB,
		LTRIGGER,
		RTRIGGER	
	};

	enum Type {
		CONNECTED,
		DISCONNECTED,
		AXIS,
		BUTTON
	};

	UID uid;
	Type type;
	u32 button;
	Axis axis;
	bool down;
	float x, y;
};

LUMIX_CORE_API bool init();
LUMIX_CORE_API void shutdown();
LUMIX_CORE_API Span<const Event> update();

} // namespace gamepad
} // namespace Lumix