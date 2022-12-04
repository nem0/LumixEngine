#include "voxelizer_ui.h"
#include "editor/asset_browser.h"
#include "editor/settings.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "renderer/model.h"
#include <imgui/imgui.h>


// this file is disabled in genie.lua, because this is only a helper class to develop voxelization
namespace Lumix {


VoxelizerUI::~VoxelizerUI() {
	m_app.removeAction(&m_toggle_ui);
}

VoxelizerUI::VoxelizerUI(StudioApp& app)
	: m_app(app)
	, m_debug_triangles(app.getAllocator())
	, m_scene(app.getAllocator())
{
	m_toggle_ui.init("Voxelizer editor", "Toggle voxelizer editor", "voxelizer_editor", "", true);
	m_toggle_ui.func.bind<&VoxelizerUI::toggleOpen>(this);
	m_toggle_ui.is_selected.bind<&VoxelizerUI::isOpen>(this);
	m_app.addWindowAction(&m_toggle_ui);
}

void VoxelizerUI::visualizeAO() {
	if (m_scene.m_ao.empty()) {
		m_scene.computeAO(m_ray_count);
	}
	m_debug_triangles.clear();
	const u32 idcs[] = { 
		0, 1, 2,   1, 2, 3, // +x
		4, 5, 6,   5, 6, 7, // -x

		0, 1, 4,   1, 4, 5, // +y
		2, 3, 6,   3, 6, 7, // -y

		0, 2, 4,   2, 4, 6, // +z
		1, 3, 5,   3, 5, 7, // -z
	};
	float voxel_size = m_scene.m_voxel_size;
	IVec3 grid_resolution = m_scene.m_grid_resolution;
	const Array<float>& ao = m_scene.m_ao;
	const OutputMemoryStream& voxels = m_scene.m_voxels;
	Vec3 x(voxel_size * 0.5f, 0, 0);
	Vec3 y(0, voxel_size * 0.5f, 0);
	Vec3 z(0, 0, voxel_size * 0.5f);
	Vec3 origin_shift = x * (float)grid_resolution.x + y * (float)grid_resolution.y + z * (float)grid_resolution.z;
	for (u32 v = 0, c = (u32)voxels.size(); v < c; ++v) {
		if (!voxels[v]) continue;
	
		const i32 k = v / (grid_resolution.x * grid_resolution.y);
		const i32 j = (v / grid_resolution.x) % grid_resolution.y;
		const i32 i = v % grid_resolution.x;
		const Vec3 from = Vec3((float)i * x * 2 + (float)j * y * 2 + (float)k * z * 2) - origin_shift;
			
		const Vec3 points[] = {
			from + x + y + z,
			from + x + y - z,
			from + x - y + z,
			from + x - y - z,

			from - x + y + z,
			from - x + y - z,
			from - x - y + z,
			from - x - y - z,
		};

		for (u32 idx : idcs) {
			const u8 g = u8(clamp(ao[v] * m_ao_multiplier * 255.f, 0.f, 255.f));
			u32 abgr = 0xff000000 | g << 16 | g << 8 | g;
			m_debug_triangles.push({points[idx], abgr});
		}
	}
}

void VoxelizerUI::visualize() {
	m_debug_triangles.clear();
	const u32 idcs[] = { 
		0, 1, 2,   1, 2, 3, // +x
		4, 5, 6,   5, 6, 7, // -x

		0, 1, 4,   1, 4, 5, // +y
		2, 3, 6,   3, 6, 7, // -y

		0, 2, 4,   2, 4, 6, // +z
		1, 3, 5,   3, 5, 7, // -z
	};
	float voxel_size = m_scene.m_voxel_size;
	IVec3 grid_resolution = m_scene.m_grid_resolution;
	OutputMemoryStream& voxels = m_scene.m_voxels;
	Vec3 x(voxel_size * 0.5f, 0, 0);
	Vec3 y(0, voxel_size * 0.5f, 0);
	Vec3 z(0, 0, voxel_size * 0.5f);
	Vec3 origin_shift = x * (float)grid_resolution.x + y * (float)grid_resolution.y + z * (float)grid_resolution.z;
	for (u32 v = 0, c = (u32)voxels.size(); v < c; ++v) {
		if (!voxels[v]) continue;
		
		const i32 k = v / (grid_resolution.x * grid_resolution.y);
		const i32 j = (v / grid_resolution.x) % grid_resolution.y;
		const i32 i = v % grid_resolution.x;
		const Vec3 from = Vec3((float)i * x * 2 + (float)j * y * 2 + (float)k * z * 2) - origin_shift;
				
		const Vec3 points[] = {
			from + x + y + z,
			from + x + y - z,
			from + x - y + z,
			from + x - y - z,
	
			from - x + y + z,
			from - x + y - z,
			from - x - y + z,
			from - x - y - z,
		};

		for (u32 idx : idcs) {
			m_debug_triangles.push({points[idx], 0xffFFffFF});
		}
	}
}

void VoxelizerUI::open(const char* path) {
	if (m_model) m_model->decRefCount();
	m_model = m_app.getEngine().getResourceManager().load<Model>(Path(path));
}

void VoxelizerUI::draw() {
	PROFILE_FUNCTION();
	WorldEditor& editor = m_app.getWorldEditor();
	UniverseView& view = editor.getView();
	const Array<EntityRef>& selected = editor.getSelectedEntities();
	if (selected.size() != 1) return;
	if (m_debug_triangles.empty()) return;

	const DVec3 p = editor.getUniverse()->getPosition(selected[0]) - Vec3(0.5f * m_scene.m_voxel_size);
	const DVec3 cam_pos = view.getViewport().pos;

 	UniverseView::Vertex* vertices = view.render(false, m_debug_triangles.size());
	for (u32 i = 0, c = m_debug_triangles.size(); i < c; ++i) {
		vertices[i].pos = Vec3(p - cam_pos) + m_debug_triangles[i].pos;
		vertices[i].abgr = m_debug_triangles[i].color;
	}
}

void VoxelizerUI::onWindowGUI() {
	if (!m_is_open) return;
	ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Voxelizer", &m_is_open, ImGuiWindowFlags_MenuBar)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
			if (ImGuiEx::BeginResizableMenu("Open", nullptr, true)) {
				char buf[LUMIX_MAX_PATH];
				static FilePathHash selected_res_hash;
				if (m_app.getAssetBrowser().resourceList(Span(buf), selected_res_hash, Model::TYPE, 0, false)) {
					open(buf);
					m_scene.m_voxels.clear();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	if (ImGui::InputInt("Ray count", (i32*)&m_ray_count, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue)) {
		m_scene.computeAO(m_ray_count);
		visualizeAO();
	}
	if (ImGui::InputInt("Resolution", (i32*)&m_max_resolution, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue)) {
		m_scene.m_voxels.clear();
	}
	if (ImGui::Checkbox("Draw", &m_debug_draw)) {
		visualize();
	}
	
	if (ImGui::Button("Blur AO")) m_scene.blurAO();
	ImGui::DragFloat("AO multiplier", &m_ao_multiplier);
	if (ImGui::Button("AO")) {
		visualizeAO();
	}

	if (m_model && m_model->isReady() && m_scene.m_voxels.empty()) {
		m_scene.voxelize(*m_model, m_max_resolution);
		visualize();
	}

	if (m_debug_draw) draw();

	ImGui::End();
}

void VoxelizerUI::onSettingsLoaded() {
	m_is_open = m_app.getSettings().getValue(Settings::GLOBAL, "is_voxelizer_ui_open", false);
}

void VoxelizerUI::onBeforeSettingsSaved() {
	m_app.getSettings().setValue(Settings::GLOBAL, "is_voxelizer_ui_open", m_is_open);
}


} // namespace Lumix