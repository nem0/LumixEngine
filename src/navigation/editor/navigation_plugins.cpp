#include <DetourCrowd.h>
#include <imgui/imgui.h>

#include "editor/property_grid.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/os.h"
#include "engine/universe.h"
#include "navigation/navigation_scene.h"


using namespace Lumix;


namespace
{


static const ComponentType NAVMESH_AGENT_TYPE = reflection::getComponentType("navmesh_agent");
static const ComponentType NAVMESH_ZONE_TYPE = reflection::getComponentType("navmesh_zone");


struct PropertyGridPlugin final : PropertyGrid::IPlugin {
	PropertyGridPlugin(StudioApp& app) : m_app(app) {}
	~PropertyGridPlugin() {
		ASSERT(!m_job);
	}

	void onAgentGUI(EntityRef entity, WorldEditor& editor) {
		Universe& universe = *editor.getUniverse();
		auto* scene = static_cast<NavigationScene*>(universe.getScene(crc32("navigation")));
		static bool debug_draw_path = false;
		const dtCrowdAgent* agent = scene->getDetourAgent(entity);
		if (agent) {
			ImGui::LabelText("Desired speed", "%f", agent->desiredSpeed);
			ImGui::LabelText("Corners", "%d", agent->ncorners);
			if (agent->ncorners > 0) {
				Vec3 pos = *(Vec3*)agent->npos;
				Vec3 corner = *(Vec3*)agent->targetPos;

				ImGui::LabelText("Target distance", "%f", length(pos - corner));
			}

			static const char* STATES[] = { "Invalid", "Walking", "Offmesh" };
			if (agent->state < lengthOf(STATES)) ImGui::LabelText("State", "%s", STATES[agent->state]);
			static const char* TARGET_STATES[] = { "None", "Failed", "Valid", "Requesting", "Waiting for queue", "Waiting for path", "Velocity" };
			if (agent->targetState < lengthOf(TARGET_STATES)) ImGui::LabelText("Target state", "%s", TARGET_STATES[agent->targetState]);
		}

		ImGui::Checkbox("Draw path", &debug_draw_path);
		if (debug_draw_path) scene->debugDrawPath(entity);
	}

	void update() override {
		if (!m_job) return;
		auto* scene = static_cast<NavigationScene*>(m_app.getWorldEditor().getUniverse()->getScene(NAVMESH_AGENT_TYPE));
		if (m_job->isFinished()) {
			scene->free(m_job);
			m_job = nullptr;
			return;
		}
		
		const float ui_width = maximum(300.f, ImGui::GetIO().DisplaySize.x * 0.33f);
		const ImVec2 pos = ImGui::GetMainViewport()->Pos;
		ImGui::SetNextWindowPos(ImVec2((ImGui::GetIO().DisplaySize.x - ui_width) * 0.5f + pos.x, 30 + pos.y));
		ImGui::SetNextWindowSize(ImVec2(ui_width, -1));
		ImGui::SetNextWindowSizeConstraints(ImVec2(-FLT_MAX, 0), ImVec2(FLT_MAX, 200));
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar 
			| ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
		if (ImGui::Begin("Navmesh building", nullptr, flags)) {
			ImGui::Text("%s", "Building navmesh...");
			ImGui::Text("%s", "Manipulating with entities at this time can produce incorrect results and even crashes.");
			float progress = m_job->getProgress();
			ImGui::ProgressBar(progress, ImVec2(-1, 0), StaticString<64>(i32(progress * 100), "%"));
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}

	void onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) override {
		if(cmp.type == NAVMESH_AGENT_TYPE) { 
			onAgentGUI((EntityRef)cmp.entity, editor);
			return;
		}

		if (cmp.type != NAVMESH_ZONE_TYPE) return;
		
		auto* scene = static_cast<NavigationScene*>(cmp.scene);
		if (m_job) {
			ImGui::TextUnformatted("Generating...");
		}
		else if (ImGui::Button("Generate")) {
			m_job = scene->generateNavmesh((EntityRef)cmp.entity);
		}

		ImGui::SameLine();
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (ImGui::Button("Load")) {
			scene->loadZone((EntityRef)cmp.entity);
		}

		if(scene->isNavmeshReady((EntityRef)cmp.entity)) {
			ImGui::SameLine();
			if (ImGui::Button("Save")) {
				StaticString<LUMIX_MAX_PATH> dir(m_app.getEngine().getFileSystem().getBasePath(), "/universes/navzones/");
				if (!os::makePath(dir) && !os::dirExists(dir)) {
					logError("Could not create ", dir);
				}
				scene->saveZone((EntityRef)cmp.entity);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Debug tile")) {
			UniverseView& view = editor.getView();
			const UniverseView::RayHit hit = view.getCameraRaycastHit(view.getViewport().w >> 1, view.getViewport().h >> 1);
			scene->generateTileAt((EntityRef)cmp.entity, hit.pos, true);
		}

		static bool debug_draw_navmesh = false;
		ImGui::Checkbox("Draw navmesh", &debug_draw_navmesh);
		if (debug_draw_navmesh) {
			static bool inner_boundaries = true;
			static bool outer_boundaries = true;
			static bool portals = true;
			ImGui::Checkbox("Inner boundaries", &inner_boundaries);
			ImGui::Checkbox("Outer boundaries", &outer_boundaries);
			ImGui::Checkbox("Portals", &portals);
			UniverseView& view = editor.getView();
			const UniverseView::RayHit hit = view.getCameraRaycastHit(view.getViewport().w >> 1, view.getViewport().h >> 1);
			scene->debugDrawNavmesh((EntityRef)cmp.entity, hit.pos, inner_boundaries, outer_boundaries, portals);
		}

		if (scene->hasDebugDrawData((EntityRef)cmp.entity)) {
			static bool debug_draw_compact_heightfield = false;
			ImGui::Checkbox("Draw compact heightfield", &debug_draw_compact_heightfield);
			if (debug_draw_compact_heightfield) scene->debugDrawCompactHeightfield((EntityRef)cmp.entity);

			static bool debug_draw_heightfield = false;
			ImGui::Checkbox("Draw heightfield", &debug_draw_heightfield);
			if (debug_draw_heightfield) scene->debugDrawHeightfield((EntityRef)cmp.entity);

			static bool debug_draw_contours = false;
			ImGui::Checkbox("Draw contours", &debug_draw_contours);
			if (debug_draw_contours) scene->debugDrawContours((EntityRef)cmp.entity);
		}
		else {
			ImGui::Text("For more info press \"Debug tile\"");
		}
	}

	StudioApp& m_app;
	NavmeshBuildJob* m_job = nullptr;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_zone_pg_plugin(app)
	{}
	
	void init() override {
		m_app.getPropertyGrid().addPlugin(m_zone_pg_plugin);
	}

	~StudioAppPlugin() {
		m_app.getPropertyGrid().removePlugin(m_zone_pg_plugin);
	}

	const char* getName() const override {
		return "navigation";
	}

	bool showGizmo(UniverseView& view, ComponentUID cmp) override {
		if(cmp.type != NAVMESH_ZONE_TYPE) return false;

		auto* scene = static_cast<NavigationScene*>(cmp.scene);
		Universe& universe = scene->getUniverse();
		
		RenderInterface* ri = m_app.getRenderInterface();
		if (!ri) return false;

		const NavmeshZone& zone = scene->getZone((EntityRef)cmp.entity);
		const Transform tr = universe.getTransform((EntityRef)cmp.entity);

		const Vec3 x = tr.rot.rotate(Vec3(zone.extents.x, 0, 0));
		const Vec3 y = tr.rot.rotate(Vec3(0, zone.extents.y, 0));
		const Vec3 z = tr.rot.rotate(Vec3(0, 0, zone.extents.z));

		addCube(view, tr.pos, z, y, x, Color::BLUE);

		return true; 
	}

	StudioApp& m_app;
	PropertyGridPlugin m_zone_pg_plugin;
};


} // anonymous


LUMIX_STUDIO_ENTRY(navigation)
{
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}

