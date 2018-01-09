#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/path.h"
#include "engine/plugin_manager.h"
#include "engine/universe/universe.h"
#include "gui/gui_scene.h"
#include "imgui/imgui.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"


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

		m_editor = &app.getWorldEditor();
		Renderer& renderer = *static_cast<Renderer*>(m_editor->getEngine().getPluginManager().getPlugin("renderer"));
		m_pipeline = Pipeline::create(renderer, Path("pipelines/draw2d.lua"), "", allocator);
		m_pipeline->load();

		m_editor->universeCreated().bind<GUIEditor, &GUIEditor::onUniverseChanged>(this);
		m_editor->universeDestroyed().bind<GUIEditor, &GUIEditor::onUniverseChanged>(this);
	}


	~GUIEditor()
	{
		Pipeline::destroy(m_pipeline);
	}


private:
	void onAction() { m_is_window_open = !m_is_window_open; }
	bool isOpen() const { return m_is_window_open; }


	void onUniverseChanged()
	{
		Universe* universe = m_editor->getUniverse();
		if (!universe)
		{
			m_pipeline->setScene(nullptr);
			return;
		}
		RenderScene* scene = (RenderScene*)universe->getScene(crc32("renderer"));
		m_pipeline->setScene(scene);
	}


	void onWindowGUI() override
	{
		if (ImGui::BeginDock("GUIEditor", &m_is_window_open))
		{
			if (!m_pipeline->isReady()) return;
			ImVec2 size = ImGui::GetContentRegionAvail();
			GUIScene* scene = (GUIScene*)m_editor->getUniverse()->getScene(crc32("gui"));			
			scene->render(*m_pipeline, { size.x, size.y });
			m_pipeline->resize(int(size.x), int(size.y));
			m_pipeline->render();
			m_texture_handle = m_pipeline->getRenderbuffer("default", 0);
			ImGui::Image(&m_texture_handle, size);
		}

		ImGui::EndDock();
	}


	bool hasFocus() override { return false; }
	void update(float) override {}
	const char* getName() const override { return "gui_editor"; }

	Pipeline* m_pipeline = nullptr;
	WorldEditor* m_editor = nullptr;
	bool m_is_window_open = false;
	bgfx::TextureHandle m_texture_handle;
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
