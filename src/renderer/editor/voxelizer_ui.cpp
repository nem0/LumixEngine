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

namespace Lumix {

VoxelizerUI::~VoxelizerUI() {
	m_app.removeAction(&m_toggle_ui);
}

VoxelizerUI::VoxelizerUI(StudioApp& app)
	: m_app(app)
	, m_voxels(app.getAllocator())
	, m_debug_triangles(app.getAllocator())
{
	m_toggle_ui.init("Voxelizer editor", "Toggle voxelizer editor", "voxelizer_editor", "", true);
	m_toggle_ui.func.bind<&VoxelizerUI::toggleOpen>(this);
	m_toggle_ui.is_selected.bind<&VoxelizerUI::isOpen>(this);
	m_app.addWindowAction(&m_toggle_ui);
}

template <typename F>
void forEachTriangle(const Mesh& mesh, const F& f) {
	const u16* indices16 = (u16*)mesh.indices.data();
	const u32* indices32 = (u32*)mesh.indices.data();
	const bool areindices16 = mesh.areIndices16();
	
	for (u32 i = 0; i < (u32)mesh.indices_count; i += 3) {
		u32 indices[3];
		if (areindices16) {
			indices[0] = indices16[i];
			indices[1] = indices16[i + 1];
			indices[2] = indices16[i + 2];
		}
		else {
			indices[0] = indices32[i];
			indices[1] = indices32[i + 1];
			indices[2] = indices32[i + 2];
		}

		const Vec3 p0 = mesh.vertices[indices[0]];
		const Vec3 p1 = mesh.vertices[indices[1]];
		const Vec3 p2 = mesh.vertices[indices[2]];
		f(p0, p1, p2);
	}
}

void VoxelizerUI::voxelize() {
	PROFILE_FUNCTION();
	ASSERT(m_model && m_model->isReady());
	
	for (u32 mesh_idx = 0; mesh_idx < (u32)m_model->getMeshCount(); ++mesh_idx) {
		const Mesh& mesh = m_model->getMesh(mesh_idx);
		Vec3 min = Vec3(FLT_MAX);
		Vec3 max = Vec3(-FLT_MAX);
		forEachTriangle(mesh, [&](const Vec3& p0, const Vec3& p1, const Vec3& p2){
			min = minimum(min, p0);
			min = minimum(min, p1);
			min = minimum(min, p2);

			max = maximum(max, p0);
			max = maximum(max, p1);
			max = maximum(max, p2);
		});

		const float voxel_size = maximum(max.x - min.x, max.y - min.y, max.z - min.z) / m_max_resolution;
		min -= Vec3(voxel_size);
		max += Vec3(voxel_size);

		const IVec3 resolution = IVec3(i32((max.x - min.x) / voxel_size), i32((max.y - min.y) / voxel_size), i32((max.z - min.z) / voxel_size));
		m_voxels.resize(resolution.x * resolution.y * resolution.z);
		memset(m_voxels.getMutableData(), 0, m_voxels.size());

		auto to_grid = [&](const Vec3& p){
			return IVec3((p - min) / voxel_size + Vec3(0.5f));
		};

		auto from_grid = [&](const IVec3& p){
			return Vec3(p) * voxel_size + Vec3(0.5f * voxel_size) + min;
		};

		auto intersect = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, IVec3 voxel){
			Vec3 center = from_grid(voxel);
			Vec3 half(0.5f * voxel_size);
			return testAABBTriangleCollision(AABB(center - half, center + half), p0, p1, p2);
		};

		forEachTriangle(mesh, [&](const Vec3& p0, const Vec3& p1, const Vec3& p2){
			AABB aabb;
			aabb.min = aabb.max = p0;
			aabb.addPoint(p1);
			aabb.addPoint(p2);

			const IVec3 ming = to_grid(aabb.min);
			const IVec3 maxg = to_grid(aabb.max);

			for (i32 k = ming.z; k <= maxg.z; ++k) {
				for (i32 j = ming.y; j <= maxg.y; ++j) {
					for (i32 i = ming.x; i <= maxg.x; ++i) {
						if (intersect(p0, p1, p2, IVec3(i, j, k))) {
							m_voxels[i + j * (resolution.x) + k * (resolution.x * resolution.y)] = 1;
						}
					}
				}
			}
		});
		m_grid_resolution = resolution;
		m_voxel_size = voxel_size;

		{
			m_debug_triangles.clear();
			Vec3 x(m_voxel_size * 0.5f, 0, 0);
			Vec3 y(0, m_voxel_size * 0.5f, 0);
			Vec3 z(0, 0, m_voxel_size * 0.5f);
			Vec3 origin_shift = x * (float)m_grid_resolution.x + y * (float)m_grid_resolution.y + z * (float)m_grid_resolution.z;
			for (u32 v = 0, c = (u32)m_voxels.size(); v < c; ++v) {
				if (!m_voxels[v]) continue;
		
				const i32 k = v / (m_grid_resolution.x * m_grid_resolution.y);
				const i32 j = (v / m_grid_resolution.x) % m_grid_resolution.y;
				const i32 i = v % m_grid_resolution.x;
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

				const u32 idcs[] = { 
					0, 1, 2,   1, 2, 3, // +x
					4, 5, 6,   5, 6, 7, // -x

					0, 1, 4,   1, 4, 5, // +y
					2, 3, 6,   3, 6, 7, // -y

					0, 2, 4,   2, 4, 6, // +z
					1, 3, 5,   3, 5, 7, // -z
				};

				for (u32 idx : idcs) {
					m_debug_triangles.push(points[idx]);
				}
			}
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

	const DVec3 p = editor.getUniverse()->getPosition(selected[0]) - Vec3(0.5f * m_voxel_size);
	const DVec3 cam_pos = view.getViewport().pos;

 	UniverseView::Vertex* vertices = view.render(false, m_debug_triangles.size());
	for (u32 i = 0, c = m_debug_triangles.size(); i < c; ++i) {
		vertices[i].pos = Vec3(p - cam_pos) + m_debug_triangles[i];
		u32 side_idx = (i / 6) % 6;
		vertices[i].abgr = Color(u8(0xff * (0.5f + (side_idx / 10.f))), 0, 0, 0xff).abgr();
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
					m_voxels.clear();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	ImGui::DragInt("Resolution", (i32*)&m_max_resolution);
	ImGui::Checkbox("Draw", &m_debug_draw);
	
	if (m_model && m_model->isReady() && m_voxels.empty()) {
		voxelize();
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