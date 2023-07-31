#pragma once

#include "editor/studio_app.h"

namespace Lumix {

template <typename T> struct UniquePtr;

namespace anim {

struct EventType {
	RuntimeHash type;
	StaticString<64> label;
	u16 size;

	virtual ~EventType() {}
	virtual bool onGUI(u8* data, const struct Controller& controller) const = 0;
};

struct ControllerEditor {
	static UniquePtr<ControllerEditor> create(StudioApp& app);
	
	virtual void registerEventType(UniquePtr<EventType>&& type) = 0;
	virtual ~ControllerEditor() {}
};

} // namespace anim
} // namespace Lumix