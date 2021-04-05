#define LUMIX_NO_CUSTOM_CRT
#include "editor/gizmo.h"
#include "editor/property_grid.h"
#include "editor/spline_editor.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/core.h"
#include "engine/geometry.h"
#include "engine/universe.h"
#include <imgui/imgui.h>

namespace Lumix {

static const ComponentType SPLINE_TYPE = reflection::getComponentType("spline");

struct SplineEditorPlugin : StudioApp::IPlugin, StudioApp::MousePlugin, PropertyGrid::IPlugin {
	SplineEditorPlugin(StudioApp& app)
		: m_app(app)
	{}

	~SplineEditorPlugin() {
		m_app.getPropertyGrid().removePlugin(*this);
		m_app.removePlugin(*this);
	}

	bool onMouseDown(UniverseView& view, int x, int y) {
		return getSplineEntity().isValid();
	}

	EntityPtr getSplineEntity() {
		WorldEditor& editor = m_app.getWorldEditor();
		const Array<EntityRef>& selected = editor.getSelectedEntities();
		if (selected.size() != 1) return INVALID_ENTITY;
		
		if (editor.getUniverse()->hasComponent(selected[0], SPLINE_TYPE)) return selected[0];
		return INVALID_ENTITY;
	
	}

	void onMouseUp(UniverseView& view, int x, int y, os::MouseButton button) {
		DVec3 origin;
		Vec3 dir;
		const Viewport& vp = view.getViewport();
		vp.getRay(Vec2((float)x, (float)y), origin, dir);

		const EntityRef e = *getSplineEntity();
		Universe* universe = m_app.getWorldEditor().getUniverse();
		const Transform tr = universe->getTransform(e);

		const Vec3 rel_pos = Vec3(origin - tr.pos);
		const Vec3 n = tr.rot.rotate(Vec3(0, 1, 0));

		Spline* spline = getSpline();
		if (spline) {
			for (const Vec3& point : spline->points) {
				const DVec3 p = tr.pos + point;
				float t;
				const bool hovered = getRaySphereIntersection(Vec3(0), dir, Vec3(p - vp.pos), 0.1f, t);
				if (hovered) {
					m_selected = i32(&point - spline->points.begin());
					return;
				}
			}
		}

		float t;
		if (getRayPlaneIntersecion(rel_pos, dir, Vec3(0), n, t)) {
			CoreScene* scene = (CoreScene*)universe->getScene(SPLINE_TYPE);
			Spline& spline = scene->getSpline(e);
			m_selected = (i32)spline.points.size();
			spline.points.push(rel_pos + dir * t);
		}
	}

	Spline* getSpline() const {
		WorldEditor& editor = m_app.getWorldEditor();
		const Array<EntityRef>& selected = editor.getSelectedEntities();
		if (selected.size() != 1) return nullptr;

		Universe* universe = editor.getUniverse();

		if (!universe->hasComponent(selected[0], SPLINE_TYPE)) return nullptr;

		CoreScene* scene = (CoreScene*)universe->getScene(SPLINE_TYPE);
		return &scene->getSpline(selected[0]);
	}

	void onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) override {
		if (cmp.type != SPLINE_TYPE) return;

		Spline* spline = getSpline();
		if (spline && !spline->points.empty() && ImGui::Button("Clear")) {
			spline->points.clear();
		}
	}

	void init() override {
		m_app.getPropertyGrid().addPlugin(*this);
		m_app.addPlugin((StudioApp::MousePlugin&)*this);
	}

	bool showGizmo(struct UniverseView& view, struct ComponentUID cmp) override {
		if (cmp.type != SPLINE_TYPE) return false;
		
		const EntityRef e = (EntityRef)cmp.entity;
		Universe& universe = cmp.scene->getUniverse();
		if (!universe.hasComponent(e, SPLINE_TYPE)) return false;

		CoreScene* scene = (CoreScene*)cmp.scene;
		Spline& spline = scene->getSpline(e);
		if (spline.points.size() < 2) return false;

		UniverseView::Vertex* vertices = view.render(true, (spline.points.size() - 1) * 2);
		const Transform& tr = universe.getTransform(e);
		const DVec3 cam_pos = view.getViewport().pos;
		const Vec3 offset = Vec3(tr.pos - cam_pos);
		DVec3 origin;
		Vec3 dir;
		view.getViewport().getRay(view.getMousePos(), origin, dir);
		for (i32 i = 0; i < spline.points.size(); ++i) {
			const DVec3 p = tr.pos + spline.points[i];
			float t;
			const bool hovered = getRaySphereIntersection(Vec3(0), dir, Vec3(p - cam_pos), 0.1f, t);
			addCircle(view, p, 0.1f, tr.rot.rotate(Vec3(0, 1, 0)), hovered ? Color::RED : Color::GREEN);
		}

		for (i32 i = 1; i < spline.points.size(); ++i) {
			vertices[(i - 1) * 2].pos = offset + spline.points[i - 1];
			vertices[(i - 1) * 2].abgr = 0xffFFffFF; 
			vertices[(i - 1) * 2 + 1].pos = offset + spline.points[i];
			vertices[(i - 1) * 2 + 1].abgr = 0xffFFffFF; 
		}

		
		auto evalCurve = [](const Vec3& a, const Vec3& b, const Vec3& c, float t){
			return lerp(lerp(a, b, t), lerp(b, c, t), t);
		};

		if (spline.points.size() > 2) {
			UniverseView::Vertex* curves = view.render(true, (spline.points.size() - 2) * 20);
			for (i32 i = 2; i < spline.points.size(); ++i) {
				const Vec3 p1 = spline.points[i - 1];
				const Vec3 p0 = lerp(spline.points[i - 2], p1, 0.5f);
				const Vec3 p2 = lerp(spline.points[i], p1, 0.5f);

				Vec3 p = p0;
				for (u32 j = 1; j < 11; ++j) {
					Vec3 r = evalCurve(p0, p1, p2, j / 10.f);
					curves[(i - 2) * 20 + (j - 1) * 2].pos = offset + p;
					curves[(i - 2) * 20 + (j - 1) * 2].abgr = 0xffff00ff;
					curves[(i - 2) * 20 + (j - 1) * 2 + 1].pos = offset + r;
					curves[(i - 2) * 20 + (j - 1) * 2 + 1].abgr = 0xffff00ff;
					p = r;
				}
			}
		}

		if (m_selected >= 0 && m_selected < spline.points.size()) {
			Transform point_tr = tr;
			point_tr.pos += spline.points[m_selected];
			Gizmo::Config cfg;
			if (Gizmo::manipulate(u64(3) << 32 | e.index, view, point_tr, cfg)){
				spline.points[m_selected] = Vec3(point_tr.pos - tr.pos);
			}
		}

		return true;
	}

	const char* getName() const override { return "spline_editor"; }

	StudioApp& m_app;
	i32 m_selected = -1;
};

StudioApp::IPlugin* createSplineEditor(StudioApp& app) {
	return LUMIX_NEW(app.getAllocator(), SplineEditorPlugin)(app);
}


}