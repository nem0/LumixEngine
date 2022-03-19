#define LUMIX_NO_CUSTOM_CRT

#include "../model.h"
#include "../renderer.h"
#include "../render_scene.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/core.h"
#include "engine/engine.h"
#include "engine/universe.h"
#include "imgui/imgui.h"
#include "spline_geometry_plugin.h"

namespace Lumix {

static const ComponentType SPLINE_GEOMETRY_TYPE = reflection::getComponentType("spline_geometry");
static const ComponentType SPLINE_TYPE = reflection::getComponentType("spline");

struct SplineIterator {
	SplineIterator(Span<const Vec3> points) : points(points) {}

	void move(float delta) { t += delta; }
	bool isEnd() { return u32(t) >= points.length() - 2; }
	Vec3 getDir() {
		const u32 segment = u32(t);
		float rel_t = t - segment;
		Vec3 p0 = points[segment + 0];
		Vec3 p1 = points[segment + 1];
		Vec3 p2 = points[segment + 2];
		return lerp(p1 - p0, p2 - p1, rel_t);
	}

	Vec3 getPosition() {
		const u32 segment = u32(t);
		float rel_t = t - segment;
		Vec3 p0 = points[segment + 0];
		Vec3 p1 = points[segment + 1];
		Vec3 p2 = points[segment + 2];
		p0 = (p1 + p0) * 0.5f;
		p2 = (p1 + p2) * 0.5f;

		return lerp(lerp(p0, p1, rel_t), lerp(p1, p2, rel_t), rel_t);
	}

	float t = 0;

	Span<const Vec3> points;
};

SplineGeometryPlugin::SplineGeometryPlugin(StudioApp& app) 
	: m_app(app)
{}

void SplineGeometryPlugin::paint(const DVec3& pos
	, const Universe& universe
	, EntityRef entity
	, const SplineGeometry& sg
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
	const u32 offset = (sg.flags.isSet(SplineGeometry::HAS_UVS) ? 20 : 12) + m_brush_channel;
	ImGuiIO& io = ImGui::GetIO();
	for (u8* iter = pg.vertex_data.getMutableData(); iter < end; iter += stride) {
		Vec3 p;
		memcpy(&p, iter, sizeof(p));

		if (squaredLength(p - center) < R2) {
			*(iter + offset) = io.KeyAlt ? 255 - m_brush_value : m_brush_value;
		}
	}

	if (pg.vertex_buffer) renderer.destroy(pg.vertex_buffer);
	const Renderer::MemRef mem = renderer.copy(pg.vertex_data.data(), (u32)pg.vertex_data.size());
	pg.vertex_buffer = renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE);	
}

bool SplineGeometryPlugin::paint(UniverseView& view, i32 x, i32 y) {
	WorldEditor& editor = view.getEditor();
	const Array<EntityRef>& selected = editor.getSelectedEntities();
	if (selected.size() != 1) return false;

	const EntityRef entity = selected[0];

	const Universe& universe = *editor.getUniverse();
	const bool is_spline = universe.hasComponent(entity, SPLINE_GEOMETRY_TYPE);
	if (!is_spline) return false;

	RenderScene* scene = (RenderScene*)universe.getScene(SPLINE_GEOMETRY_TYPE);
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
	const SplineGeometry& sg = scene->getSplineGeometry(entity);
	paint(hit.origin + hit.t * hit.dir, universe, entity, sg, pg, *renderer);

	return true;
}

void SplineGeometryPlugin::onMouseWheel(float value) {
	m_brush_size = maximum(0.f, m_brush_size + value * 0.2f);
}

bool SplineGeometryPlugin::onMouseDown(UniverseView& view, int x, int y) {
	return paint(view, x, y);
}

void SplineGeometryPlugin::onMouseUp(UniverseView& view, int x, int y, os::MouseButton button) {
}

void SplineGeometryPlugin::onMouseMove(UniverseView& view, int x, int y, int rel_x, int rel_y) {
	paint(view, x, y);
}

void SplineGeometryPlugin::drawCursor(WorldEditor& editor, EntityRef entity) const {
	const UniverseView& view = editor.getView();
	const Vec2 mp = view.getMousePos();
	Universe& universe = *editor.getUniverse();
	
	RenderScene* scene = static_cast<RenderScene*>(universe.getScene(SPLINE_GEOMETRY_TYPE));
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

void SplineGeometryPlugin::drawCursor(WorldEditor& editor, RenderScene& scene, EntityRef entity, const DVec3& center) const {
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

static const char* toString(SplineGeometryPlugin::GeometryMode mode) {
	switch (mode) {
		case SplineGeometryPlugin::GeometryMode::NO_SNAP: return "No snap";
		case SplineGeometryPlugin::GeometryMode::SNAP_CENTER: return "Snap center";
		case SplineGeometryPlugin::GeometryMode::SNAP_ALL: return "Snap everything";
	}
	ASSERT(false);
	return "N/A";
}

void SplineGeometryPlugin::onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) {
	if (cmp.type != SPLINE_GEOMETRY_TYPE) return;

	const EntityRef e = *cmp.entity;
	Universe& universe = cmp.scene->getUniverse();
	if (!universe.hasComponent(*cmp.entity, SPLINE_TYPE)) {
		ImGui::TextUnformatted("There's no spline component");
		if (ImGui::Button("Create spline component")) {
			editor.addComponent(Span(&e, 1), SPLINE_TYPE);
		}
	}
	else {
		RenderScene* render_scene = (RenderScene*)universe.getScene(SPLINE_GEOMETRY_TYPE);
		CoreScene* core_scene = (CoreScene*)universe.getScene(SPLINE_TYPE);
		const Spline& spline = core_scene->getSpline(e);
		const SplineGeometry& sg = render_scene->getSplineGeometry(e);
		const ProceduralGeometry& pg = render_scene->getProceduralGeometry(e);
			
		drawCursor(editor, *cmp.entity);

		ImGuiEx::Label("Triangles");
		ImGui::Text("%d", u32(pg.index_data.size() / (pg.index_type == gpu::DataType::U16 ? 2 : 4) / 3));

		ImGui::Separator();

		ImGuiEx::Label("Brush size");
		ImGui::DragFloat("##bs", &m_brush_size, 0.1f, 0, FLT_MAX);

		if (sg.num_user_channels > 1) {
			ImGuiEx::Label("Paint channel");
			ImGui::SliderInt("##pc", (int*)&m_brush_channel, 0, sg.num_user_channels - 1);
		}

		ImGuiEx::Label("Paint value");
		ImGui::SliderInt("##pv", (int*)&m_brush_value, 0, 255);

		ImGui::Separator();

		ImGuiEx::Label("Mode");
		if (ImGui::BeginCombo("##gm", toString(m_geometry_mode))) {
			for (u32 i = 0; i < (u32)GeometryMode::COUNT; ++i) {
				if (ImGui::Selectable(toString(GeometryMode(i)))) m_geometry_mode = GeometryMode(i);
			}
			ImGui::EndCombo();
		}

		const bool snap = m_geometry_mode != GeometryMode::NO_SNAP;

		if (!snap) ImGuiEx::PushReadOnly();
		ImGuiEx::Label("Snap height");
		ImGui::DragFloat("##sh", &m_snap_height);
		if (!snap) ImGuiEx::PopReadOnly();

		if (ImGui::Button("Generate geometry")) {
			if (!spline.points.empty()) {
				const float width = sg.width;
				SplineIterator iterator(spline.points);
				gpu::VertexDecl decl;
				decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
				
				OutputMemoryStream vertices(m_app.getAllocator());
				OutputMemoryStream indices(m_app.getAllocator());
				vertices.reserve(16 * 1024);
				const bool has_uvs = sg.flags.isSet(SplineGeometry::HAS_UVS);
				if (has_uvs) decl.addAttribute(1, 12, 2, gpu::AttributeType::FLOAT, 0);
				struct Vertex {
					Vec3 position;
					Vec2 uv;
				};

				const Transform spline_tr = universe.getTransform(e);
				const Transform spline_tr_inv = spline_tr.inverted();

				auto write_vertex = [&](const Vertex& v){
					Vec3 position = v.position;
					if (m_geometry_mode == GeometryMode::SNAP_ALL) {
						const DVec3 p = spline_tr.transform(v.position) + Vec3(0, 1 + m_snap_height, 0);
						const RayCastModelHit hit = render_scene->castRayTerrain(p, Vec3(0, -1, 0));
						if (hit.is_hit) {
							const DVec3 hp = hit.origin + (hit.t - m_snap_height) * hit.dir;
							position = Vec3(spline_tr_inv.transform(hp));
						}
					}
					vertices.write(position);
					if (has_uvs) {
						vertices.write(v.uv);
					}
					if (sg.num_user_channels > 0) {
						u32 tmp = 0;
						vertices.write(&tmp, sg.num_user_channels);
					}
				};
						
				float u = 0;
				u32 rows = 0;
				Vec3 prev_p = spline.points[0];
				const u32 u_density = sg.u_density;
				while (!iterator.isEnd()) {
					++rows;
					Vec3 p = iterator.getPosition();
					if (m_geometry_mode == GeometryMode::SNAP_CENTER) {
						const DVec3 pglob = spline_tr.transform(p) + Vec3(0, 100 + m_snap_height, 0);
						const RayCastModelHit hit = render_scene->castRayTerrain(pglob, Vec3(0, -1, 0));
						if (hit.is_hit) {
							const DVec3 hp = hit.origin + (hit.t - m_snap_height) * hit.dir;
							p = Vec3(spline_tr_inv.transform(hp));
						}
					}

					const Vec3 dir = iterator.getDir();
					const Vec3 side = normalize(cross(Vec3(0, 1, 0), dir)) * width;
					u += length(p - prev_p);

					const Vec3 p0 = p - side;

					for (u32 i = 0; i < u_density; ++i) {
					
						Vertex v;
						v.position = p0 + 2 * side * (i / float(u_density - 1));
						v.uv.x = u;
						v.uv.y = i / float(u_density - 1) * width;
								
						write_vertex(v);
					}

					iterator.move(sg.v_density);
					prev_p = p;
				}

				const bool u16indices = u_density * rows < 0xffFF;

				if (u16indices) {
					for (u32 row = 0; row < rows - 1; ++row) {
						for (u32 i = 0; i < u_density - 1; ++i) {
							indices.write(u16(u_density * row + i));
							indices.write(u16(u_density * row + i + 1));
							indices.write(u16(u_density * (row + 1) + i));

							indices.write(u16(u_density * row + i + 1));
							indices.write(u16(u_density * (row + 1) + i));
							indices.write(u16(u_density * (row + 1) + i + 1));
						}
					}
				}
				else {
					for (u32 row = 0; row < rows - 1; ++row) {
						for (u32 i = 0; i < u_density - 1; ++i) {
							indices.write(u32(u_density * row + i));
							indices.write(u32(u_density * row + i + 1));
							indices.write(u32(u_density * (row + 1) + i));

							indices.write(u32(u_density * row + i + 1));
							indices.write(u32(u_density * (row + 1) + i));
							indices.write(u32(u_density * (row + 1) + i + 1));
						}
					}
				}

				if (sg.num_user_channels > 0) {
					decl.addAttribute(2, has_uvs ? 20 : 12, sg.num_user_channels, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
				}

				render_scene->setProceduralGeometry(e, vertices, decl, gpu::PrimitiveType::TRIANGLES, indices, u16indices ? gpu::DataType::U16 : gpu::DataType::U32);
			}
		}
	}
}

} // namespace Lumix