#define LUMIX_NO_CUSTOM_CRT

#include "../render_scene.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/core.h"
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
			
		const ProceduralGeometry& pg = render_scene->getProceduralGeometries()[e];
			
		ImGuiEx::Label("Triangles");
		ImGui::Text("%d", pg.index_data.size() / (pg.index_type == gpu::DataType::U16 ? 2 : 4) / 3);

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
						decl.addAttribute(2, 20, sg.num_user_channels, gpu::AttributeType::U8, 0);
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
						decl.addAttribute(1, 12, sg.num_user_channels, gpu::AttributeType::U8, 0);
					}
				}

				render_scene->setProceduralGeometry(e, vertices, decl, gpu::PrimitiveType::TRIANGLES, indices, gpu::DataType::U16);
			}
		}
	}
}

} // namespace Lumix