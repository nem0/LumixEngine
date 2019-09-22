#pragma once


#include "editor/studio_app.h"


namespace Lumix {

class Model;

namespace Anim {


struct ControllerEditor : StudioApp::GUIPlugin {
	ControllerEditor(StudioApp& app);
	~ControllerEditor();

	void onWindowGUI() override;
	const char* getName() const override { return "Animation Editor"; }

	StudioApp& m_app;
	class Controller* m_controller;
	struct GroupNode* m_current_level;
	Model* m_model = nullptr;
};


} // ns Anim
} // ns Lumix