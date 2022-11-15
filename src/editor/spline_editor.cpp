#define LUMIX_NO_CUSTOM_CRT
#include "editor/asset_compiler.h"
#include "editor/gizmo.h"
#include "editor/prefab_system.h"
#include "editor/property_grid.h"
#include "editor/spline_editor.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/core.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/prefab.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/universe.h"
#include <math.h>
#include <imgui/imgui.h>

namespace Lumix {

static const ComponentType SPLINE_TYPE = reflection::getComponentType("spline");

struct SplineEditorPlugin : SplineEditor, StudioApp::MousePlugin, PropertyGrid::IPlugin {
	SplineEditorPlugin(StudioApp& app)
		: m_app(app)
		, m_selected_prefabs(app.getAllocator())
	{}

	~SplineEditorPlugin() {
		m_app.getPropertyGrid().removePlugin(*this);
		m_app.removePlugin(*this);
	}

	bool onMouseDown(UniverseView& view, int x, int y) {
		Spline* spline = getSpline();
		if (!spline) return false;
		
		Universe* universe = m_app.getWorldEditor().getUniverse();
		const EntityRef e = *getSplineEntity();
		const Transform tr = universe->getTransform(e);
		DVec3 origin;
		Vec3 dir;
		const Viewport& vp = view.getViewport();
		vp.getRay(Vec2((float)x, (float)y), origin, dir);
		
		for (const Vec3& point : spline->points) {
			const DVec3 p = tr.pos + point;
			float t;
			const bool hovered = getRaySphereIntersection(Vec3(0), dir, Vec3(p - vp.pos), 0.1f, t);
			if (hovered) return true;
		}

		return m_hovered_gizmo || ImGui::GetIO().KeyAlt;
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

		UniverseView::RayHit hit = view.getCameraRaycastHit(x, y, INVALID_ENTITY);
		if (hit.is_hit) {
			CoreScene* scene = (CoreScene*)universe->getScene(SPLINE_TYPE);
			Spline& hit_spline = scene->getSpline(e);
			m_selected = (i32)hit_spline.points.size();
			recordUndo(-1, hit_spline, e, [&](){
				hit_spline.points.push(Vec3(hit.pos - tr.pos));
			});
		}
	}

	void setSplinePoints(EntityRef entity, Span<const Vec3> points) override {
		WorldEditor& editor = m_app.getWorldEditor();
		Universe* universe = editor.getUniverse();

		ASSERT(universe->hasComponent(entity, SPLINE_TYPE));

		CoreScene* scene = (CoreScene*)universe->getScene(SPLINE_TYPE);
		Spline& spline = scene->getSpline(entity);
		
		recordUndo(-1, spline, entity, [&](){
			spline.points.clear();
			for (Vec3 p : points) spline.points.push(p);
		});
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

	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override {
		if (cmp_type != SPLINE_TYPE) return;
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

		prefabsList();
	}

	struct EditorCommand : IEditorCommand {
		EditorCommand(WorldEditor& editor, IAllocator& allocator)
			: editor(editor)
			, old_points(allocator)
			, new_points(allocator)
		{}

		bool execute() override { 
			CoreScene* scene = (CoreScene*)editor.getUniverse()->getScene(SPLINE_TYPE);
			Spline& spline = scene->getSpline(e);
			spline.points = new_points.makeCopy();
			return true;
		}

		void undo() override {
			CoreScene* scene = (CoreScene*)editor.getUniverse()->getScene(SPLINE_TYPE);
			Spline& spline = scene->getSpline(e);
			spline.points = old_points.makeCopy();
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
		cmd->old_points = spline.points.makeCopy();
		cmd->e = e;
		cmd->id = id;
		f();
		cmd->new_points = spline.points.makeCopy();
		m_app.getWorldEditor().executeCommand(cmd.move());
	}

	void prefabsList() {
		if (!ImGui::CollapsingHeader("Place prefabs")) return;

		static ImVec2 size(-1, 200);
		const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
		ImGui::SetNextItemWidth(-w);
		ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));
		ImGui::SameLine();
		if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) m_filter[0] = '\0';

		if (ImGui::BeginListBox("##prefabs", size)) {
			auto& resources = m_app.getAssetCompiler().lockResources();
			u32 count = 0;
			for (const AssetCompiler::ResourceItem& res : resources) {
				if (res.type != PrefabResource::TYPE) continue;
				++count;
				if (m_filter[0] != 0 && stristr(res.path.c_str(), m_filter) == nullptr) continue;
				int selected_idx = m_selected_prefabs.find([&](PrefabResource* r) -> bool {
					return r && r->getPath() == res.path;
				});
				bool selected = selected_idx >= 0;
				const char* loading_str = selected_idx >= 0 && m_selected_prefabs[selected_idx]->isEmpty() ? " - loading..." : "";
				StaticString<LUMIX_MAX_PATH + 15> label(res.path.c_str(), loading_str);
				if (ImGui::Selectable(label, &selected)) {
					if (selected) {
						ResourceManagerHub& manager = m_app.getEngine().getResourceManager();
						PrefabResource* prefab = manager.load<PrefabResource>(res.path);
						if (!ImGui::GetIO().KeyShift) {
							for (PrefabResource* iter : m_selected_prefabs) iter->decRefCount();
							m_selected_prefabs.clear();
						}
						m_selected_prefabs.push(prefab);
					}
					else {
						PrefabResource* prefab = m_selected_prefabs[selected_idx];
						if (!ImGui::GetIO().KeyShift) {
							for (PrefabResource* iter : m_selected_prefabs) iter->decRefCount();
							m_selected_prefabs.clear();
						}
						else {
							m_selected_prefabs.swapAndPop(selected_idx);
							prefab->decRefCount();
						}
					}
				}
			}
			if (count == 0) ImGui::TextUnformatted("No prefabs");
			m_app.getAssetCompiler().unlockResources();
			ImGui::EndListBox();
		}
		ImGuiEx::HSplitter("after_prefab", &size);
		ImGuiEx::Label("Spacing");
		ImGui::DragFloat("##spacing", &m_spacing);
		ImGuiEx::Label("Rotate by 90deg");
		ImGui::Checkbox("##rot90", &m_rotate_by_90deg);
		ImGuiEx::Label("Place as children");
		ImGui::Checkbox("##chil", &m_place_as_children);
		ImGuiEx::Label("Random rotation");
		ImGui::Checkbox("##randrot", &m_random_rotation);
		ImGuiEx::Label("XZ dispersion");
		ImGui::DragFloat("##disp", &m_dispersion);
		
		if (ImGui::Button("Snap")) snap();
		ImGui::SameLine();
		if (ImGui::Button("Place")) {
			Spline* spline = getSpline();
			WorldEditor& editor = m_app.getWorldEditor();
			const EntityRef spline_entity = *getSplineEntity();
			const Transform tr = editor.getUniverse()->getTransform(spline_entity);
			float f = 0;
			float offset = 0;
			editor.beginCommandGroup("spline_place");
			for (i32 i = 1; i < spline->points.size(); ++i) {
				const Vec3 p0 = spline->points[i - 1];
				const Vec3 p1 = spline->points[i];
				const float l = length(p0 - p1);
				const Vec3 dir = (p1 - p0) / l;

				while (f < offset + l) {
					const EntityPtr e = place(tr.pos + lerp(p0, p1, (f - offset) / l), dir);
					if (!e.isValid()) break;
					if (m_place_as_children) editor.makeParent(spline_entity, *e);
					f += m_spacing;
				}

				offset += l;
			}
			editor.endCommandGroup();
		}
	}

	void snap() {
		Spline* spline = getSpline();
		recordUndo(-1, *spline, *getSplineEntity(), [&](){
			for (i32 i = 1; i < spline->points.size(); ++i) {
				const Vec3 d = spline->points[i] - spline->points[i - 1];
				const float l = length(d) + 1e-5f;
				const Vec3 dir = d / l;
				const u32 count = u32(l / m_spacing + 0.5f);
				spline->points[i] = spline->points[i - 1] + dir * (m_spacing * count);
			}
		});
	}

	static Vec3 randomXZVec() {
		float a = randFloat() * 2.f * PI;
		float d = randFloat() + randFloat();
		d = d > 1 ? 2 - d : d;
		float c = cosf(a) * d;
		float s = sinf(a) * d;
		return Vec3(c, 0, s);
	}

	EntityPtr place(const DVec3& pos, const Vec3& dir) const {
		if (m_selected_prefabs.empty()) return INVALID_ENTITY;

		PrefabSystem& prefab_system = m_app.getWorldEditor().getPrefabSystem();

		PrefabResource* res = m_selected_prefabs[rand() % m_selected_prefabs.size()];
		float a = atan2f(dir.x, dir.z);
		Quat rot = Quat(Vec3(0, 1, 0), a);
		if (m_rotate_by_90deg) rot = rot * Quat(0, 0.707f, 0, -0.707f);
		if (m_random_rotation) rot = Quat(Vec3(0, 1, 0), randFloat() * PI * 2);
		const DVec3 p = pos + m_dispersion * randomXZVec();
		return prefab_system.instantiatePrefab(*res, p, rot, 1.f);		
	}

	void init() override {
		m_app.getPropertyGrid().addPlugin(*this);
		m_app.addPlugin((StudioApp::MousePlugin&)*this);
	}

	bool showGizmo(struct UniverseView& view, struct ComponentUID cmp) override {
		m_hovered_gizmo = false;
		if (cmp.type != SPLINE_TYPE) return false;
		
		const EntityRef e = (EntityRef)cmp.entity;
		Universe& universe = cmp.scene->getUniverse();
		if (!universe.hasComponent(e, SPLINE_TYPE)) return false;

		CoreScene* scene = (CoreScene*)cmp.scene;
		Spline& spline = scene->getSpline(e);
		if (spline.points.size() == 0) return false;

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
	Array<PrefabResource*> m_selected_prefabs;
	i32 m_selected = -1;
	char m_filter[64] = "";
	bool m_rotate_by_90deg = false;
	bool m_place_as_children = true;
	bool m_random_rotation = false;
	bool m_hovered_gizmo = false;
	float m_dispersion = 0;
	float m_spacing = 1.f;
};

SplineEditor* createSplineEditor(StudioApp& app) {
	return LUMIX_NEW(app.getAllocator(), SplineEditorPlugin)(app);
}


}