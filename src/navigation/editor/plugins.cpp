#include "editor/platform_interface.h"
#include "editor/property_grid.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/math_utils.h"
#include "engine/universe/universe.h"
#include "navigation/navigation_system.h"
#include <DetourCrowd.h>


using namespace Lumix;


namespace
{


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
				ImGui::SameLine();
				if (ImGui::Button("Debug tile"))
				{
					Vec3 camera_hit = app.getWorldEditor()->getCameraRaycastHit();
					scene->generateTileAt(camera_hit, true);
				}

				static bool debug_draw_path = false;
				const auto& selected_entities = app.getWorldEditor()->getSelectedEntities();
				if (!selected_entities.empty())
				{
					const dtCrowdAgent* agent = scene->getDetourAgent(selected_entities[0]);
					if (agent)
					{
						ImGui::Text("Agent");
						ImGui::Checkbox("Draw path", &debug_draw_path);
						if (debug_draw_path) scene->debugDrawPath(selected_entities[0]);
						ImGui::LabelText("Desired speed", "%f", agent->desiredSpeed);
						ImGui::Separator();
					}
				}

				static bool debug_draw_navmesh = false;
				ImGui::Checkbox("Draw navmesh", &debug_draw_navmesh);
				if (debug_draw_navmesh)
				{
					static bool inner_boundaries = true;
					static bool outer_boundaries = true;
					static bool portals = true;
					ImGui::Checkbox("Inner boundaries", &inner_boundaries);
					ImGui::Checkbox("Outer boundaries", &outer_boundaries);
					ImGui::Checkbox("Portals", &portals);
					scene->debugDrawNavmesh(app.getWorldEditor()->getCameraRaycastHit(), inner_boundaries, outer_boundaries, portals);
				}

				if (scene->hasDebugDrawData())
				{
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
				else
				{
					ImGui::Text("For more info press \"Debug tile\"");
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

