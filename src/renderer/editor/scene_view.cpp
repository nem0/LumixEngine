#include <imgui/imgui.h>

#include "scene_view.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/gizmo.h"
#include "editor/log_ui.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "core/allocators.h"
#include "core/delegate_list.h"
#include "engine/engine.h"
#include "core/geometry.h"
#include "engine/lua_wrapper.h"
#include "core/os.h"
#include "core/path.h"
#include "core/path.h"
#include "engine/prefab.h"
#include "core/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "core/string.h"
#include "engine/world.h"
#include "lua_script/lua_script.h"
#include "renderer/culling_system.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include "renderer/gpu/gpu.h"
#include "renderer/draw_stream.h"
#include "renderer/editor/editor_icon.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/pose.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"

namespace Lumix
{

static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType PARTICLE_EMITTER_TYPE = reflection::getComponentType("particle_emitter");
static const ComponentType MESH_ACTOR_TYPE = reflection::getComponentType("rigid_actor");

struct WorldViewImpl final : WorldView {
	enum class MouseMode
	{
		NONE,
		SELECT,
		NAVIGATE,
		PAN,

		CUSTOM
	};

	enum class SnapMode
	{
		NONE,
		FREE,
		VERTEX
	};

	WorldViewImpl(SceneView& view) 
		: m_scene_view(view)
		, m_app(view.m_app)
		, m_editor(view.m_editor) 
		, m_module(nullptr)
		, m_draw_cmds(view.m_app.getAllocator())
		, m_draw_vertices(view.m_app.getAllocator())
	{
		m_editor.worldCreated().bind<&WorldViewImpl::onWorldCreated>(this);
		m_editor.worldDestroyed().bind<&WorldViewImpl::onWorldDestroyed>(this);
		m_viewport.is_ortho = false;
		m_viewport.pos = DVec3(0);
		m_viewport.rot.set(0, 0, 0, 1);
		m_viewport.w = -1;
		m_viewport.h = -1;
		m_viewport.fov = view.m_app.getFOV();
		m_viewport.near = 0.1f;
		m_viewport.far = 1'000'000.f;

		ResourceManagerHub& rm = m_editor.getEngine().getResourceManager();
		Path font_path("editor/fonts/notosans-regular.ttf");
		m_font_res = rm.load<FontResource>(font_path);
		m_font = m_font_res->addRef(16);
		onWorldCreated();
	}

	~WorldViewImpl() {
		m_font_res->decRefCount();
		m_editor.worldCreated().unbind<&WorldViewImpl::onWorldCreated>(this);
		m_editor.worldDestroyed().unbind<&WorldViewImpl::onWorldDestroyed>(this);
		onWorldDestroyed();
	}
	
	WorldEditor& getEditor() override { return m_editor; }

	void addCross(const DVec3& pos, float size, Color color) {
		addLine(*this, pos - Vec3(size, 0, 0), pos + Vec3(size, 0, 0), color);
		addLine(*this, pos - Vec3(0, size, 0), pos + Vec3(0, size, 0), color);
		addLine(*this, pos - Vec3(0, 0, size), pos + Vec3(0, 0, size), color);
	}

	void onWorldCreated(){
		m_module = (RenderModule*)m_editor.getWorld()->getModule("renderer");
		m_icons = EditorIcons::create(m_editor, *m_module);
	}

	void onWorldDestroyed(){
		m_icons.reset();
	}

	void setSnapMode(bool enable, bool vertex_snap) override
	{
		m_snap_mode = enable ? (vertex_snap ? SnapMode::VERTEX : SnapMode::FREE) : SnapMode::NONE;
	}

	void previewSnapVertex()
	{
		if (m_snap_mode != SnapMode::VERTEX) return;

		const Ray ray = m_viewport.getRay(m_mouse_pos);
		const RayCastModelHit hit = m_module->castRay(ray, INVALID_ENTITY);
		if (!hit.is_hit) return;

		const DVec3 snap_pos = getClosestVertex(hit);
		addCross(snap_pos, 1, Color::RED);
	}

	Vec2 getMouseSensitivity() override
	{
		return m_mouse_sensitivity;
	}

	void setMouseSensitivity(float x, float y) override
	{
		m_mouse_sensitivity.x = 10000 / x;
		m_mouse_sensitivity.y = 10000 / y;
	}

	void rectSelect()
	{
		Vec2 min = m_rect_selection_start;
		Vec2 max = m_mouse_pos;
		if (min.x > max.x) swap(min.x, max.x);
		if (min.y > max.y) swap(min.y, max.y);
		const ShiftedFrustum frustum = m_viewport.getFrustum(min, max);
		
		m_editor.selectEntities({}, false);
		
		for (int i = 0; i < (int)RenderableTypes::COUNT; ++i) {
			CullResult* renderables = m_module->getRenderables(frustum, (RenderableTypes)i);
			CullResult* first = renderables;
			while (renderables) {
				m_editor.selectEntities(Span(renderables->entities, renderables->header.count), true);
				renderables = renderables->header.next;
			}
			if (first) first->free(m_editor.getEngine().getPageAllocator());
		}
	}

	void onMouseWheel(float value) {
		if (m_mouse_mode == MouseMode::NONE) {
			for (StudioApp::MousePlugin* plugin : m_app.getMousePlugins()) {
				plugin->onMouseWheel(value);
			}
		}
	}

	void onMouseUp(int x, int y, os::MouseButton button)
	{
		m_mouse_pos = {(float)x, (float)y};
		if (m_mouse_mode == MouseMode::SELECT)
		{
			if ((m_rect_selection_start.x != m_mouse_pos.x || m_rect_selection_start.y != m_mouse_pos.y) && m_rect_selection_timer > 0.1f)
			{
				rectSelect();
			}
			else
			{
				const Ray ray = m_viewport.getRay(m_mouse_pos);
				const RayCastModelHit hit = m_module->castRay(ray, INVALID_ENTITY);

				const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
				if (m_snap_mode != SnapMode::NONE && !selected_entities.empty() && hit.is_hit)
				{
					DVec3 snap_pos = ray.origin + ray.dir * hit.t;
					if (m_snap_mode == SnapMode::VERTEX) snap_pos = getClosestVertex(hit);
					const Quat rot = m_editor.getWorld()->getRotation(selected_entities[0]);
					const Gizmo::Config& gizmo_cfg = m_app.getGizmoConfig();
					const Vec3 offset = rot.rotate(gizmo_cfg.getOffset());
					m_editor.snapEntities(snap_pos - offset, gizmo_cfg.isTranslateMode());
				}
				else
				{
					auto icon_hit = m_icons->raycast(ray.origin, ray.dir);
					if (icon_hit.entity != INVALID_ENTITY) {
						if(icon_hit.entity.isValid()) {
							EntityRef e = (EntityRef)icon_hit.entity;
							m_editor.selectEntities(Span(&e, 1), ImGui::GetIO().KeyCtrl);
						}
					}
					else if (hit.is_hit) {
						if (hit.entity.isValid()) {
							EntityRef entity = (EntityRef)hit.entity;
							m_editor.selectEntities(Span(&entity, 1), ImGui::GetIO().KeyCtrl);
						}
					} else {
						m_editor.selectEntities(Span((const EntityRef*)nullptr, (u32)0), false);
					}
				}
			}
		}

		m_is_mouse_down[(int)button] = false;
		if (m_mouse_handling_plugin)
		{
			m_mouse_handling_plugin->onMouseUp(*this, x, y, button);
			m_mouse_handling_plugin = nullptr;
		}
		m_mouse_mode = MouseMode::NONE;
	}

	Vec2 getMousePos() const override { return m_mouse_pos; }
	
	void resetPivot() override {
		m_app.getGizmoConfig().setOffset(Vec3::ZERO);
	}
	
	void setCustomPivot() override
	{
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.empty()) return;

		const Ray ray = m_viewport.getRay(m_mouse_pos);
		const RayCastModelHit hit = m_module->castRay(ray, INVALID_ENTITY);
		if (!hit.is_hit || hit.entity != selected_entities[0]) return;

		const DVec3 snap_pos = getClosestVertex(hit);

		const Transform tr = m_editor.getWorld()->getTransform(selected_entities[0]);
		m_app.getGizmoConfig().setOffset(tr.rot.conjugated() * Vec3(snap_pos - tr.pos));
	}


	DVec3 getClosestVertex(const RayCastModelHit& hit) const {
		ASSERT(hit.entity.isValid());
		const EntityRef entity = (EntityRef)hit.entity;
		const DVec3& wpos = hit.origin + hit.t * hit.dir;
		World& world = m_module->getWorld();
		const Transform tr = world.getTransform(entity);
		const Vec3 lpos = tr.rot.conjugated() * Vec3(wpos - tr.pos);
		if (!world.hasComponent(entity, MODEL_INSTANCE_TYPE)) return wpos;

		Model* model = m_module->getModelInstanceModel(entity);

		float min_dist_squared = FLT_MAX;
		Vec3 closest_vertex = lpos;
		auto processVertex = [&](const Vec3& vertex) {
			float dist_squared = squaredLength(vertex - lpos);
			if (dist_squared < min_dist_squared)
			{
				min_dist_squared = dist_squared;
				closest_vertex = vertex;
			}
		};

		for (int mesh_idx = 0, cm = model->getMeshCount(); mesh_idx < cm; ++mesh_idx)
		{
			Mesh& mesh = model->getMesh(mesh_idx);

			if (mesh.areIndices16())
			{
				const u16* indices = (const u16*)mesh.indices.data();
				for (i32 i = 0, c = (i32)mesh.indices.size() >> 1; i < c; ++i)
				{
					Vec3 vertex = mesh.vertices[indices[i]];
					processVertex(vertex);
				}
			}
			else
			{
				const u32* indices = (const u32*)mesh.indices.data();
				for (i32 i = 0, c = (i32)mesh.indices.size() >> 2; i < c; ++i)
				{
					Vec3 vertex = mesh.vertices[indices[i]];
					processVertex(vertex);
				}
			}
		}
		return tr.pos + tr.rot * closest_vertex;
	}

	void inputFrame() {
		for (auto& i : m_is_mouse_click) i = false;
	}

	bool isMouseClick(os::MouseButton button) const override { return m_is_mouse_click[(int)button]; }
	bool isMouseDown(os::MouseButton button) const override { return m_is_mouse_down[(int)button]; }

	void onMouseMove(int x, int y, int relx, int rely)
	{
		PROFILE_FUNCTION();
		m_mouse_pos = Vec2((float)x, (float)y);
		
		static const float MOUSE_MULTIPLIER = 1 / 200.0f;

		switch (m_mouse_mode)
		{
			case MouseMode::CUSTOM:
			{
				if (m_mouse_handling_plugin)
				{
					m_mouse_handling_plugin->onMouseMove(*this, x, y, relx, rely);
				}
			}
			break;
			case MouseMode::NAVIGATE: {
				const float yaw = -signum(relx) * (powf(fabsf((float)relx / m_mouse_sensitivity.x), 1.2f));
				const float pitch = -signum(rely) * (powf(fabsf((float)rely / m_mouse_sensitivity.y), 1.2f));
				rotateCamera(yaw, pitch);
				break;
			}
			case MouseMode::PAN: panCamera(relx * MOUSE_MULTIPLIER, rely * MOUSE_MULTIPLIER); break;
			case MouseMode::NONE:
			case MouseMode::SELECT:
				break;
		}
	}

	void onMouseDown(int x, int y, os::MouseButton button)
	{
		m_is_mouse_click[(int)button] = true;
		m_is_mouse_down[(int)button] = true;
		if(button == os::MouseButton::MIDDLE)
		{
			m_mouse_mode = MouseMode::PAN;
		}
		else if (button == os::MouseButton::RIGHT)
		{
			m_mouse_mode = MouseMode::NAVIGATE;
		}
		else if (button == os::MouseButton::LEFT)
		{
			const Ray ray = m_viewport.getRay({(float)x, (float)y});
			const RayCastModelHit hit = m_module->castRay(ray, INVALID_ENTITY);
			if (Gizmo::isActive()) return;

			if (m_scene_view.m_is_measure_active) {
				m_mouse_mode = MouseMode::NONE;
				if (!hit.is_hit) return;

				const DVec3 p = hit.origin + hit.t * hit.dir;
				if (m_scene_view.m_is_measure_from_set) m_scene_view.m_measure_to = p;
				else m_scene_view.m_measure_from = p; 
				m_scene_view.m_is_measure_from_set = !m_scene_view.m_is_measure_from_set;
				return;
			}

			for (StudioApp::MousePlugin* plugin : m_app.getMousePlugins()) {
				if (plugin->onMouseDown(*this, x, y)) {
					m_mouse_handling_plugin = plugin;
					m_mouse_mode = MouseMode::CUSTOM;
					return;
				}
			}
			m_mouse_mode = MouseMode::SELECT;
			m_rect_selection_start = {(float)x, (float)y};
			m_rect_selection_timer = 0;
		}
	}

	const Viewport& getViewport() const override { return m_viewport; }
	void setViewport(const Viewport& vp) override { m_viewport = vp; }

	void refreshIcons() override {
		m_icons->refresh();
	}


	void copyTransform() override {
		if (m_editor.getSelectedEntities().empty()) return;

		m_editor.setEntitiesPositionsAndRotations(m_editor.getSelectedEntities().begin(), &m_viewport.pos, &m_viewport.rot, 1);
	}

	void lookAtSelected() override {
		const World* world = m_editor.getWorld();
		if (m_editor.getSelectedEntities().empty()) return;

		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		const Vec3 dir = m_viewport.rot.rotate(Vec3(0, 0, 1));
		m_go_to_parameters.m_to = world->getPosition(m_editor.getSelectedEntities()[0]) + dir * 10;
		const double len = length(m_go_to_parameters.m_to - m_go_to_parameters.m_from);
		m_go_to_parameters.m_speed = maximum(100.0f / (len > 0 ? float(len) : 1), 2.0f);
		m_go_to_parameters.m_from_rot = m_go_to_parameters.m_to_rot = m_viewport.rot;

	}
	
	void setTopView() override {
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (!selected_entities.empty()) {
			auto* world = m_editor.getWorld();
			m_go_to_parameters.m_to = world->getPosition(selected_entities[0]) + Vec3(0, 10, 0);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(1, 0, 0), -PI * 0.5f);
	}

	void setFrontView() override {
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (!selected_entities.empty()) {
			auto* world = m_editor.getWorld();
			m_go_to_parameters.m_to = world->getPosition(selected_entities[0]) + Vec3(0, 0, -10);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), PI);
	}


	void setSideView() override {
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (!selected_entities.empty()) {
			auto* world = m_editor.getWorld();
			m_go_to_parameters.m_to = world->getPosition(selected_entities[0]) + Vec3(-10, 0, 0);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), -PI * 0.5f);
	}

	void moveCamera(float forward, float right, float up, float speed) override
	{
		const Quat rot = m_viewport.rot;
		
		right = m_app.getCommonActions().cam_orbit.isActive() ? 0 : right;

		m_viewport.pos += rot.rotate(Vec3(0, 0, -1)) * forward * speed;
		m_viewport.pos += rot.rotate(Vec3(1, 0, 0)) * right * speed;
		m_viewport.pos += rot.rotate(Vec3(0, 1, 0)) * up * speed;
	}

	void rotateCamera(float yaw, float pitch) {
		const World* world = m_editor.getWorld();
		DVec3 pos = m_viewport.pos;
		Quat rot = m_viewport.rot;

		Quat yaw_rot(Vec3(0, 1, 0), yaw);
		rot = normalize(yaw_rot * rot);

		Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
		const Quat pitch_rot(pitch_axis, pitch);
		rot = normalize(pitch_rot * rot);

		if (m_app.getCommonActions().cam_orbit.isActive() && !m_editor.getSelectedEntities().empty()) {
			const Vec3 dir = rot.rotate(Vec3(0, 0, 1));
			const Transform tr = world->getTransform(m_editor.getSelectedEntities()[0]);
			Vec3 offset = m_app.getGizmoConfig().offset;
			const DVec3 orbit_point = tr.pos + tr.rot.rotate(offset);
			const float dist = float(length(orbit_point - pos));
			pos = orbit_point + dir * dist;
		}

		m_viewport.pos = pos;
		m_viewport.rot = rot;
	}

	void panCamera(float x, float y) {
		const Quat rot = m_viewport.rot;

		m_viewport.pos += rot.rotate(Vec3(x, 0, 0));
		m_viewport.pos += rot.rotate(Vec3(0, -y, 0));
	}

	void update(float time_delta) {
		PROFILE_FUNCTION();
		if (m_mouse_mode == MouseMode::SELECT) {
			m_rect_selection_timer += time_delta;
		}

		if (!m_viewport.is_ortho) {
			m_viewport.fov = m_app.getFOV();
		}
		previewSnapVertex();
		
		if (m_is_mouse_down[(int)os::MouseButton::LEFT] && m_mouse_mode == MouseMode::SELECT) {
			Draw2D& d2d = m_scene_view.getPipeline()->getDraw2D();
			d2d.addRect(m_rect_selection_start, m_mouse_pos, Color(0xfffffFFF), 1);
			d2d.addRect(m_rect_selection_start - Vec2(1, 1), m_mouse_pos + Vec2(1, 1), Color(0, 0, 0, 0xff), 1);
		}

		if (!m_go_to_parameters.m_is_active) return;

		float t = easeInOut(m_go_to_parameters.m_t);
		m_go_to_parameters.m_t += m_editor.getEngine().getLastTimeDelta() * m_go_to_parameters.m_speed;
		DVec3 pos = m_go_to_parameters.m_from * (1 - t) + m_go_to_parameters.m_to * t;
		Quat rot;
		rot = nlerp(m_go_to_parameters.m_from_rot, m_go_to_parameters.m_to_rot, t);
		if (m_go_to_parameters.m_t >= 1)
		{
			pos = m_go_to_parameters.m_to;
			m_go_to_parameters.m_is_active = false;
		}
		m_viewport.pos = pos;
		m_viewport.rot = rot;
	}

	RayHit getCameraRaycastHit(int cam_x, int cam_y, EntityPtr ignore) override
	{
		RayHit res;
		const Vec2 center{float(cam_x), float(cam_y)};

		const Ray ray = m_viewport.getRay(center);
		const RayCastModelHit hit = m_module->castRay(ray, ignore);
		DVec3 pos;
		if (hit.is_hit) {
			res.pos = ray.origin + ray.dir * hit.t;
			res.t = hit.t;
			res.entity = hit.entity;
			res.is_hit = true;
			return res;
		}
		res.pos = m_viewport.pos + m_viewport.rot.rotate(Vec3(0, 0, -2));
		res.t = 2;
		res.entity = INVALID_ENTITY;
		res.is_hit = false;
		return res;
	}
	
	void addText2D(float x, float y, Color color, const char* text) override {
		if (m_font) m_scene_view.m_pipeline->getDraw2D().addText(*m_font, {x, y}, color, text);
	}

	Vertex* render(bool lines, u32 vertex_count) override {
		m_draw_vertices.resize(m_draw_vertices.size() + vertex_count);
		DrawCmd& cmd = m_draw_cmds.emplace();
		cmd.lines = lines;
		cmd.vertex_count = vertex_count;
		return m_draw_vertices.end() - vertex_count;
	}

	struct DrawCmd {
		bool lines;
		u32 vertex_count;
	};

	struct {
		bool m_is_active = false;
		DVec3 m_from;
		DVec3 m_to;
		Quat m_from_rot;
		Quat m_to_rot;
		float m_t;
		float m_speed;
	} m_go_to_parameters;

	StudioApp& m_app;
	WorldEditor& m_editor;
	SceneView& m_scene_view;
	Viewport m_viewport;
	FontResource* m_font_res;
	Font* m_font;

	MouseMode m_mouse_mode = MouseMode::NONE;
	SnapMode m_snap_mode = SnapMode::NONE;
	Vec2 m_mouse_pos;
	Vec2 m_mouse_sensitivity{200, 200};
	bool m_is_mouse_down[(int)os::MouseButton::EXTENDED] = {};
	bool m_is_mouse_click[(int)os::MouseButton::EXTENDED] = {};
	StudioApp::MousePlugin* m_mouse_handling_plugin = nullptr;
	Vec2 m_rect_selection_start;
	float m_rect_selection_timer = 0;
	UniquePtr<EditorIcons> m_icons;
	RenderModule* m_module;
	Array<Vertex> m_draw_vertices;
	Array<DrawCmd> m_draw_cmds;
};

DVec3 SceneView::getViewportPosition() {
	return m_view->getViewport().pos;
}

void SceneView::setViewportPosition(const DVec3& pos) {
	Viewport vp = m_view->getViewport();
	vp.pos = pos;
	m_view->setViewport(vp);
}

Quat SceneView::getViewportRotation() {
	return m_view->getViewport().rot;
}

void SceneView::setViewportRotation(const Quat& rot) {
	Viewport vp = m_view->getViewport();
	vp.rot = rot;
	m_view->setViewport(vp);
}

static volatile bool once = [](){
	LUMIX_GLOBAL_FUNC(SceneView::getViewportRotation);
	LUMIX_GLOBAL_FUNC(SceneView::setViewportRotation);
	LUMIX_GLOBAL_FUNC(SceneView::getViewportPosition);
	LUMIX_GLOBAL_FUNC(SceneView::setViewportPosition);
	return true;
}();

SceneView::SceneView(StudioApp& app)
	: m_app(app)
	, m_log_ui(app.getLogUI())
	, m_editor(m_app.getWorldEditor())
{
	m_camera_speed = 0.1f;
	m_is_mouse_captured = false;

	m_copy_move_action.init("Duplicate move", "Duplicate entity when moving with gizmo", "duplicateEntityMove", "", Action::LOCAL);
	m_toggle_gizmo_step_action.init("Enable/disable gizmo step", "Enable/disable gizmo step", "toggleGizmoStep", "", Action::LOCAL);
	m_set_pivot_action.init("Set custom pivot", "Set custom pivot", "set_custom_pivot", "", os::Keycode::K, Action::Modifiers::NONE, Action::IMGUI_PRIORITY);
	m_reset_pivot_action.init("Reset pivot", "Reset pivot", "reset_pivot", "", os::Keycode::K, Action::Modifiers::SHIFT, Action::IMGUI_PRIORITY);
	m_insert_model_action.init("Insert model", "Insert model or prefab", "insert_model", ICON_FA_SEARCH, (os::Keycode)'P', Action::Modifiers::CTRL, Action::IMGUI_PRIORITY);

	m_top_view_action.init("Top", "Set top camera view", "viewTop", "", Action::IMGUI_PRIORITY);
	m_side_view_action.init("Side", "Set side camera view", "viewSide", "", Action::IMGUI_PRIORITY);
	m_front_view_action.init("Front", "Set front camera view", "viewFront", "", Action::IMGUI_PRIORITY);
	m_toggle_projection_action.init("Ortho/perspective", "Toggle ortho/perspective projection", "toggleProjection", "", Action::IMGUI_PRIORITY);
	m_look_at_selected_action.init("Look at selected", "Look at selected entity", "lookAtSelected", "", Action::IMGUI_PRIORITY);
	m_copy_view_action.init("Copy view transform", "Copy view transform", "copyViewTransform", "", Action::IMGUI_PRIORITY);
	m_rotate_entity_90_action.init("Rotate 90 degrees", "Rotate selected entities by 90 degrees", "rotate90deg", "", os::Keycode::R, Action::Modifiers::NONE, Action::IMGUI_PRIORITY);
	m_move_entity_E_action.init("Move entity east", "Move selected entity east", "moveEntityE", "", os::Keycode::RIGHT, Action::Modifiers::CTRL, Action::IMGUI_PRIORITY);
	m_move_entity_N_action.init("Move entity north", "Move selected entity north", "moveEntityN", "", os::Keycode::UP, Action::Modifiers::CTRL, Action::IMGUI_PRIORITY);
	m_move_entity_S_action.init("Move entity south", "Move selected entity south", "moveEntityS", "", os::Keycode::DOWN, Action::Modifiers::CTRL, Action::IMGUI_PRIORITY);
	m_move_entity_W_action.init("Move entity west", "Move selected entity west", "moveEntityW", "", os::Keycode::LEFT, Action::Modifiers::CTRL, Action::IMGUI_PRIORITY);

	m_app.addAction(&m_copy_move_action);
	m_app.addAction(&m_toggle_gizmo_step_action);
	m_app.addAction(&m_insert_model_action);
	m_app.addAction(&m_set_pivot_action);
	m_app.addAction(&m_reset_pivot_action);
	m_app.addAction(&m_top_view_action);
	m_app.addAction(&m_side_view_action);
	m_app.addAction(&m_front_view_action);
	m_app.addAction(&m_look_at_selected_action);
	m_app.addAction(&m_copy_view_action);
	m_app.addAction(&m_rotate_entity_90_action);
	m_app.addAction(&m_move_entity_E_action);
	m_app.addAction(&m_move_entity_N_action);
	m_app.addAction(&m_move_entity_S_action);
	m_app.addAction(&m_move_entity_W_action);
	m_app.addAction(&m_toggle_projection_action);
}

void SceneView::toggleProjection() {
	Viewport vp = m_view->getViewport(); 
	vp.is_ortho = !vp.is_ortho;
	m_view->setViewport(vp); 
}

void SceneView::init() {
	m_view = LUMIX_NEW(m_app.getAllocator(), WorldViewImpl)(*this);
	m_editor.setView(m_view);

	Engine& engine = m_app.getEngine();
	auto* renderer = static_cast<Renderer*>(engine.getSystemManager().getSystem("renderer"));
	LuaScript* pres = engine.getResourceManager().load<LuaScript>(Path("pipelines/main.lua"));
	m_pipeline = Pipeline::create(*renderer, pres, "SCENE_VIEW");
	m_pipeline->addCustomCommandHandler("renderSelection").callback.bind<&SceneView::renderSelection>(this);
	m_pipeline->addCustomCommandHandler("renderGizmos").callback.bind<&SceneView::renderGizmos>(this);
	m_pipeline->addCustomCommandHandler("renderIcons").callback.bind<&SceneView::renderIcons>(this);

	ResourceManagerHub& rm = engine.getResourceManager();
	m_debug_shape_shader = rm.load<Shader>(Path("pipelines/debug_shape.shd"));

	lua_State* L = m_app.getEngine().getState();
	lua_getglobal(L, "Editor");
	LuaWrapper::pushObject(L, this, "SceneView");
	lua_setfield(L, -2, "scene_view");
	lua_pop(L, 1);
}


SceneView::~SceneView()
{
	m_app.removeAction(&m_copy_move_action);
	m_app.removeAction(&m_toggle_gizmo_step_action);
	m_app.removeAction(&m_insert_model_action);
	m_app.removeAction(&m_set_pivot_action);
	m_app.removeAction(&m_reset_pivot_action);
	m_app.removeAction(&m_top_view_action);
	m_app.removeAction(&m_side_view_action);
	m_app.removeAction(&m_front_view_action);
	m_app.removeAction(&m_look_at_selected_action);
	m_app.removeAction(&m_copy_view_action);
	m_app.removeAction(&m_rotate_entity_90_action);
	m_app.removeAction(&m_move_entity_E_action);
	m_app.removeAction(&m_move_entity_N_action);
	m_app.removeAction(&m_move_entity_S_action);
	m_app.removeAction(&m_move_entity_W_action);
	m_app.removeAction(&m_toggle_projection_action);
	m_editor.setView(nullptr);
	LUMIX_DELETE(m_app.getAllocator(), m_view);
	m_debug_shape_shader->decRefCount();
}

void SceneView::rotate90Degrees() {
	const Array<EntityRef>& selected_entities = m_app.getWorldEditor().getSelectedEntities();
	WorldEditor& editor = m_app.getWorldEditor();
	World& world = *editor.getWorld();
	editor.beginCommandGroup("rot90deg");
	for (EntityRef e : selected_entities) {
		Quat rot = world.getRotation(e);
		float yaw = rot.toEuler().y;
		yaw += PI * 0.5f; // next turn
		yaw += 2 * PI; // make yaw positive if it were negative
		yaw += PI * 0.25f; // for correct "rounding"
		yaw -= fmodf(yaw, PI * 0.5f); // round
		rot.fromEuler({0, yaw, 0});
		editor.setEntitiesRotations(&e, &rot, 1);
	}
	editor.endCommandGroup();
}

void SceneView::moveEntity(Vec2 v) {
	const Array<EntityRef>& selected_entities = m_app.getWorldEditor().getSelectedEntities();
	Vec3 V = m_view->m_viewport.rot * Vec3(0, 0, -1);  
	if (fabsf(V.x) > fabsf(V.z)) {
		V = {signum(V.x), 0, 0};
	}
	else {
		V = {0, 0, signum(V.z)};
	}

	float step = m_app.getGizmoConfig().getStep(Gizmo::Config::TRANSLATE);
	if (step <= 0) step = 1;
	V *= step;

	Vec3 S(V.z, 0, -V.x);

	WorldEditor& editor = m_app.getWorldEditor();
	World& world = *editor.getWorld();
	editor.beginCommandGroup("rot90deg");
	for (EntityRef e : selected_entities) {
		DVec3 pos = world.getPosition(e);
		pos += V * v.y + S * v.x;
		// round position to multiple of step
		pos += Vec3(step * 0.5f);
		pos.x = floor(pos.x / step) * step;
		pos.y = floor(pos.y / step) * step;
		pos.z = floor(pos.z / step) * step;
		editor.setEntitiesPositions(&e, &pos, 1);
	}
	editor.endCommandGroup();
}

bool SceneView::onAction(const Action& action) {
	if (&action == &m_insert_model_action) m_insert_model_request = true;
	else if (&action == &m_set_pivot_action) m_view->setCustomPivot();
	else if (&action == &m_reset_pivot_action) m_view->resetPivot();
	else if (&action == &m_top_view_action) m_view->setTopView();
	else if (&action == &m_side_view_action) m_view->setSideView();
	else if (&action == &m_front_view_action) m_view->setFrontView();
	else if (&action == &m_toggle_projection_action) toggleProjection();
	else if (&action == &m_look_at_selected_action) m_view->lookAtSelected();
	else if (&action == &m_copy_view_action) m_view->copyTransform();
	else if (&action == &m_rotate_entity_90_action) rotate90Degrees();
	else if (&action == &m_move_entity_E_action) moveEntity(Vec2(-1, 0));
	else if (&action == &m_move_entity_N_action) moveEntity(Vec2(0, 1));
	else if (&action == &m_move_entity_S_action) moveEntity(Vec2(0, -1));
	else if (&action == &m_move_entity_W_action) moveEntity(Vec2(1, 0));
	else return false;
	return true;
}

void SceneView::manipulate() {
	PROFILE_FUNCTION();
	const Array<EntityRef>* selected = &m_editor.getSelectedEntities();
	if (selected->empty()) return;

	const bool is_snap = m_toggle_gizmo_step_action.isActive();
	Gizmo::Config& cfg = m_app.getGizmoConfig();
	cfg.enableStep(is_snap);
		
	Transform tr = m_editor.getWorld()->getTransform((*selected)[0]);
	tr.pos += tr.rot.rotate(cfg.getOffset());
	const Transform old_pivot_tr = tr;
			
	const bool copy_move = m_copy_move_action.isActive();
	if (!copy_move || !m_view->m_is_mouse_down[0]) {
		m_copy_moved = false;
	}

	if (!Gizmo::manipulate((*selected)[0].index, *m_view, tr, cfg)) return;

	if (copy_move && !m_copy_moved) {
		m_editor.duplicateEntities();
		selected = &m_editor.getSelectedEntities();
		Gizmo::setDragged((*selected)[0].index);
		m_copy_moved = true;
	}

	const Transform new_pivot_tr = tr;
	tr.pos -= tr.rot.rotate(cfg.getOffset());
	World& world = *m_editor.getWorld();
	switch (cfg.mode) {
		case Gizmo::Config::TRANSLATE: {
			const DVec3 diff = new_pivot_tr.pos - old_pivot_tr.pos;
			Array<DVec3> positions(m_app.getAllocator());
			positions.resize(selected->size());
			for (u32 i = 0, c = selected->size(); i < c; ++i) {
				positions[i] = world.getPosition((*selected)[i]) + diff;
			}
			m_editor.setEntitiesPositions(selected->begin(), positions.begin(), positions.size());
			break;
		}
		case Gizmo::Config::ROTATE: {
			Array<DVec3> poss(m_app.getAllocator());
			Array<Quat> rots(m_app.getAllocator());
			rots.resize(selected->size());
			poss.resize(selected->size());
			for (u32 i = 0, c = selected->size(); i < c; ++i) {
				const Transform t = new_pivot_tr * old_pivot_tr.inverted() * world.getTransform((*selected)[i]);
				poss[i] = t.pos;
				rots[i] = normalize(t.rot);
			}
			m_editor.setEntitiesPositionsAndRotations(selected->begin(), poss.begin(), rots.begin(), rots.size());
			break;
		}
		case Gizmo::Config::SCALE: {
			const Vec3 diff = new_pivot_tr.scale / old_pivot_tr.scale;
			Array<Vec3> scales(m_app.getAllocator());
			scales.resize(selected->size());
			for (u32 i = 0, c = selected->size(); i < c; ++i) {
				scales[i] = world.getScale((*selected)[i]) * diff;
			}
			m_editor.setEntitiesScales(selected->begin(), scales.begin(), scales.size());
			break;
		}
	}
	if (cfg.isAutosnapDown()) m_app.snapDown();
}

void SceneView::update(float time_delta)
{
	PROFILE_FUNCTION();
	m_pipeline->setWorld(m_editor.getWorld());
	ImGuiIO& io = ImGui::GetIO();
	if (!io.KeyShift) {
		m_view->setSnapMode(false, false);
	}
	else {
		m_view->setSnapMode(io.KeyShift, io.KeyCtrl);
	}
	Settings& settings = m_app.getSettings();
	m_view->setMouseSensitivity(settings.m_mouse_sensitivity.x, settings.m_mouse_sensitivity.y);
	m_view->update(time_delta);

	if (m_is_measure_active) {
		m_view->addCross(m_measure_from, 0.3f, Color::BLUE);
		m_view->addCross(m_measure_to, 0.3f, Color::BLUE);
		addLine(*m_view, m_measure_from, m_measure_to, Color::BLUE);
	}

	manipulate();

	if (ImGui::IsAnyItemActive()) return;
	if (!m_is_mouse_captured) return;
	if (ImGui::GetIO().KeyCtrl) return;

	int screen_x = int(ImGui::GetIO().MousePos.x);
	int screen_y = int(ImGui::GetIO().MousePos.y);
	bool is_inside = screen_x >= m_screen_x && screen_y >= m_screen_y && screen_x <= m_screen_x + m_width &&
					 screen_y <= m_screen_y + m_height;
	if (!is_inside) return;

	m_camera_speed = maximum(0.01f, m_camera_speed + ImGui::GetIO().MouseWheel / 20.0f);

	float speed = m_camera_speed * time_delta * 60.f;
	if (ImGui::GetIO().KeyShift) speed *= 10;
	const CommonActions& actions = m_app.getCommonActions();
	if (actions.cam_forward.isActive()) m_view->moveCamera(1.0f, 0, 0, speed);
	if (actions.cam_backward.isActive()) m_view->moveCamera(-1.0f, 0, 0, speed);
	if (actions.cam_left.isActive()) m_view->moveCamera(0.0f, -1.0f, 0, speed);
	if (actions.cam_right.isActive()) m_view->moveCamera(0.0f, 1.0f, 0, speed);
	if (actions.cam_down.isActive()) m_view->moveCamera(0, 0, -1.0f, speed);
	if (actions.cam_up.isActive()) m_view->moveCamera(0, 0, 1.0f, speed);
}


void SceneView::renderIcons() {
	Renderer& renderer = m_pipeline->getRenderer();
	
	renderer.pushJob("icons", [this, &renderer](DrawStream& stream) {
		const Viewport& vp = m_view->getViewport();
		const Matrix camera_mtx({0, 0, 0}, vp.rot);

		EditorIcons* icon_manager = m_view->m_icons.get();
		icon_manager->computeScales();
		const HashMap<EntityRef, EditorIcons::Icon>& icons = icon_manager->getIcons();
		for (const EditorIcons::Icon& icon : icons) {
			const Model* model = icon_manager->getModel(icon.type);
			if (!model || !model->isReady()) continue;

			const Matrix mtx = icon_manager->getIconMatrix(icon, camera_mtx, vp.pos, vp.is_ortho, vp.ortho_size);
			const Renderer::TransientSlice ub = renderer.allocUniform(&mtx, sizeof(Matrix));
			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
		
			for (int i = 0; i <= model->getLODIndices()[0].to; ++i) {
				const Mesh& mesh = model->getMesh(i);
				const Material* material = mesh.material;
				stream.bind(0, material->m_bind_group);
				const gpu::StateFlags state = material->m_render_states | gpu::StateFlags::DEPTH_FN_GREATER | gpu::StateFlags::DEPTH_WRITE;
				gpu::ProgramHandle program = mesh.material->getShader()->getProgram(state, mesh.vertex_decl, material->getDefineMask(), mesh.semantics_defines);
				
				stream.useProgram(program);
				stream.bindIndexBuffer(mesh.index_buffer_handle);
				stream.bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
				stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				stream.drawIndexed(0, mesh.indices_count, mesh.index_type);
			}
		}
	});
}


void SceneView::renderSelection()
{
	PROFILE_FUNCTION();
	Engine& engine = m_app.getEngine();
	Renderer& renderer = m_pipeline->getRenderer();
	const Array<EntityRef>& entities = m_editor.getSelectedEntities();
	if (entities.size() > 5000) return;
	
	renderer.pushJob("selection", [&renderer, this, &engine, &entities](DrawStream& stream) {
		RenderModule* module = m_pipeline->getModule();
		const World& world = module->getWorld();
		const u32 skinned_define = 1 << renderer.getShaderDefineIdx("SKINNED");
		const u32 depth_define = 1 << renderer.getShaderDefineIdx("DEPTH");
		const DVec3 view_pos = m_view->getViewport().pos;
		Array<DualQuat> dq_pose(engine.getAllocator());
		for (EntityRef e : entities) {
			if (!module->getWorld().hasComponent(e, MODEL_INSTANCE_TYPE)) continue;

			const Model* model = module->getModelInstanceModel(e);
			if (!model || !model->isReady()) continue;

			const Pose* pose = module->lockPose(e);
			for (int i = 0; i <= model->getLODIndices()[0].to; ++i) {
				const Mesh& mesh = model->getMesh(i);
					
				Material* material = mesh.material;
				u32 define_mask = material->getDefineMask() | depth_define;
				const Matrix mtx = world.getRelativeMatrix(e, view_pos);
				dq_pose.clear();
				if (pose && pose->count > 0 && mesh.type == Mesh::SKINNED) {
					define_mask |= skinned_define;
					dq_pose.reserve(pose->count);
					for (int j = 0, c = pose->count; j < c; ++j) {
						const Model::Bone& bone = model->getBone(j);
						const LocalRigidTransform tmp = {pose->positions[j], pose->rotations[j]};
						dq_pose.push((tmp * bone.inv_bind_transform).toDualQuat());
					}
				}
			
				Renderer::TransientSlice ub;
				if (dq_pose.empty()) {
					ub = renderer.allocUniform(&mtx, sizeof(mtx));
				}
				else {
					struct UBPrefix {
						float layer;
						float fur_scale;
						float gravity;
						float padding;
						Matrix model_mtx;
						// DualQuat bones[];
					};

					ub = renderer.allocUniform(sizeof(UBPrefix) + dq_pose.byte_size());
					UBPrefix* dc = (UBPrefix*)ub.ptr;
					dc->layer = 0;
					dc->fur_scale = 0;
					dc->gravity = 0;
					dc->model_mtx = mtx;
					memcpy(ub.ptr + sizeof(UBPrefix), dq_pose.begin(), dq_pose.byte_size());
				}
		
				const gpu::StateFlags state = gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_FUNCTION;
				gpu::ProgramHandle program = mesh.material->getShader()->getProgram(material->m_render_states | state, mesh.vertex_decl, define_mask, mesh.semantics_defines);
				stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
				stream.bind(0, material->m_bind_group);
				stream.useProgram(program);
				stream.bindIndexBuffer(mesh.index_buffer_handle);
				stream.bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
				stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				stream.drawIndexed(0, mesh.indices_count, mesh.index_type);
			}
			module->unlockPose(e, false);
		}
	});
}


void SceneView::renderGizmos()
{
	if (!m_debug_shape_shader || !m_debug_shape_shader->isReady()) return;
	auto& vertices = m_view->m_draw_vertices;
	if (vertices.empty()) return;

	Renderer& renderer = m_pipeline->getRenderer();
	renderer.pushJob("gizmos", [&renderer, &vertices, this](DrawStream& stream){
		Renderer::TransientSlice vb = renderer.allocTransient(vertices.byte_size());
		memcpy(vb.ptr, vertices.begin(), vertices.byte_size());
		const Renderer::TransientSlice ub = renderer.allocUniform(&Matrix::IDENTITY.columns[0].x, sizeof(Matrix));

		gpu::VertexDecl lines_decl(gpu::PrimitiveType::LINES);
		lines_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, 0);
		lines_decl.addAttribute(12, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);

		gpu::VertexDecl tris_decl(gpu::PrimitiveType::TRIANGLES);
		tris_decl.addAttribute(0, 3, gpu::AttributeType::FLOAT, 0);
		tris_decl.addAttribute(12, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);

		const gpu::StateFlags state = gpu::StateFlags::DEPTH_FN_GREATER | gpu::StateFlags::DEPTH_WRITE;
		gpu::ProgramHandle lines_program = m_debug_shape_shader->getProgram(state, lines_decl, 0, "");
		gpu::ProgramHandle triangles_program = m_debug_shape_shader->getProgram(state, tris_decl, 0, "");
	
		u32 offset = 0;
		stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
		for (const WorldViewImpl::DrawCmd& cmd : m_view->m_draw_cmds) {
			const gpu::ProgramHandle program = cmd.lines ? lines_program : triangles_program;
			stream.useProgram(program);
			stream.bindIndexBuffer(gpu::INVALID_BUFFER);
			stream.bindVertexBuffer(0, vb.buffer, vb.offset + offset, sizeof(WorldView::Vertex));
			stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
			stream.drawArrays(0, cmd.vertex_count);

			offset += cmd.vertex_count * sizeof(WorldView::Vertex);
		}

		m_view->m_draw_cmds.clear();
	});
}


void SceneView::captureMouse(bool capture) {
	if (m_is_mouse_captured == capture) return;
	m_is_mouse_captured = capture;
	os::showCursor(!capture);
	if (capture) {
		m_app.clipMouseCursor();
		const os::Point p = os::getMouseScreenPos();
		m_captured_mouse_x = p.x;
		m_captured_mouse_y = p.y;
	}
	else {
		m_app.unclipMouseCursor();
		os::setMouseScreenPos(m_captured_mouse_x, m_captured_mouse_y);
	}
}


RayCastModelHit SceneView::castRay(float x, float y)
{
	auto* module =  m_pipeline->getModule();
	ASSERT(module);
	
	const Viewport& vp = m_view->getViewport();
	const Ray ray = vp.getRay({x * vp.w, y * vp.h});
	return module->castRay(ray, INVALID_ENTITY);
}


void SceneView::handleDrop(const char* path, float x, float y)
{
	const RayCastModelHit hit = castRay(x, y);

	if (Path::hasExtension(path, "par")) {
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 5) * hit.dir;
		
		m_editor.beginCommandGroup("insert_particle");
		EntityRef entity = m_editor.addEntity();
		m_editor.setEntitiesPositions(&entity, &pos, 1);
		m_editor.addComponent(Span(&entity, 1), PARTICLE_EMITTER_TYPE);
		m_editor.setProperty(PARTICLE_EMITTER_TYPE, "", -1, "Source", Span(&entity, 1), Path(path));
		m_editor.endCommandGroup();
		m_editor.selectEntities(Span(&entity, 1), false);
	}
	else if (Path::hasExtension(path, "fbx"))
	{
		if (findInsensitive(path, ".phy:"))
		{
			if (hit.is_hit && hit.entity.isValid())
			{
				m_editor.beginCommandGroup("insert_phy_component");
				const EntityRef e = (EntityRef)hit.entity;
				m_editor.selectEntities(Span(&e, 1), false);
				m_editor.addComponent(Span(&e, 1), MESH_ACTOR_TYPE);
				m_editor.setProperty(MESH_ACTOR_TYPE, "", -1, "Mesh", Span(&e, 1), Path(path));
				m_editor.endCommandGroup();
			}
			else
			{
				const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;
				m_editor.beginCommandGroup("insert_phy");
				EntityRef entity = m_editor.addEntity();
				m_editor.setEntitiesPositions(&entity, &pos, 1);
				m_editor.selectEntities(Span(&entity, 1), false);
				m_editor.addComponent(Span(&entity, 1), MESH_ACTOR_TYPE);
				m_editor.setProperty(MESH_ACTOR_TYPE, "", -1, "Mesh", Span(&entity, 1), Path(path));
				m_editor.endCommandGroup();
			}
		}
		else {
			const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 5) * hit.dir;

			m_editor.beginCommandGroup("insert_mesh");
			EntityRef entity = m_editor.addEntity();
			m_editor.selectEntities(Span(&entity, 1), false);
			m_editor.setEntitiesPositions(&entity, &pos, 1);
			m_editor.addComponent(Span(&entity, 1), MODEL_INSTANCE_TYPE);
			m_editor.setProperty(MODEL_INSTANCE_TYPE, "", -1, "Source", Span(&entity, 1), Path(path));
			m_editor.endCommandGroup();
		}
	}
	else if (Path::hasExtension(path, "fab"))
	{
		ResourceManagerHub& manager = m_editor.getEngine().getResourceManager();
		PrefabResource* prefab = manager.load<PrefabResource>(Path(path));
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;
		if (prefab->isEmpty()) {
			FileSystem& fs = m_editor.getEngine().getFileSystem();
			while (fs.hasWork()) fs.processCallbacks();
		}
		if (prefab->isReady()) {
			EntityPtr eptr = m_editor.getPrefabSystem().instantiatePrefab(*prefab, pos, Quat::IDENTITY, Vec3(1));
			if (eptr.isValid()) {
				EntityRef e = *eptr;
				m_editor.selectEntities(Span(&e, 1), false);
			}
		}
		else {
			ASSERT(prefab->isFailure());
			logError("Failed to load ", prefab->getPath());
		}
	}
}


void SceneView::onToolbar()
{
	static const char* actions_names[] = { "setTranslateGizmoMode",
		"setRotateGizmoMode",
		"setScaleGizmoMode",
		"setLocalCoordSystem",
		"setGlobalCoordSystem",
		"viewTop",
		"viewFront",
		"viewSide" };

	auto pos = ImGui::GetCursorScreenPos();
	const float toolbar_height = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2;
	if (ImGuiEx::BeginToolbar("scene_view_toolbar", pos, ImVec2(0, toolbar_height)))
	{
		for (auto* action_name : actions_names)
		{
			auto* action = m_app.getAction(action_name);
			action->toolbarButton(m_app.getBigIconFont());
		}
	}

	ImGui::SameLine();
	const ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_Text];
	const ImVec4 col_active = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
	
	bool open_camera_transform = false;
	if(ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_CAMERA, bg_color, "Camera details")) {
		open_camera_transform = true;
	}

	ImGui::PushItemWidth(50);
	float offset = (toolbar_height - ImGui::GetTextLineHeightWithSpacing()) / 2;
	
	Action* mode_action;
	if (m_app.getGizmoConfig().isTranslateMode())
	{
		mode_action = m_app.getAction("setTranslateGizmoMode");
	}
	else
	{
		mode_action = m_app.getAction("setRotateGizmoMode");
	}
	
	ImGui::SameLine();
	const ImVec4 measure_icon_color = m_is_measure_active ? col_active : ImGui::GetStyle().Colors[ImGuiCol_Text];
	if (ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_RULER_VERTICAL, measure_icon_color, "Measure")) {
		m_is_measure_active = !m_is_measure_active;
	}

	ImGui::SameLine();
	pos = ImGui::GetCursorPos();
	//pos.y -= offset;
	ImGui::SetCursorPos(pos);
	ImGui::TextUnformatted(mode_action->font_icon);

	ImGui::SameLine();
	pos = ImGui::GetCursorPos();
	pos.y += offset;
	ImGui::SetCursorPos(pos);
	float step = m_app.getGizmoConfig().getStep();
	if (ImGui::DragFloat("##gizmoStep", &step, 1.0f, 0, 200))
	{
		m_app.getGizmoConfig().setStep(step);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Snap amount");

	ImGui::SameLine();
	m_pipeline->callLuaFunction("onGUI");

	ImGui::PopItemWidth();
	ImGuiEx::EndToolbar();

	if (open_camera_transform) ImGui::OpenPopup("Camera details");

	if (ImGui::BeginPopup("Camera details")) {
		ImGui::DragFloat("Speed", &m_camera_speed, 0.1f, 0.01f, 999.0f, "%.2f");
		Viewport vp = m_view->getViewport();
		if (ImGui::Checkbox("Ortho", &vp.is_ortho)) m_view->setViewport(vp);
		if (vp.is_ortho && ImGui::DragFloat("Ortho size", &vp.ortho_size)) {
			m_view->setViewport(vp);
		}
		if (ImGui::DragScalarN("Position", ImGuiDataType_Double, &vp.pos.x, 3, 1.f)) {
			m_view->setViewport(vp);
		}
		if (inputRotation("Rotation", &vp.rot)) {
			m_view->setViewport(vp);
		}
		ImGui::Selectable(ICON_FA_TIMES " Close");
		ImGui::EndPopup();
	}
}

void SceneView::handleEvents() {
	const bool handle_input = m_is_mouse_captured || ImGui::IsItemHovered();
	for (const os::Event event : m_app.getEvents()) {
		switch (event.type) {
			case os::Event::Type::KEY: {
				if (handle_input) {
					if (event.key.down && event.key.keycode == os::Keycode::ESCAPE) {
						m_editor.selectEntities(Span((const EntityRef*)nullptr, (u32)0), false);
					}
				}
				break;
			}
			case os::Event::Type::MOUSE_WHEEL: {
				if (handle_input) {
					m_view->onMouseWheel(event.mouse_wheel.amount);
				}
				break;
			}
			case os::Event::Type::MOUSE_BUTTON: {
				const os::Point cp = os::getMouseScreenPos();
				Vec2 rel_mp = { (float)cp.x, (float)cp.y };
				rel_mp.x -= m_screen_x;
				rel_mp.y -= m_screen_y;
				if (handle_input) {
					if (event.mouse_button.button == os::MouseButton::RIGHT) {
						ImGui::SetWindowFocus();
						captureMouse(event.mouse_button.down);
					}
					ImGuiEx::ResetActiveID();
					if (event.mouse_button.down) {
						m_view->onMouseDown((int)rel_mp.x, (int)rel_mp.y, event.mouse_button.button);
					}
					else {
						m_view->onMouseUp((int)rel_mp.x, (int)rel_mp.y, event.mouse_button.button);
					}
				}
				else if (!event.mouse_button.down) {
					m_view->onMouseUp((int)rel_mp.x, (int)rel_mp.y, event.mouse_button.button);
				}
				break;
			}
			case os::Event::Type::MOUSE_MOVE: 
				if (handle_input) {
					const os::Point cp = os::getMouseScreenPos();
					Vec2 rel_mp = {(float)cp.x, (float)cp.y};
					rel_mp.x -= m_screen_x;
					rel_mp.y -= m_screen_y;
					m_view->onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)event.mouse_move.xrel, (int)event.mouse_move.yrel);
				}
				break;
			default: break;
		}
	}
}

void SceneView::onSettingsLoaded() {
	Settings& settings = m_app.getSettings();
	m_search_preview = settings.getValue(Settings::GLOBAL, "quicksearch_preview", false);
}

void SceneView::onBeforeSettingsSaved() {
	Settings& settings = m_app.getSettings();
	settings.setValue(Settings::GLOBAL, "quicksearch_preview", m_search_preview);
}

void SceneView::insertModelUI() {
	if (m_insert_model_request) ImGui::OpenPopup("Insert model");

	if (ImGuiEx::BeginResizablePopup("Insert model", ImVec2(300, 200))) {
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

		ImGui::AlignTextToFramePadding();
		if (ImGuiEx::IconButton(ICON_FA_COG, "Settings")) ImGui::OpenPopup("settings_popup");
		if (ImGui::BeginPopup("settings_popup")) {
			ImGui::Checkbox("Preview", &m_search_preview);
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		if(m_insert_model_request) m_search_selected = 0;
		if (m_filter.gui(ICON_FA_SEARCH " Search", -1, m_insert_model_request)) {
			m_search_selected = 0;
		}
		bool scroll = false;
		const bool insert_enter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
		if (ImGui::IsItemFocused()) {
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_search_selected > 0) {
				--m_search_selected;
				scroll =  true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				++m_search_selected;
				scroll =  true;
			}
		}
		if (m_filter.isActive()) {
			if (ImGui::BeginChild("##list")) {
				auto insert = [&](const Path& path){
					const RayCastModelHit hit = castRay(0.5f, 0.5f);
					const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 5) * hit.dir;

					m_editor.beginCommandGroup("insert_mesh");
					EntityRef entity = m_editor.addEntity();
					m_editor.setEntitiesPositions(&entity, &pos, 1);
					m_editor.addComponent(Span(&entity, 1), MODEL_INSTANCE_TYPE);
					m_editor.setProperty(MODEL_INSTANCE_TYPE, "", -1, "Source", Span(&entity, 1), path);
					m_editor.endCommandGroup();
					m_editor.selectEntities(Span(&entity, 1), false);

					ImGui::CloseCurrentPopup();
				};
				AssetBrowser& ab = m_app.getAssetBrowser();
				const auto& resources = m_app.getAssetCompiler().lockResources();
				u32 idx = 0;
				for (const auto& res : resources) {
					if (res.type != Model::TYPE) continue;
					if (!m_filter.pass(res.path)) continue;

					const bool selected = idx == m_search_selected;
					if (m_search_preview) {
						ImGui::SameLine();
						if (idx == 0 || ImGui::GetContentRegionAvail().x < 50) ImGui::NewLine();
						ab.tile(res.path, selected);
						if (ImGui::IsItemClicked() || (insert_enter && selected)) {
							insert(res.path);
							break;
						}
					}
					else {
						if (ImGui::Selectable(res.path.c_str(), selected) || (insert_enter && selected)) {
							insert(res.path);
							break;
						}
					}
					if (selected && scroll) {
						ImGui::SetScrollHereY();
					}
					++idx;
				}
				m_search_selected = idx > 0 ? minimum(m_search_selected, idx - 1) : 0;

				m_app.getAssetCompiler().unlockResources();
			}
			ImGui::EndChild();
		}
		ImGui::EndPopup();
	}
	m_insert_model_request = false;
}

void SceneView::onGUI()
{
	if (m_is_mouse_captured && !m_app.isMouseCursorClipped()) captureMouse(false);

	m_has_focus = false;
	PROFILE_FUNCTION();
	m_pipeline->setWorld(m_editor.getWorld());

	bool is_open = false;
	ImVec2 view_pos;
	const char* title = ICON_FA_GLOBE "Scene View###Scene View";
	if (m_log_ui.getUnreadErrorCount() > 0) title = ICON_FA_GLOBE "Scene View | " ICON_FA_EXCLAMATION_TRIANGLE " errors in log###Scene View";

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImVec2 view_size;
	if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoScrollWithMouse)) {
		m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || m_has_focus;
		
		ImGui::PopStyleVar();
		is_open = true;
		ImGui::Dummy(ImVec2(2, 2));
		onToolbar();
		view_size = ImGui::GetContentRegionAvail();
		Viewport vp = m_view->getViewport();
		vp.w = (int)view_size.x;
		vp.h = (int)view_size.y;
		m_view->setViewport(vp);
		m_pipeline->setViewport(vp);
		m_pipeline->render(false);
		m_view->m_draw_vertices.clear();
		m_view->m_draw_cmds.clear();
		m_view->inputFrame();

		const gpu::TextureHandle texture_handle = m_pipeline->getOutput();
		if (view_size.x > 0 && view_size.y > 0) {
			const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
			m_screen_x = int(cursor_pos.x);
			m_screen_y = int(cursor_pos.y);
			m_width = int(view_size.x);
			m_height = int(view_size.y);
			view_pos = ImGui::GetCursorScreenPos();
			
			if (texture_handle) {
				void* t = texture_handle;
				if (gpu::isOriginBottomLeft()) {
					ImGui::Image(t, view_size, ImVec2(0, 1), ImVec2(1, 0));
				} 
				else {
					ImGui::Image(t, view_size);
				}
			}

			if (ImGui::BeginDragDropTarget()) {
				if (auto* payload = ImGui::AcceptDragDropPayload("path")) {
					const ImVec2 drop_pos = (ImGui::GetMousePos() - view_pos) / view_size;
					handleDrop((const char*)payload->Data, drop_pos.x, drop_pos.y);
				}
				ImGui::EndDragDropTarget();
			}

			handleEvents();
		}
	}
	else {
		ImGui::PopStyleVar();
		m_view->inputFrame();
		m_view->m_draw_vertices.clear();
		m_view->m_draw_cmds.clear();
	}

	ImGui::End();

	if (m_is_mouse_captured && is_open) {
		os::Rect rect;
		rect.left = (i32)view_pos.x;
		rect.top = (i32)view_pos.y;
		rect.width = (i32)view_size.x;
		rect.height = (i32)view_size.y;
		m_app.setMouseClipRect(ImGui::GetWindowViewport()->PlatformHandle, rect);
	}

	insertModelUI();

	if (is_open && m_is_measure_active) {
		view_pos.x += ImGui::GetStyle().FramePadding.x;
		view_pos.y += ImGui::GetStyle().FramePadding.y;
		ImGui::SetNextWindowPos(view_pos);
		auto col = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
		col.w = 0.3f;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, col);
		if (ImGui::Begin("###stats_overlay",
				nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
					ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
		{
			const double d = length(m_measure_to - m_measure_from);
			ImGui::Text("Measured distance: %f", d);
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
}


} // namespace Lumix
