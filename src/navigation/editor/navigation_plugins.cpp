#include <DetourCrowd.h>
#include <imgui/imgui.h>

#include "editor/property_grid.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/component_uid.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "core/geometry.h"
#include "core/log.h"
#include "core/path.h"
#include "core/os.h"
#include "core/profiler.h"
#include "engine/world.h"
#include "navigation/navigation_module.h"


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
		World& world = *editor.getWorld();
		auto* module = static_cast<NavigationModule*>(world.getModule("navigation"));
		const dtCrowdAgent* agent = module->getDetourAgent(entity);
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

		ImGui::Checkbox("Draw path", &m_debug_draw_path);
		if (m_debug_draw_path) {
			ImGui::Checkbox("Draw path polygons", &m_debug_draw_path_polys);
		}
		if (m_debug_draw_path) module->debugDrawPath(entity, m_debug_draw_path_polys);
	}

	void update() override {
		if (!m_job) return;
		auto* module = static_cast<NavigationModule*>(m_app.getWorldEditor().getWorld()->getModule(NAVMESH_AGENT_TYPE));
		if (m_job->isFinished()) {
			module->free(m_job);
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
			ImGui::TextUnformatted("Building navmesh...");
			ImGui::TextUnformatted("Manipulating with entities at this time can produce incorrect results and even crashes.");
			float progress = m_job->getProgress();
			ImGui::ProgressBar(progress, ImVec2(-1, 0), StaticString<64>(i32(progress * 100), "%"));
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}

	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor) override {
		if (filter.isActive()) return;
		if (entities.length() != 1) return;

		if(cmp_type == NAVMESH_AGENT_TYPE) { 
			onAgentGUI(entities[0], editor);
			return;
		}

		if (cmp_type != NAVMESH_ZONE_TYPE) return;
		
		auto* module = static_cast<NavigationModule*>(editor.getWorld()->getModule(cmp_type));
		if (m_job) {
			ImGui::TextUnformatted("Generating...");
		}
		else if (ImGui::Button("Generate")) {
			m_job = module->generateNavmesh(entities[0]);
		}

		ImGui::SameLine();
		if (ImGui::Button("Load")) {
			module->loadZone(entities[0]);
		}

		if(module->isNavmeshReady(entities[0])) {
			ImGui::SameLine();
			if (ImGui::Button("Save")) {
				const Path dir(m_app.getProjectDir(), "/navzones/");
				if (!os::makePath(dir.c_str()) && !os::dirExists(dir)) {
					logError("Could not create ", dir);
				}
				module->saveZone(entities[0]);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Debug tile")) {
			WorldView& view = editor.getView();
			const RayHit hit = view.getCameraRaycastHit(view.getViewport().w >> 1, view.getViewport().h >> 1, INVALID_ENTITY);
			module->generateTileAt(entities[0], hit.pos, true);
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
			WorldView& view = editor.getView();
			const RayHit hit = view.getCameraRaycastHit(view.getViewport().w >> 1, view.getViewport().h >> 1, INVALID_ENTITY);
			module->debugDrawNavmesh(entities[0], hit.pos, inner_boundaries, outer_boundaries, portals);
		}

		if (module->hasDebugDrawData(entities[0])) {
			static bool debug_draw_compact_heightfield = false;
			ImGui::Checkbox("Draw compact heightfield", &debug_draw_compact_heightfield);
			if (debug_draw_compact_heightfield) module->debugDrawCompactHeightfield(entities[0]);

			static bool debug_draw_heightfield = false;
			ImGui::Checkbox("Draw heightfield", &debug_draw_heightfield);
			if (debug_draw_heightfield) module->debugDrawHeightfield(entities[0]);

			static bool debug_draw_contours = false;
			ImGui::Checkbox("Draw contours", &debug_draw_contours);
			if (debug_draw_contours) module->debugDrawContours(entities[0]);
		}
		else {
			ImGui::Text("For more info press \"Debug tile\"");
		}
	}

	StudioApp& m_app;
	NavmeshBuildJob* m_job = nullptr;
	bool m_debug_draw_path = false;
	bool m_debug_draw_path_polys = false;
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

	bool showGizmo(WorldView& view, ComponentUID cmp) override {
		if(cmp.type != NAVMESH_ZONE_TYPE) return false;

		auto* module = static_cast<NavigationModule*>(cmp.module);
		World& world = module->getWorld();
		
		const NavmeshZone& zone = module->getZone((EntityRef)cmp.entity);
		const Transform tr = world.getTransform((EntityRef)cmp.entity);

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


LUMIX_STUDIO_ENTRY(navigation) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}

