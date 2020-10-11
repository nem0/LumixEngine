#pragma once


#include "editor/studio_app.h"


namespace Lumix {

struct Model;
template <typename T> struct UniquePtr;

namespace Anim {


struct ControllerEditor : StudioApp::GUIPlugin {
	static UniquePtr<ControllerEditor> create(StudioApp& app);

	virtual ~ControllerEditor() {}
};


} // ns Anim
} // ns Lumix