#pragma once


#include "editor/studio_app.h"


namespace Lumix {

struct Model;

namespace Anim {


struct ControllerEditor : StudioApp::GUIPlugin {
	static ControllerEditor& create(StudioApp& app);
	static void destroy(ControllerEditor& editor);

	virtual ~ControllerEditor() {}
};


} // ns Anim
} // ns Lumix