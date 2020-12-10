#pragma once


#include "editor/studio_app.h"


namespace Lumix {

template <typename T> struct UniquePtr;

namespace anim {


// TODO this does not need to be hidden by interface, since it's only included once
struct ControllerEditor : StudioApp::GUIPlugin {
	static UniquePtr<ControllerEditor> create(StudioApp& app);
	
	virtual void show(const char* path) = 0;

	virtual ~ControllerEditor() {}
};


} // namespace anim
} // namespace Lumix