#include "core/geometry.h"
#include "core/string.h"

#include "editor/asset_compiler.h"
#include "editor/gizmo.h"
#include "editor/property_grid.h"
#include "editor/spline_editor.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/component_types.h"
#include "engine/component_uid.h"
#include "engine/core.h"
#include "engine/engine.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include <math.h>
#include <imgui/imgui.h>

namespace black {

struct SplineEditorPlugin : SplineEditor, StudioApp::MousePlugin, PropertyGrid::IPlugin {
	SplineEditorPlugin(StudioApp& app)
		: m_app(app)
	{}

	~SplineEditorPlugin() {
		m_app.getPropertyGrid().removePlugin(*this);
		m_app.removePlugin(*this);
	}

	bool onMouseDown(WorldView& view, int x, int y) override {
		Spline* spline = getSpline();
		if (!spline) return false;
		
		World* world = m_app.getWorldEditor().getWorld();
		const EntityRef e = *getSplineEntity();
		const Transform tr = world->getTransform(e);
		const Viewport& vp = view.getViewport();
		const Ray ray = vp.getRay(Vec2((float)x, (float)y));
		
		for (const Vec3& point : spline->points) {
			const DVec3 p = tr.pos + point;
			float t;
			const bool hovered = getRaySphereIntersection(Vec3(0), ray.dir, Vec3(p - vp.pos), 0.1f, t);
			if (hovered) return true;
		}

		return m_hovered_gizmo || ImGui::GetIO().KeyAlt;
	}

	EntityPtr getSplineEntity() {
		WorldEditor& editor = m_app.getWorldEditor();
		Span<const EntityRef> selected = editor.getSelectedEntities();
		if (selected.size() != 1) return INVALID_ENTITY;
		
		if (editor.getWorld()->hasComponent(selected[0], types::spline)) return selected[0];
		return INVALID_ENTITY;
	}

	void onMouseUp(WorldView& view, int x, int y, os::MouseButton button) override {
		const Viewport& vp = view.getViewport();
		const Ray ray = vp.getRay(Vec2((float)x, (float)y));

		const EntityRef e = *getSplineEntity();
		World* world = m_app.getWorldEditor().getWorld();
		const Transform tr = world->getTransform(e);

		Spline* spline = getSpline();
		if (spline) {
			for (const Vec3& point : spline->points) {
				const DVec3 p = tr.pos + point;
				float t;
				const bool hovered = getRaySphereIntersection(Vec3(0), ray.dir, Vec3(p - vp.pos), 0.1f, t);
				if (hovered) {
					m_selected = i32(&point - spline->points.begin());
					return;
				}
			}
		}

		RayHit hit = view.getCameraRaycastHit(x, y, INVALID_ENTITY);
		if (hit.is_hit) {
			CoreModule* module = (CoreModule*)world->getModule(types::spline);
			Spline& hit_spline = module->getSpline(e);
			m_selected = (i32)hit_spline.points.size();
			recordUndo(-1, hit_spline, e, [&](){
				hit_spline.points.push(Vec3(hit.pos - tr.pos));
			});
		}
	}

	void setSplinePoints(EntityRef entity, Span<const Vec3> points) override {
		WorldEditor& editor = m_app.getWorldEditor();
		World* world = editor.getWorld();

		ASSERT(world->hasComponent(entity, types::spline));

		CoreModule* module = (CoreModule*)world->getModule(types::spline);
		Spline& spline = module->getSpline(entity);
		
		recordUndo(-1, spline, entity, [&](){
			spline.points.clear();
			for (Vec3 p : points) spline.points.push(p);
		});
	}

	Spline* getSpline() const {
		WorldEditor& editor = m_app.getWorldEditor();
		Span<const EntityRef> selected = editor.getSelectedEntities();
		if (selected.size() != 1) return nullptr;

		World* world = editor.getWorld();

		if (!world->hasComponent(selected[0], types::spline)) return nullptr;

		CoreModule* module = (CoreModule*)world->getModule(types::spline);
		return &module->getSpline(selected[0]);
	}

	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor) override {
		if (filter.isActive()) return;
		if (cmp_type != types::spline) return;
		if (entities.length() != 1) return;

		const EntityRef entity = entities[0];
		Spline* spline = getSpline();
		ASSERT(spline);

		if (!spline->points.empty() && ImGui::Button("Clear")) {
			recordUndo(-1, *spline, entity, [&](){
				spline->points.clear();
			});
		}

		ImGui::SameLine();
		if (m_selected >= 0 && m_selected < spline->points.size() && ImGui::Button("Delete node")) {
			recordUndo(-1, *spline, entity, [&](){
				spline->points.erase(m_selected);
			});
		}

		ImGui::TextUnformatted("Alt + mouse click - create new node");
	}

	struct EditorCommand : IEditorCommand {
		EditorCommand(WorldEditor& editor, IAllocator& allocator)
			: editor(editor)
			, old_points(allocator)
			, new_points(allocator)
		{}

		bool execute() override { 
			CoreModule* module = (CoreModule*)editor.getWorld()->getModule(types::spline);
			Spline& spline = module->getSpline(e);
			new_points.copyTo(spline.points);
			return true;
		}

		void undo() override {
			CoreModule* module = (CoreModule*)editor.getWorld()->getModule(types::spline);
			Spline& spline = module->getSpline(e);
			old_points.copyTo(spline.points);
		}

		const char* getType() override { return "edit_spline"; }
		bool merge(IEditorCommand& command) override { 
			EditorCommand& rhs = ((EditorCommand&)command);
			if (id == -1 || id != rhs.id) return false;
			rhs.new_points = new_points.move();
			return true;
		}
		
		WorldEditor& editor;
		i32 id;
		EntityRef e;
		Array<Vec3> old_points;
		Array<Vec3> new_points;
	};

	template <typename T>
	void recordUndo(i32 id, Spline& spline, EntityRef e, T&& f) {
		WorldEditor& editor = m_app.getWorldEditor();
		IAllocator& allocator = editor.getAllocator();
		UniquePtr<EditorCommand> cmd = UniquePtr<EditorCommand>::create(allocator, editor, allocator);
		spline.points.copyTo(cmd->old_points);
		cmd->e = e;
		cmd->id = id;
		f();
		spline.points.copyTo(cmd->new_points);
		m_app.getWorldEditor().executeCommand(cmd.move());
	}

	void init() override {
		m_app.getPropertyGrid().addPlugin(*this);
		m_app.addPlugin((StudioApp::MousePlugin&)*this);
	}

	bool showGizmo(WorldView& view, ComponentUID cmp) override {
		m_hovered_gizmo = false;
		if (cmp.type != types::spline) return false;
		
		const EntityRef e = (EntityRef)cmp.entity;
		World& world = cmp.module->getWorld();
		if (!world.hasComponent(e, types::spline)) return false;

		CoreModule* module = (CoreModule*)cmp.module;
		Spline& spline = module->getSpline(e);
		if (spline.points.size() == 0) return false;

		WorldView::Vertex* vertices = view.render(true, (spline.points.size() - 1) * 2);
		const Transform& tr = world.getTransform(e);
		const DVec3 cam_pos = view.getViewport().pos;
		const Vec3 offset = Vec3(tr.pos - cam_pos);
		const Ray ray = view.getViewport().getRay(view.getMousePos());
		for (i32 i = 0; i < spline.points.size(); ++i) {
			const DVec3 p = tr.pos + spline.points[i];
			float t;
			const bool hovered = getRaySphereIntersection(Vec3(0), ray.dir, Vec3(p - cam_pos), 0.1f, t);
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
			WorldView::Vertex* curves = view.render(true, (spline.points.size() - 2) * 20);
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
				recordUndo(m_selected, spline, e, [&](){
					spline.points[m_selected] = Vec3(point_tr.pos - tr.pos);
				});
			}
			m_hovered_gizmo = Gizmo::isActive();
		}

		return true;
	}

	const char* getName() const override { return "spline_editor"; }

	StudioApp& m_app;
	i32 m_selected = -1;
	bool m_hovered_gizmo = false;
};

SplineEditor* createSplineEditor(StudioApp& app) {
	return BLACK_NEW(app.getAllocator(), SplineEditorPlugin)(app);
}


}