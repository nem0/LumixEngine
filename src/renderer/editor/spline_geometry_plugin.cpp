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
	for (u8* iter = pg.vertex_data.getMutableData(); iter < end; iter += stride) {
		Vec3 p;
		memcpy(&p, iter, sizeof(p));

		if (squaredLength(p - center) < R2) {
			*(iter + offset) = m_brush_value;
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

	const Universe& universe = *editor.getUniverse();
	const bool is_spline = universe.hasComponent(selected[0], SPLINE_GEOMETRY_TYPE);
	if (!is_spline) return false;

	RenderScene* scene = (RenderScene*)universe.getScene(SPLINE_GEOMETRY_TYPE);
	DVec3 origin;
	Vec3 dir;
	view.getViewport().getRay({(float)x, (float)y}, origin, dir);
	const RayCastModelHit hit = scene->castRayProceduralGeometry(origin, dir);
	if (!hit.is_hit) return false;
	if (hit.entity != selected[0]) return false;

	Renderer* renderer = (Renderer*)editor.getEngine().getPluginManager().getPlugin("renderer");
	ASSERT(renderer);

	ProceduralGeometry& pg = scene->getProceduralGeometry(selected[0]);
	const SplineGeometry& sg = scene->getSplineGeometry(selected[0]);
	paint(hit.origin + hit.t * hit.dir, universe, selected[0], sg, pg, *renderer);

	return true;
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
	const RayCastModelHit hit = scene->castRayProceduralGeometry(origin, dir);

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

	for (u32 i = 0, c = pg.getIndexCount(); i < c; ++i) {
		Vec3 p;
		memcpy(&p, data + stride * i, sizeof(p));
		if (squaredLength(center_local - p) < R2) {
			addCircle(view, tr.transform(p), 0.1f, Vec3(0, 1, 0), Color::BLUE);
		}
	}
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

		ImGuiEx::Label("Brush size");
		ImGui::DragFloat("##bs", &m_brush_size, 0.1f, 0, FLT_MAX);

		if (sg.num_user_channels > 1) {
			ImGuiEx::Label("Paint channel");
			ImGui::SliderInt("##pc", (int*)&m_brush_channel, 0, sg.num_user_channels - 1);
		}

		ImGuiEx::Label("Paint value");
		ImGui::SliderInt("##pv", (int*)&m_brush_value, 0, 255);

		if (ImGui::Button("Generate geometry")) {
			if (!spline.points.empty()) {
				const float width = sg.width;
				SplineIterator iterator(spline.points);
				gpu::VertexDecl decl;
				decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
				
				OutputMemoryStream vertices(m_app.getAllocator());
				OutputMemoryStream indices(m_app.getAllocator());
				vertices.reserve(16 * 1024);
				if (sg.flags.isSet(SplineGeometry::HAS_UVS)) {
					decl.addAttribute(1, 12, 2, gpu::AttributeType::FLOAT, 0);
					struct Vertex {
						Vec3 position;
						Vec2 uv;
					};

					auto write_vertex = [&](const Vertex& v){
						vertices.write(v);
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
						const Vec3 p = iterator.getPosition();
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

					if (sg.num_user_channels > 0) {
						decl.addAttribute(2, 20, sg.num_user_channels, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
					}
				}
				else {
					while (!iterator.isEnd()) {
						const Vec3 p = iterator.getPosition();
						const Vec3 dir = iterator.getDir();
						const Vec3 side = normalize(cross(Vec3(0, 1, 0), dir)) * width;
						const Vec3 v0 = p + side;
						const Vec3 v1 = p - side;
						vertices.write(v0);
						if (sg.num_user_channels > 0) {
							u32 tmp = 0;
							vertices.write(&tmp, sg.num_user_channels);
						}
						vertices.write(v1);
						if (sg.num_user_channels > 0) {
							u32 tmp = 0;
							vertices.write(&tmp, sg.num_user_channels);
						}
						iterator.move(0.1f);
					}
					if (sg.num_user_channels > 0) {
						decl.addAttribute(1, 12, sg.num_user_channels, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
					}
				}

				render_scene->setProceduralGeometry(e, vertices, decl, gpu::PrimitiveType::TRIANGLES, indices, gpu::DataType::U16);
			}
		}
	}
}

} // namespace Lumix