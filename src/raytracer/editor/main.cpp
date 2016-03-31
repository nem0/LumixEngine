#include "lumix.h"
#include "core/crc32.h"
#include "core/resource_manager.h"
#include "editor/asset_browser.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "../renderer.h"
#include "engine/engine.h"
#include "core/iallocator.h"
#include "../pipeline.h"
#include "editor/imgui/imgui.h"
#include "engine/plugin_manager.h"


using namespace Lumix;


struct SceneViewPlugin : public StudioApp::IPlugin
{
	explicit SceneViewPlugin(StudioApp& app)
		: m_app(app)
	{
		auto& editor = *app.getWorldEditor();
		auto& allocator = editor.getAllocator();
		auto& engine = editor.getEngine();
		auto* renderer = static_cast<Lumix::Renderer*>(engine.getPluginManager().getPlugin("renderer"));
		m_pipeline = Lumix::Pipeline::create(*renderer, engine.getAllocator());
	}


	void update(float) override
	{
		
	}


	void onAction() {}


	void onWindowGUI() override
	{
		bool dummy;
		ImGui::Checkbox("Statssss", &dummy);
		/*auto size = ImGui::GetContentRegionAvail();
		if(size.x > 0 && size.y > 0)
		{
			m_pipeline->setViewport(0, 0, int(size.x), int(size.y));
			m_pipeline->render();
		}*/
	}

	StudioApp& m_app;
	Lumix::Pipeline* m_pipeline;
};


static const uint32 CAMERA_HASH = crc32("camera");
static const uint32 RENDERABLE_MESH = crc32("renderable_model");


extern "C" {


LUMIX_STUDIO_ENTRY(raytracer)
{
	auto& allocator = app.getWorldEditor()->getAllocator();

	auto* scene_view_plugin = LUMIX_NEW(allocator, SceneViewPlugin)(app);
	app.addPlugin(*scene_view_plugin);
}


}