#pragma once


#include "editor/studio_app.h"


namespace Lumix::Anim {


struct ControllerEditor : StudioApp::GUIPlugin {
	ControllerEditor(StudioApp& app);

	void onWindowGUI() override;
	const char* getName() const override { return "Animation Editor"; }

	StudioApp& m_app;
	class Controller* m_controller;
	struct GroupNode* m_current_level;
};


} // ns Lumix::Anim