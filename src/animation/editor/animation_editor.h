#pragma once


#include "editor/studio_app.h"
#include "imgui/imgui.h"


namespace AnimEditor
{


class Container;
class ControllerResource;


class AnimationEditor : public StudioApp::IPlugin
{
public:
	AnimationEditor(StudioApp& app);
	~AnimationEditor();

	void setContainer(Container* container) { m_container = container; }
	bool isEditorOpened() { return m_editor_opened; }
	void toggleEditorOpened() { m_editor_opened = !m_editor_opened; }
	bool isInputsOpened() { return m_inputs_opened; }
	void toggleInputsOpened() { m_inputs_opened = !m_inputs_opened; }
	void onWindowGUI() override;

private:
	void save();
	void saveAs();
	void drawGraph();
	void load();
	void loadFromEntity();
	void loadFromFile();
	void editorGUI();
	void inputsGUI();
	void constantsGUI();
	void animSetGUI();
	void menuGUI();
	void dropFile(const char* path, const ImVec2& canvas_screen_pos);

private:
	StudioApp& m_app;
	bool m_editor_opened;
	bool m_inputs_opened;
	ImVec2 m_offset;
	ControllerResource* m_resource;
	Container* m_container;
	char m_path[Lumix::MAX_PATH_LENGTH];
};


}