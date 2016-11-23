#pragma once


#include "editor/studio_app.h"
#include "engine/delegate.h"
#include "engine/lumix.h"
#include "imgui/imgui.h"


namespace Lumix
{
namespace Anim
{
struct EventHeader;
}
}


namespace AnimEditor
{


class Component;
class Container;
class ControllerResource;


class AnimationEditor : public StudioApp::IPlugin
{
public:
	struct EventType
	{
		Lumix::u32 type;
		Lumix::StaticString<32> label;
		int size;
		Lumix::Delegate<void(Lumix::u8*, Component&)> editor;
	};

public:
	AnimationEditor(StudioApp& app);
	~AnimationEditor();

	const char* getName() const override { return "animation_editor"; }
	void setContainer(Container* container) { m_container = container; }
	bool isEditorOpened() { return m_editor_opened; }
	void toggleEditorOpened() { m_editor_opened = !m_editor_opened; }
	bool isInputsOpened() { return m_inputs_opened; }
	void toggleInputsOpened() { m_inputs_opened = !m_inputs_opened; }
	void onWindowGUI() override;
	StudioApp& getApp() { return m_app; }
	int getEventTypesCount() const;
	EventType& createEventType(const char* type);
	EventType& getEventTypeByIdx(int idx) { return m_event_types[idx]; }
	EventType& getEventType(Lumix::u32 type);

private:
	void newController();
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
	void onSetInputGUI(Lumix::u8* data, Component& component);

private:
	StudioApp& m_app;
	bool m_editor_opened;
	bool m_inputs_opened;
	ImVec2 m_offset;
	ControllerResource* m_resource;
	Container* m_container;
	char m_path[Lumix::MAX_PATH_LENGTH];
	Lumix::Array<EventType> m_event_types;
};


}