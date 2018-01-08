#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "imgui/imgui.h"


using namespace Lumix;


namespace
{


class GUIEditor LUMIX_FINAL : public StudioApp::IPlugin
{
public:
	GUIEditor(StudioApp& app)
	{
		IAllocator& allocator = app.getWorldEditor().getAllocator();

		Action* action = LUMIX_NEW(allocator, Action)("GUI Editor", "Toggle gui editor", "gui_editor");
		action->func.bind<GUIEditor, &GUIEditor::onAction>(this);
		action->is_selected.bind<GUIEditor, &GUIEditor::isOpen>(this);
		app.addWindowAction(action);
	}


private:
	void onAction() { m_is_window_open = !m_is_window_open; }
	bool isOpen() const { return m_is_window_open; }


	void onWindowGUI() override
	{
		if (!ImGui::BeginDock("GUIEditor", &m_is_window_open))
		{
			ImGui::EndDock();
			return;
		}

		ImGui::EndDock();
	}


	bool hasFocus() override { return false; }
	void update(float) override {}
	const char* getName() const override { return "gui_editor"; }

	bool m_is_window_open = false;
};



} // anonymous namespace


LUMIX_STUDIO_ENTRY(gui)
{
	app.registerComponent("gui_image", "GUI/Image");
	app.registerComponent("gui_rect", "GUI/Rect");
	app.registerComponent("gui_text", "GUI/Text");

	IAllocator& allocator = app.getWorldEditor().getAllocator();
	app.addPlugin(*LUMIX_NEW(allocator, GUIEditor)(app));
}
