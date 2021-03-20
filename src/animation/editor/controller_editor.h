#pragma once


#include "editor/studio_app.h"
#include "engine/string.h"


namespace Lumix {

template <typename T> struct UniquePtr;

namespace anim {

struct EventType {
	u32 type;
	StaticString<64> label;
	u16 size;

	virtual ~EventType() {}
	virtual bool onGUI(u8* data, const struct ControllerEditor& editor) const = 0;
};

struct ControllerEditor : StudioApp::GUIPlugin {
	static UniquePtr<ControllerEditor> create(StudioApp& app);
	
	virtual void show(const char* path) = 0;
	virtual void registerEventType(UniquePtr<EventType>&& type) = 0;
	virtual ~ControllerEditor() {}
};


} // namespace anim
} // namespace Lumix