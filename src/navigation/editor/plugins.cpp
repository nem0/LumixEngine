#include "editor/property_grid.h"
#include "editor/platform_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/universe/universe.h"
#include "navigation/navigation_system.h"


using namespace Lumix;


namespace
{


static const uint32 NAVMESH_AGENT_HASH = crc32("navmesh_agent");


struct StudioAppPlugin : public StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& _app)
		: app(_app)
		, is_opened(false)
	{
		auto& allocator = app.getWorldEditor()->getAllocator();
		m_action = LUMIX_NEW(allocator, Action)("Navigation", "toggleNavigationWindow");
		m_action->func.bind<StudioAppPlugin, &StudioAppPlugin::onAction>(this);
		m_action->is_selected.bind<StudioAppPlugin, &StudioAppPlugin::isOpened>(this);
	}


	void onAction()
	{
		is_opened = !is_opened;
	}


	bool isOpened() const
	{
		return is_opened;
	}


	void onWindowGUI() override
	{
		auto* scene = static_cast<NavigationScene*>(app.getWorldEditor()->getUniverse()->getScene(crc32("navigation")));
		if (!scene) return;

		if (ImGui::BeginDock("Navigation", &is_opened, ImGuiWindowFlags_NoScrollWithMouse))
		{
			if (ImGui::Button("Generate")) scene->generateNavmesh();
			ImGui::SameLine();
			if (ImGui::Button("Load"))
			{
				char path[MAX_PATH_LENGTH];
				if (PlatformInterface::getOpenFilename(path, lengthOf(path), "Navmesh\0*.nav\0", nullptr))
				{
					scene->load(path);
				}
			}
			if (scene->isNavmeshReady())
			{
				ImGui::SameLine();
				if (ImGui::Button("Save"))
				{
					char path[MAX_PATH_LENGTH];
					if (PlatformInterface::getSaveFilename(path, lengthOf(path), "Navmesh\0*.nav\0", nullptr))
					{
						scene->save(path);
					}
				}
				static int tile[2] = {};
				if (ImGui::Button("Debug tile"))
				{
					Vec3 camera_hit = app.getWorldEditor()->getCameraRaycastHit();
					scene->generateTileAt(camera_hit, true);
				}

				static bool debug_draw_navmesh = false;
				ImGui::Checkbox("Draw navmesh", &debug_draw_navmesh);
				if (debug_draw_navmesh) scene->debugDrawNavmesh();

				static bool debug_draw_compact_heightfield = false;
				ImGui::Checkbox("Draw compact heightfield", &debug_draw_compact_heightfield);
				if (debug_draw_compact_heightfield) scene->debugDrawCompactHeightfield();

				static bool debug_draw_heightfield = false;
				ImGui::Checkbox("Draw heightfield", &debug_draw_heightfield);
				if (debug_draw_heightfield) scene->debugDrawHeightfield();

				static bool debug_draw_contours = false;
				ImGui::Checkbox("Draw contours", &debug_draw_contours);
				if (debug_draw_contours) scene->debugDrawContours();

				auto& entities = app.getWorldEditor()->getSelectedEntities();
				if (!entities.empty())
				{
					static bool debug_draw_path = false;
					ImGui::Checkbox("Draw path", &debug_draw_path);
					if (debug_draw_path) scene->debugDrawPath(entities[0]);
				}
			}
		}
		ImGui::EndDock();
	}


	bool is_opened;
	StudioApp& app;
};



} // anonymous


LUMIX_STUDIO_ENTRY(navigation)
{
	auto& allocator = app.getWorldEditor()->getAllocator();
	auto* studio_app_plugin = LUMIX_NEW(allocator, StudioAppPlugin)(app);
	app.addPlugin(*studio_app_plugin);

	app.registerComponent("navmesh_agent", "Navmesh Agent");
}

