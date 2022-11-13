#define LUMIX_NO_CUSTOM_CRT

#include "../draw_stream.h"
#include "../model.h"
#include "../renderer.h"
#include "../render_scene.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/core.h"
#include "engine/engine.h"
#include "engine/universe.h"
#include "imgui/imgui.h"
#include "procedural_geometry_painter.h"

namespace Lumix {

ProceduralGeometryPainter::ProceduralGeometryPainter(StudioApp& app) 
	: m_app(app)
{
	m_toggle_ui.init("Procedural painter", "Toggle procedural painter UI", "procedural_geom_painter", "", false);
	m_toggle_ui.func.bind<&ProceduralGeometryPainter::toggleUI>(this);
	m_toggle_ui.is_selected.bind<&ProceduralGeometryPainter::isOpen>(this);
	app.addWindowAction(&m_toggle_ui);
}

ProceduralGeometryPainter::~ProceduralGeometryPainter() {
	m_app.removeAction(&m_toggle_ui);
}

void ProceduralGeometryPainter::paint(const DVec3& pos
	, const Universe& universe
	, EntityRef entity
	, ProceduralGeometry& pg
	, Renderer& renderer) const
{
	if (pg.vertex_data.size() == 0) return;
	
	// TODO undo/redo

	const Transform tr = universe.getTransform(entity);
	const Vec3 center(tr.inverted().transform(pos));

	const float R2 = m_brush_size * m_brush_size;

	const u8* end = pg.vertex_data.data() + pg.vertex_data.size();
	const u32 stride = pg.vertex_decl.getStride();
	ASSERT(stride != 0);
	const u32 offset = pg.vertex_decl.attributes[4].byte_offset + m_brush_channel;
	ImGuiIO& io = ImGui::GetIO();
	for (u8* iter = pg.vertex_data.getMutableData(); iter < end; iter += stride) {
		Vec3 p;
		memcpy(&p, iter, sizeof(p));

		if (squaredLength(p - center) < R2) {
			*(iter + offset) = io.KeyAlt ? 255 - m_brush_value : m_brush_value;
		}
	}

	if (pg.vertex_buffer) renderer.getDrawStream().destroy(pg.vertex_buffer);
	const Renderer::MemRef mem = renderer.copy(pg.vertex_data.data(), (u32)pg.vertex_data.size());
	pg.vertex_buffer = renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE);	
}

bool ProceduralGeometryPainter::paint(UniverseView& view, i32 x, i32 y) {
	WorldEditor& editor = view.getEditor();
	const Array<EntityRef>& selected = editor.getSelectedEntities();
	if (selected.size() != 1) return false;

	const EntityRef entity = selected[0];
	const Universe& universe = *editor.getUniverse();
	RenderScene* scene = (RenderScene*)universe.getScene("renderer");
	if (!scene->hasProceduralGeometry(entity)) return false;

	DVec3 origin;
	Vec3 dir;
	view.getViewport().getRay({(float)x, (float)y}, origin, dir);
	const RayCastModelHit hit = scene->castRayProceduralGeometry(origin, dir, [entity](const RayCastModelHit& hit) {
		return hit.entity == entity;
	});
	if (!hit.is_hit) return false;
	if (hit.entity != entity) return false;

	Renderer* renderer = (Renderer*)editor.getEngine().getPluginManager().getPlugin("renderer");
	ASSERT(renderer);

	ProceduralGeometry& pg = scene->getProceduralGeometry(entity);
	paint(hit.origin + hit.t * hit.dir, universe, entity, pg, *renderer);

	return true;
}

void ProceduralGeometryPainter::onMouseWheel(float value) {
	m_brush_size = maximum(0.f, m_brush_size + value * 0.2f);
}

bool ProceduralGeometryPainter::onMouseDown(UniverseView& view, int x, int y) {
	return paint(view, x, y);
}

void ProceduralGeometryPainter::onMouseUp(UniverseView& view, int x, int y, os::MouseButton button) {
}

void ProceduralGeometryPainter::onMouseMove(UniverseView& view, int x, int y, int rel_x, int rel_y) {
	paint(view, x, y);
}

void ProceduralGeometryPainter::drawCursor(WorldEditor& editor, EntityRef entity) const {
	const UniverseView& view = editor.getView();
	const Vec2 mp = view.getMousePos();
	Universe& universe = *editor.getUniverse();
	
	RenderScene* scene = static_cast<RenderScene*>(universe.getScene("renderer"));
	DVec3 origin;
	Vec3 dir;
	editor.getView().getViewport().getRay(mp, origin, dir);
	const RayCastModelHit hit = scene->castRayProceduralGeometry(origin, dir, [entity](const RayCastModelHit& hit){
		return hit.entity == entity;
	});

	if (hit.is_hit) {
		const DVec3 center = hit.origin + hit.dir * hit.t;
		drawCursor(editor, *scene, entity, center);
		return;
	}
}

void ProceduralGeometryPainter::drawCursor(WorldEditor& editor, RenderScene& scene, EntityRef entity, const DVec3& center) const {
	UniverseView& view = editor.getView();
	addCircle(view, center, m_brush_size, Vec3(0, 1, 0), Color::GREEN);
	const ProceduralGeometry& pg = scene.getProceduralGeometry(entity);

	if (pg.vertex_data.size() == 0) return;

	const u8* data = pg.vertex_data.data();
	const u32 stride = pg.vertex_decl.getStride();

	const float R2 = m_brush_size * m_brush_size;

	const Transform tr = scene.getUniverse().getTransform(entity);
	const Vec3 center_local = Vec3(tr.inverted().transform(center));

	for (u32 i = 0, c = pg.getVertexCount(); i < c; ++i) {
		Vec3 p;
		memcpy(&p, data + stride * i, sizeof(p));
		if (squaredLength(center_local - p) < R2) {
			addCircle(view, tr.transform(p), 0.1f, Vec3(0, 1, 0), Color::BLUE);
		}
	}
}

void ProceduralGeometryPainter::onSettingsLoaded() {
	m_is_open = m_app.getSettings().getValue(Settings::GLOBAL, "is_proc_geom_painter_open", false);
}

void ProceduralGeometryPainter::onBeforeSettingsSaved() {
	m_app.getSettings().setValue(Settings::GLOBAL, "is_proc_geom_painter_open", m_is_open);
}

void ProceduralGeometryPainter::onWindowGUI() {
	if (!m_is_open) return;
	ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Procedural geometry painter", &m_is_open)) {
		ImGui::End();
		return;
	}

	WorldEditor& editor = m_app.getWorldEditor();
	Universe& universe = *editor.getUniverse();
	RenderScene* render_scene = (RenderScene*)universe.getScene("renderer");
	const Array<EntityRef>& entities = editor.getSelectedEntities();
	if (entities.size() == 1 && render_scene->hasProceduralGeometry(entities[0])) {
		const EntityRef e = entities[0];
		const ProceduralGeometry& pg = render_scene->getProceduralGeometry(e);
			
		drawCursor(editor, e);

		ImGuiEx::Label("Triangles");
		ImGui::Text("%d", u32(pg.index_data.size() / (pg.index_type == gpu::DataType::U16 ? 2 : 4) / 3));

		ImGui::Separator();

		ImGuiEx::Label("Brush size");
		ImGui::DragFloat("##bs", &m_brush_size, 0.1f, 0, FLT_MAX);

		if (pg.vertex_decl.attributes_count > 4) {
			if (pg.vertex_decl.attributes[4].components_count > 1) {
				ImGuiEx::Label("Paint channel");
				ImGui::SliderInt("##pc", (int*)&m_brush_channel, 0, pg.vertex_decl.attributes[4].components_count - 1);
			}
	
			ImGuiEx::Label("Paint value");
			ImGui::SliderInt("##pv", (int*)&m_brush_value, 0, 255);
		}
	}
	ImGui::End();
}

} // namespace Lumix