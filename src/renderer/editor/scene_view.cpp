#include <imgui/imgui.h>

#include "scene_view.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/gizmo.h"
#include "editor/log_ui.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/crc32.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/path.h"
#include "engine/path.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/universe.h"
#include "renderer/culling_system.h"
#include "renderer/draw2d.h"
#include "renderer/font.h"
#include "renderer/gpu/gpu.h"
#include "renderer/editor/editor_icon.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"

namespace Lumix
{

static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType MESH_ACTOR_TYPE = Reflection::getComponentType("mesh_rigid_actor");

struct UniverseViewImpl final : UniverseView {
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

	UniverseViewImpl(SceneView& view) 
		: m_scene_view(view)
		, m_editor(view.m_editor) 
		, m_scene(nullptr)
		, m_draw_cmds(view.m_app.getAllocator())
		, m_draw_vertices(view.m_app.getAllocator())
	{
		m_editor.universeCreated().bind<&UniverseViewImpl::onUniverseCreated>(this);
		m_editor.universeDestroyed().bind<&UniverseViewImpl::onUniverseDestroyed>(this);
		m_viewport.is_ortho = false;
		m_viewport.pos = DVec3(0);
		m_viewport.rot.set(0, 0, 0, 1);
		m_viewport.w = -1;
		m_viewport.h = -1;
		m_viewport.fov = view.m_app.getFOV();
		m_viewport.near = 0.1f;
		m_viewport.far = 100000.f;

		ResourceManagerHub& rm = m_editor.getEngine().getResourceManager();
		Path font_path("editor/fonts/NotoSans-Regular.ttf");
		m_font_res = rm.load<FontResource>(font_path);
		m_font = m_font_res->addRef(16);
		onUniverseCreated();
	}

	~UniverseViewImpl() {
		m_font_res->getResourceManager().unload(*m_font_res);
		m_editor.universeCreated().unbind<&UniverseViewImpl::onUniverseCreated>(this);
		m_editor.universeDestroyed().unbind<&UniverseViewImpl::onUniverseDestroyed>(this);
		onUniverseDestroyed();
	}

	void addCross(const DVec3& pos, float size, Color color) {
		addLine(*this, pos - Vec3(size, 0, 0), pos + Vec3(size, 0, 0), color);
		addLine(*this, pos - Vec3(0, size, 0), pos + Vec3(0, size, 0), color);
		addLine(*this, pos - Vec3(0, 0, size), pos + Vec3(0, 0, size), color);
	}

	void onUniverseCreated(){
		m_scene = (RenderScene*)m_editor.getUniverse()->getScene(crc32("renderer"));
		m_icons = EditorIcons::create(m_editor, *m_scene);
	}

	void onUniverseDestroyed(){
		if (m_icons) EditorIcons::destroy(*m_icons);
		m_icons = nullptr;
	}

	void setSnapMode(bool enable, bool vertex_snap) override
	{
		m_snap_mode = enable ? (vertex_snap ? SnapMode::VERTEX : SnapMode::FREE) : SnapMode::NONE;
	}

	void previewSnapVertex()
	{
		if (m_snap_mode != SnapMode::VERTEX) return;

		DVec3 origin;
		Vec3 dir;
		m_viewport.getRay(m_mouse_pos, origin, dir);
		const RayCastModelHit hit = m_scene->castRay(origin, dir, INVALID_ENTITY);
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
			CullResult* renderables = m_scene->getRenderables(frustum, (RenderableTypes)i);
			CullResult* first = renderables;
			while (renderables) {
				m_editor.selectEntities(Span(renderables->entities, renderables->header.count), true);
				renderables = renderables->header.next;
			}
			if (first) first->free(m_editor.getEngine().getPageAllocator());
		}
	}

	void onMouseUp(int x, int y, OS::MouseButton button)
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
				DVec3 origin;
				Vec3 dir;
				m_viewport.getRay(m_mouse_pos, origin, dir);
				const RayCastModelHit hit = m_scene->castRay(origin, dir, INVALID_ENTITY);

				const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
				if (m_snap_mode != SnapMode::NONE && !selected_entities.empty() && hit.is_hit)
				{
					DVec3 snap_pos = origin + dir * hit.t;
					if (m_snap_mode == SnapMode::VERTEX) snap_pos = getClosestVertex(hit);
					const Quat rot = m_editor.getUniverse()->getRotation(selected_entities[0]);
					const Gizmo::Config& gizmo_cfg = m_scene_view.m_app.getGizmoConfig();
					const Vec3 offset = rot.rotate(gizmo_cfg.getOffset());
					m_editor.snapEntities(snap_pos - offset, gizmo_cfg.isTranslateMode());
				}
				else
				{
					auto icon_hit = m_icons->raycast(origin, dir);
					if (icon_hit.entity != INVALID_ENTITY)
					{
						if(icon_hit.entity.isValid()) {
							EntityRef e = (EntityRef)icon_hit.entity;
							m_editor.selectEntities(Span(&e, 1), ImGui::GetIO().KeyCtrl);
						}
					}
					else if (hit.is_hit)
					{
						if(hit.entity.isValid()) {
							EntityRef entity = (EntityRef)hit.entity;
							m_editor.selectEntities(Span(&entity, 1), ImGui::GetIO().KeyCtrl);
						}
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
		m_scene_view.m_app.getGizmoConfig().setOffset(Vec3::ZERO);
	}
	
	void setCustomPivot() override
	{
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.empty()) return;

		DVec3 origin;
		Vec3 dir;		
		m_viewport.getRay(m_mouse_pos, origin, dir);
		const RayCastModelHit hit = m_scene->castRay(origin, dir, INVALID_ENTITY);
		if (!hit.is_hit || hit.entity != selected_entities[0]) return;

		const DVec3 snap_pos = getClosestVertex(hit);

		const Transform tr = m_editor.getUniverse()->getTransform(selected_entities[0]);
		m_scene_view.m_app.getGizmoConfig().setOffset(tr.rot.conjugated() * (snap_pos - tr.pos).toFloat());
	}


	DVec3 getClosestVertex(const RayCastModelHit& hit) const {
		ASSERT(hit.entity.isValid());
		const EntityRef entity = (EntityRef)hit.entity;
		const DVec3& wpos = hit.origin + hit.t * hit.dir;
		Universe& universe = m_scene->getUniverse();
		const Transform tr = universe.getTransform(entity);
		const Vec3 lpos = tr.rot.conjugated() * (wpos - tr.pos).toFloat();
		if (!universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) return wpos;

		Model* model = m_scene->getModelInstanceModel(entity);

		float min_dist_squared = FLT_MAX;
		Vec3 closest_vertex = lpos;
		auto processVertex = [&](const Vec3& vertex) {
			float dist_squared = (vertex - lpos).squaredLength();
			if (dist_squared < min_dist_squared)
			{
				min_dist_squared = dist_squared;
				closest_vertex = vertex;
			}
		};

		for (int i = 0, c = model->getMeshCount(); i < c; ++i)
		{
			Mesh& mesh = model->getMesh(i);

			if (mesh.areIndices16())
			{
				const u16* indices = (const u16*)&mesh.indices[0];
				for (int i = 0, c = mesh.indices.size() >> 1; i < c; ++i)
				{
					Vec3 vertex = mesh.vertices[indices[i]];
					processVertex(vertex);
				}
			}
			else
			{
				const u32* indices = (const u32*)&mesh.indices[0];
				for (int i = 0, c = mesh.indices.size() >> 2; i < c; ++i)
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

	bool isMouseClick(OS::MouseButton button) const override { return m_is_mouse_click[(int)button]; }
	bool isMouseDown(OS::MouseButton button) const override { return m_is_mouse_down[(int)button]; }

	void onMouseMove(int x, int y, int relx, int rely)
	{
		PROFILE_FUNCTION();
		m_mouse_pos.set((float)x, (float)y);
		
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

	void onMouseDown(int x, int y, OS::MouseButton button)
	{
		m_is_mouse_click[(int)button] = true;
		m_is_mouse_down[(int)button] = true;
		if(button == OS::MouseButton::MIDDLE)
		{
			m_mouse_mode = MouseMode::PAN;
		}
		else if (button == OS::MouseButton::RIGHT)
		{
			m_mouse_mode = MouseMode::NAVIGATE;
		}
		else if (button == OS::MouseButton::LEFT)
		{
			DVec3 origin;
			Vec3 dir;
			m_viewport.getRay({(float)x, (float)y}, origin, dir);
			const RayCastModelHit hit = m_scene->castRay(origin, dir, INVALID_ENTITY);
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

			for (StudioApp::MousePlugin* plugin : m_scene_view.m_app.getMousePlugins()) {
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

	void copyTransform() override {
		if (m_editor.getSelectedEntities().empty()) return;

		m_editor.setEntitiesPositionsAndRotations(m_editor.getSelectedEntities().begin(), &m_viewport.pos, &m_viewport.rot, 1);
	}

	void lookAtSelected() override {
		const Universe* universe = m_editor.getUniverse();
		if (m_editor.getSelectedEntities().empty()) return;

		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		const Vec3 dir = m_viewport.rot.rotate(Vec3(0, 0, 1));
		m_go_to_parameters.m_to = universe->getPosition(m_editor.getSelectedEntities()[0]) + dir * 10;
		const double len = (m_go_to_parameters.m_to - m_go_to_parameters.m_from).length();
		m_go_to_parameters.m_speed = maximum(100.0f / (len > 0 ? float(len) : 1), 2.0f);
		m_go_to_parameters.m_from_rot = m_go_to_parameters.m_to_rot = m_viewport.rot;

	}
	
	void setTopView() override {
		m_go_to_parameters.m_is_active = true;
		m_go_to_parameters.m_t = 0;
		m_go_to_parameters.m_from = m_viewport.pos;
		m_go_to_parameters.m_to = m_go_to_parameters.m_from;
		const Array<EntityRef>& selected_entities = m_editor.getSelectedEntities();
		if (m_scene_view.m_orbit_action->isActive() && !selected_entities.empty()) {
			auto* universe = m_editor.getUniverse();
			m_go_to_parameters.m_to = universe->getPosition(selected_entities[0]) + Vec3(0, 10, 0);
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
		if (m_scene_view.m_orbit_action->isActive() && !selected_entities.empty()) {
			auto* universe = m_editor.getUniverse();
			m_go_to_parameters.m_to = universe->getPosition(selected_entities[0]) + Vec3(0, 0, -10);
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
		if (m_scene_view.m_orbit_action->isActive() && !selected_entities.empty()) {
			auto* universe = m_editor.getUniverse();
			m_go_to_parameters.m_to = universe->getPosition(selected_entities[0]) + Vec3(-10, 0, 0);
		}
		m_go_to_parameters.m_speed = 2.0f;
		m_go_to_parameters.m_from_rot = m_viewport.rot;
		m_go_to_parameters.m_to_rot = Quat(Vec3(0, 1, 0), -PI * 0.5f);
	}

	void moveCamera(float forward, float right, float up, float speed) override
	{
		const Quat rot = m_viewport.rot;

		right = m_scene_view.m_orbit_action->isActive() ? 0 : right;

		m_viewport.pos += rot.rotate(Vec3(0, 0, -1)) * forward * speed;
		m_viewport.pos += rot.rotate(Vec3(1, 0, 0)) * right * speed;
		m_viewport.pos += rot.rotate(Vec3(0, 1, 0)) * up * speed;
	}

	void rotateCamera(float yaw, float pitch) {
		const Universe* universe = m_editor.getUniverse();
		DVec3 pos = m_viewport.pos;
		Quat rot = m_viewport.rot;
		const Quat old_rot = rot;

		Quat yaw_rot(Vec3(0, 1, 0), yaw);
		rot = yaw_rot * rot;
		rot.normalize();

		Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
		const Quat pitch_rot(pitch_axis, pitch);
		rot = pitch_rot * rot;
		rot.normalize();

		if (m_scene_view.m_orbit_action->isActive() && !m_editor.getSelectedEntities().empty())
		{
			const Vec3 dir = rot.rotate(Vec3(0, 0, 1));
			const DVec3 entity_pos = universe->getPosition(m_editor.getSelectedEntities()[0]);
			const float dist = float((entity_pos - pos).length());
			pos = entity_pos + dir * dist;
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
		if (m_mouse_mode == MouseMode::SELECT) {
			m_rect_selection_timer += time_delta;
		}

		m_viewport.fov = m_scene_view.m_app.getFOV();
		previewSnapVertex();
		
		if (m_is_mouse_down[(int)OS::MouseButton::LEFT] && m_mouse_mode == MouseMode::SELECT) {
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

	RayHit getCameraRaycastHit(int cam_x, int cam_y) override
	{
		RayHit res;
		const Vec2 center{float(cam_x), float(cam_y)};

		DVec3 origin;
		Vec3 dir;
		m_viewport.getRay(center, origin, dir);
		const RayCastModelHit hit = m_scene->castRay(origin, dir, INVALID_ENTITY);
		DVec3 pos;
		if (hit.is_hit) {
			res.pos = origin + dir * hit.t;
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

	WorldEditor& m_editor;
	SceneView& m_scene_view;
	Viewport m_viewport;
	FontResource* m_font_res;
	Font* m_font;

	MouseMode m_mouse_mode = MouseMode::NONE;
	SnapMode m_snap_mode = SnapMode::NONE;
	Vec2 m_mouse_pos;
	Vec2 m_mouse_sensitivity{200, 200};
	bool m_is_mouse_down[(int)OS::MouseButton::EXTENDED] = {};
	bool m_is_mouse_click[(int)OS::MouseButton::EXTENDED] = {};
	StudioApp::MousePlugin* m_mouse_handling_plugin = nullptr;
	Vec2 m_rect_selection_start;
	float m_rect_selection_timer = 0;
	EditorIcons* m_icons = nullptr;
	RenderScene* m_scene;
	Array<Vertex> m_draw_vertices;
	Array<DrawCmd> m_draw_cmds;
};


SceneView::SceneView(StudioApp& app)
	: m_app(app)
	, m_log_ui(app.getLogUI())
	, m_editor(m_app.getWorldEditor())
{
	m_camera_speed = 0.1f;
	m_is_mouse_captured = false;
	m_show_stats = false;

	Engine& engine = m_editor.getEngine();
	IAllocator& allocator = engine.getAllocator();
	auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
	PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
	m_pipeline = Pipeline::create(*renderer, pres, "SCENE_VIEW", engine.getAllocator());
	m_pipeline->addCustomCommandHandler("renderSelection").callback.bind<&SceneView::renderSelection>(this);
	m_pipeline->addCustomCommandHandler("renderGizmos").callback.bind<&SceneView::renderGizmos>(this);
	m_pipeline->addCustomCommandHandler("renderIcons").callback.bind<&SceneView::renderIcons>(this);

	ResourceManagerHub& rm = engine.getResourceManager();
	m_debug_shape_shader = rm.load<Shader>(Path("pipelines/debug_shape.shd"));

	m_orbit_action = LUMIX_NEW(allocator, Action)("Orbit", "Orbit with RMB", "orbitRMB");
	m_orbit_action->is_global = false;
	m_app.addAction(m_orbit_action);

	m_toggle_gizmo_step_action = LUMIX_NEW(allocator, Action)("Enable/disable gizmo step", "Enable/disable gizmo step", "toggleGizmoStep");
	m_toggle_gizmo_step_action->is_global = false;
	m_app.addAction(m_toggle_gizmo_step_action);

	m_move_forward_action = LUMIX_NEW(allocator, Action)("Move forward", "Move camera forward", "moveForward");
	m_move_forward_action->is_global = false;
	m_app.addAction(m_move_forward_action);

	m_move_back_action = LUMIX_NEW(allocator, Action)("Move back", "Move camera back", "moveBack");
	m_move_back_action->is_global = false;
	m_app.addAction(m_move_back_action);

	m_move_left_action = LUMIX_NEW(allocator, Action)("Move left", "Move camera left", "moveLeft");
	m_move_left_action->is_global = false;
	m_app.addAction(m_move_left_action);

	m_move_right_action = LUMIX_NEW(allocator, Action)("Move right", "Move camera right", "moveRight");
	m_move_right_action->is_global = false;
	m_app.addAction(m_move_right_action);

	m_move_up_action = LUMIX_NEW(allocator, Action)("Move up", "Move camera up", "moveUp");
	m_move_up_action->is_global = false;
	m_app.addAction(m_move_up_action);

	m_move_down_action = LUMIX_NEW(allocator, Action)("Move down", "Move camera down", "moveDown");
	m_move_down_action->is_global = false;
	m_app.addAction(m_move_down_action);

	m_camera_speed_action = LUMIX_NEW(allocator, Action)(ICON_FA_CAMERA "Camera speed", "Reset camera speed", "cameraSpeed", ICON_FA_CAMERA);
	m_camera_speed_action->is_global = false;
	m_camera_speed_action->func.bind<&SceneView::resetCameraSpeed>(this);
	m_app.addAction(m_camera_speed_action);

	const ResourceType pipeline_type("pipeline");
	m_app.getAssetCompiler().registerExtension("pln", pipeline_type); 

	m_view = LUMIX_NEW(m_editor.getAllocator(), UniverseViewImpl)(*this);
	m_editor.setView(m_view);
}


void SceneView::resetCameraSpeed()
{
	m_camera_speed = 0.1f;
}


SceneView::~SceneView()
{
	m_editor.setView(nullptr);
	LUMIX_DELETE(m_app.getAllocator(), m_view);
	Pipeline::destroy(m_pipeline);
	m_debug_shape_shader->getResourceManager().unload(*m_debug_shape_shader);
	m_pipeline = nullptr;
}

void SceneView::manipulate() {
	const Array<EntityRef>& selected = m_editor.getSelectedEntities();
	if (selected.empty()) return;

	const bool is_snap = m_toggle_gizmo_step_action->isActive();
	Gizmo::Config& cfg = m_app.getGizmoConfig();
	cfg.enableStep(is_snap);
		
	Transform tr = m_editor.getUniverse()->getTransform(selected[0]);
	tr.pos += tr.rot.rotate(cfg.getOffset());
	const Transform old_pivot_tr = tr;
			
	if (!Gizmo::manipulate(selected[0].index, *m_view, Ref(tr), cfg)) return;

	const Transform new_pivot_tr = tr;
	tr.pos -= tr.rot.rotate(cfg.getOffset());
	Universe& universe = *m_editor.getUniverse();
	switch (cfg.mode) {
		case Gizmo::Config::TRANSLATE: {
			const DVec3 diff = new_pivot_tr.pos - old_pivot_tr.pos;
			Array<DVec3> positions(m_app.getAllocator());
			positions.resize(selected.size());
			for (u32 i = 0, c = selected.size(); i < c; ++i) {
				positions[i] = universe.getPosition(selected[i]) + diff;
			}
			m_editor.setEntitiesPositions(&selected[0], positions.begin(), positions.size());
			break;
		}
		case Gizmo::Config::ROTATE: {
			Array<DVec3> poss(m_app.getAllocator());
			Array<Quat> rots(m_app.getAllocator());
			rots.resize(selected.size());
			poss.resize(selected.size());
			for (u32 i = 0, c = selected.size(); i < c; ++i) {
				const Transform t = new_pivot_tr * old_pivot_tr.inverted() * universe.getTransform(selected[i]);
				poss[i] = t.pos;
				rots[i] = t.rot.normalized();
			}
			m_editor.setEntitiesPositionsAndRotations(&selected[0], poss.begin(), rots.begin(), rots.size());

			break;
		}
		case Gizmo::Config::SCALE: {
			const float diff = new_pivot_tr.scale / old_pivot_tr.scale;
			Array<float> scales(m_app.getAllocator());
			scales.resize(selected.size());
			for (u32 i = 0, c = selected.size(); i < c; ++i) {
				scales[i] = universe.getScale(selected[i]) * diff;
			}
			m_editor.setEntitiesScales(&selected[0], scales.begin(), scales.size());

			break;
		}
		default: ASSERT(false); break;
	}
	if (cfg.isAutosnapDown()) m_app.snapDown();
}

void SceneView::update(float time_delta)
{
	PROFILE_FUNCTION();
	m_pipeline->setUniverse(m_editor.getUniverse());
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
	if (m_move_forward_action->isActive()) m_view->moveCamera(1.0f, 0, 0, speed);
	if (m_move_back_action->isActive()) m_view->moveCamera(-1.0f, 0, 0, speed);
	if (m_move_left_action->isActive()) m_view->moveCamera(0.0f, -1.0f, 0, speed);
	if (m_move_right_action->isActive()) m_view->moveCamera(0.0f, 1.0f, 0, speed);
	if (m_move_down_action->isActive()) m_view->moveCamera(0, 0, -1.0f, speed);
	if (m_move_up_action->isActive()) m_view->moveCamera(0, 0, 1.0f, speed);
}


void SceneView::renderIcons()
{
	struct RenderJob : Renderer::RenderJob
	{
		RenderJob(IAllocator& allocator) 
			: m_allocator(allocator)
			, m_items(allocator) 
		{}

		void setup() override
		{
			PROFILE_FUNCTION();
			Array<EditorIcons::RenderData> data(m_allocator);
			m_ui->m_view->m_icons->getRenderData(&data);
			
			for (EditorIcons::RenderData& rd : data) {
				const Model* model = rd.model;
				if (!model || !model->isReady()) continue;

				for (int i = 0; i <= model->getLODs()[0].to_mesh; ++i) {
					const Mesh& mesh = model->getMesh(i);
					Item& item = m_items.emplace();
					item.mesh = mesh.render_data;
					item.mtx = rd.mtx;
					item.material = mesh.material->getRenderData();
					item.program = mesh.material->getShader()->getProgram(mesh.vertex_decl, item.material->define_mask);
				}
			}
		}

		void execute() override
		{
			PROFILE_FUNCTION();
			const gpu::BufferHandle drawcall_ub = m_ui->m_pipeline->getDrawcallUniformBuffer();

			for (const Item& item : m_items) {
				const Mesh::RenderData* rd = item.mesh;
			
				gpu::update(drawcall_ub, &item.mtx.m11, sizeof(item.mtx));
				gpu::bindTextures(item.material->textures, 0, item.material->textures_count);
				gpu::useProgram(item.program);
				gpu::bindIndexBuffer(rd->index_buffer_handle);
				gpu::bindVertexBuffer(0, rd->vertex_buffer_handle, 0, rd->vb_stride);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				gpu::setState(item.material->render_states);
				gpu::drawTriangles(rd->indices_count, rd->index_type);
			}
		}

		struct Item {
			gpu::ProgramHandle program;
			Mesh::RenderData* mesh;
			Material::RenderData* material;
			Matrix mtx;
		};

		IAllocator& m_allocator;
		Array<Item> m_items;
		SceneView* m_ui;
	};

	Engine& engine = m_editor.getEngine();
	Renderer* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));

	IAllocator& allocator = renderer->getAllocator();
	RenderJob* cmd = LUMIX_NEW(allocator, RenderJob)(allocator);
	cmd->m_ui = this;
	renderer->queue(cmd, 0);
}


void SceneView::renderSelection()
{
	struct RenderJob : Renderer::RenderJob
	{
		RenderJob(IAllocator& allocator) : m_items(allocator) {}

		void setup() override
		{
			PROFILE_FUNCTION();
			const Array<EntityRef>& entities = m_editor->getSelectedEntities();
			RenderScene* scene = m_pipeline->getScene();
			const Universe& universe = scene->getUniverse();
			for (EntityRef e : entities) {
				if (!scene->getUniverse().hasComponent(e, MODEL_INSTANCE_TYPE)) continue;

				const Model* model = scene->getModelInstanceModel(e);
				if (!model || !model->isReady()) continue;

				for (int i = 0; i <= model->getLODs()[0].to_mesh; ++i) {
					const Mesh& mesh = model->getMesh(i);
					Item& item = m_items.emplace();
					item.mesh = mesh.render_data;
					item.mtx = universe.getRelativeMatrix(e, m_cam_pos);
					item.material = mesh.material->getRenderData();
					item.program = mesh.material->getShader()->getProgram(mesh.vertex_decl, m_define_mask | item.material->define_mask);
				}
			}
		}

		void execute() override
		{
			PROFILE_FUNCTION();
			const gpu::BufferHandle drawcall_ub = m_pipeline->getDrawcallUniformBuffer();

			for (const Item& item : m_items) {
				const Mesh::RenderData* rd = item.mesh;
			
				gpu::update(drawcall_ub, &item.mtx.m11, sizeof(item.mtx));
				gpu::bindTextures(item.material->textures, 0, item.material->textures_count);
				gpu::useProgram(item.program);
				gpu::bindIndexBuffer(rd->index_buffer_handle);
				gpu::bindVertexBuffer(0, rd->vertex_buffer_handle, 0, rd->vb_stride);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				gpu::setState(item.material->render_states);
				gpu::drawTriangles(rd->indices_count, rd->index_type);
			}
		}

		struct Item {
			gpu::ProgramHandle program;
			Mesh::RenderData* mesh;
			Material::RenderData* material;
			Matrix mtx;
		};

		Array<Item> m_items;
		Pipeline* m_pipeline;
		WorldEditor* m_editor;
		u32 m_define_mask;
		DVec3 m_cam_pos;
	};

	Engine& engine = m_editor.getEngine();
	Renderer* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
	IAllocator& allocator = renderer->getAllocator();
	RenderJob* job = LUMIX_NEW(allocator, RenderJob)(allocator);
	job->m_define_mask = 1 << renderer->getShaderDefineIdx("DEPTH");
	job->m_pipeline = m_pipeline;
	job->m_editor = &m_editor;
	job->m_cam_pos = m_view->getViewport().pos;
	renderer->queue(job, 0);
}


void SceneView::renderGizmos()
{
	struct Cmd : Renderer::RenderJob
	{
		Cmd(IAllocator& allocator)
			: cmds(allocator)
		{}

		void setup() override {
			PROFILE_FUNCTION();
			viewport = view->m_view->getViewport();
			cmds.swap(view->m_view->m_draw_cmds);
			Engine& engine = view->m_editor.getEngine();
			renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
			auto& vertices = view->m_view->m_draw_vertices;
			vb = renderer->allocTransient(vertices.byte_size());
			memcpy(vb.ptr, vertices.begin(), vertices.byte_size());
		}

		void execute() override {
			PROFILE_FUNCTION();
			if (cmds.empty()) return;

			renderer->beginProfileBlock("gizmos", 0);
			gpu::pushDebugGroup("gizmos");
			gpu::setState(u64(gpu::StateFlags::DEPTH_TEST) | u64(gpu::StateFlags::DEPTH_WRITE));
			u32 offset = 0;
			const gpu::BufferHandle drawcall_ub = view->getPipeline()->getDrawcallUniformBuffer();
			const Matrix mtx = Matrix::IDENTITY;
			for (const UniverseViewImpl::DrawCmd& cmd : cmds) {
				gpu::update(drawcall_ub, &mtx.m11, sizeof(mtx));
				gpu::useProgram(program);
				gpu::bindIndexBuffer(gpu::INVALID_BUFFER);
				gpu::bindVertexBuffer(0, vb.buffer, vb.offset + offset, sizeof(UniverseView::Vertex));
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				const gpu::PrimitiveType primitive_type = cmd.lines ? gpu::PrimitiveType::LINES : gpu::PrimitiveType::TRIANGLES;
				gpu::drawArrays(0, cmd.vertex_count, primitive_type);

				offset += cmd.vertex_count * sizeof(UniverseView::Vertex);
			}
			gpu::popDebugGroup();
			
			renderer->endProfileBlock();
		}

		Renderer* renderer;
		Renderer::TransientSlice ib;
		Renderer::TransientSlice vb;
		Viewport viewport;
		SceneView* view;
		gpu::ProgramHandle program;
		Array<UniverseViewImpl::DrawCmd> cmds;
	};

	if (!m_debug_shape_shader || !m_debug_shape_shader->isReady()) return;

	Engine& engine = m_editor.getEngine();
	Renderer* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));

	IAllocator& allocator = renderer->getAllocator();
	Cmd* cmd = LUMIX_NEW(allocator, Cmd)(allocator);
	gpu::VertexDecl decl;
	decl.addAttribute(0, 0, 3, gpu::AttributeType::FLOAT, 0);
	decl.addAttribute(1, 12, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
	cmd->program = m_debug_shape_shader->getProgram(decl, 0);
	cmd->view = this;
	renderer->queue(cmd, 0);
}


void SceneView::captureMouse(bool capture)
{
	if(m_is_mouse_captured == capture) return;
	m_is_mouse_captured = capture;
	OS::showCursor(!m_is_mouse_captured);
	if (capture) {
		const OS::Point p = OS::getMouseScreenPos();
		m_captured_mouse_x = p.x;
		m_captured_mouse_y = p.y;
	}
	else {
		OS::setMouseScreenPos(m_captured_mouse_x, m_captured_mouse_y);
		OS::unclipCursor();
	}
}


RayCastModelHit SceneView::castRay(float x, float y)
{
	auto* scene =  m_pipeline->getScene();
	ASSERT(scene);
	
	const Viewport& vp = m_view->getViewport();
	DVec3 origin;
	Vec3 dir;
	vp.getRay({x * vp.w, y * vp.h}, origin, dir);
	return scene->castRay(origin, dir, INVALID_ENTITY);
}


void SceneView::handleDrop(const char* path, float x, float y)
{
	const RayCastModelHit hit = castRay(x, y);

	if (Path::hasExtension(path, "fbx"))
	{
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 5) * hit.dir;

		m_editor.beginCommandGroup(crc32("insert_mesh"));
		EntityRef entity = m_editor.addEntity();
		m_editor.setEntitiesPositions(&entity, &pos, 1);
		m_editor.addComponent(Span(&entity, 1), MODEL_INSTANCE_TYPE);
		m_editor.setProperty(MODEL_INSTANCE_TYPE, "", -1, "Source", Span(&entity, 1), Path(path));
		m_editor.endCommandGroup();
	}
	else if (Path::hasExtension(path, "fab"))
	{
		ResourceManagerHub& manager = m_editor.getEngine().getResourceManager();
		PrefabResource* prefab = manager.load<PrefabResource>(Path(path));
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;
		m_editor.getPrefabSystem().instantiatePrefab(*prefab, pos, Quat::IDENTITY, 1);
	}
	else if (Path::hasExtension(path, "phy"))
	{
		if (hit.is_hit && hit.entity.isValid())
		{
			m_editor.beginCommandGroup(crc32("insert_phy_component"));
			const EntityRef e = (EntityRef)hit.entity;
			m_editor.selectEntities(Span(&e, 1), false);
			m_editor.addComponent(Span(&e, 1), MESH_ACTOR_TYPE);
			m_editor.setProperty(MESH_ACTOR_TYPE, "", -1, "Source", Span(&e, 1), path);
			m_editor.endCommandGroup();
		}
		else
		{
			const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;
			m_editor.beginCommandGroup(crc32("insert_phy"));
			EntityRef entity = m_editor.addEntity();
			m_editor.setEntitiesPositions(&entity, &pos, 1);
			m_editor.selectEntities(Span(&entity, 1), false);
			m_editor.addComponent(Span(&entity, 1), MESH_ACTOR_TYPE);
			m_editor.setProperty(MESH_ACTOR_TYPE, "", -1, "Source", Span(&entity, 1), path);
			m_editor.endCommandGroup();
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
	if (ImGui::BeginToolbar("scene_view_toolbar", pos, ImVec2(0, 24)))
	{
		for (auto* action_name : actions_names)
		{
			auto* action = m_app.getAction(action_name);
			action->toolbarButton(m_app.getBigIconFont());
		}
	}

	m_app.getAction("cameraSpeed")->toolbarButton(m_app.getBigIconFont());

	ImGui::PushItemWidth(50);
	ImGui::SameLine();
	float offset = (24 - ImGui::GetTextLineHeightWithSpacing()) / 2;
	pos = ImGui::GetCursorPos();
	pos.y += offset;
	ImGui::SetCursorPos(pos);
	ImGui::DragFloat("##camera_speed", &m_camera_speed, 0.1f, 0.01f, 999.0f, "%.2f");
	
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
	pos = ImGui::GetCursorPos();
	pos.y -= offset;
	ImGui::SetCursorPos(pos);
	ImVec4 tint_color = ImGui::GetStyle().Colors[ImGuiCol_Text];
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

	ImGui::SameLine(0, 20);
	ImGui::Checkbox("Stats", &m_show_stats);

	ImGui::SameLine(0, 20);
	m_pipeline->callLuaFunction("onGUI");

	ImGui::SameLine(0, 20);
	ImGui::Checkbox("Measure", &m_is_measure_active);

	if (m_is_measure_active) {
		ImGui::SameLine(0, 20);
		const double d = (m_measure_to - m_measure_from).length();
		ImGui::Text(" | Measured distance: %f", d);
	}

	ImGui::PopItemWidth();

	ImGui::EndToolbar();
}

void SceneView::handleEvents() {
	const bool handle_input = ImGui::IsItemHovered() && OS::getFocused() == ImGui::GetWindowViewport()->PlatformHandle;
	const OS::Event* events = m_app.getEvents();
	for (int i = 0, c = m_app.getEventsCount(); i < c; ++i) {
		const OS::Event& event = events[i];
		switch (event.type) {
			case OS::Event::Type::MOUSE_BUTTON: {
				const OS::Point cp = OS::getMouseScreenPos();
				Vec2 rel_mp = { (float)cp.x, (float)cp.y };
				rel_mp.x -= m_screen_x;
				rel_mp.y -= m_screen_y;
				if (handle_input) {
					if (event.mouse_button.button == OS::MouseButton::RIGHT) {
						ImGui::SetWindowFocus();
						captureMouse(event.mouse_button.down);
					}
					ImGui::ResetActiveID();
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
			case OS::Event::Type::MOUSE_MOVE: 
				if (handle_input) {
					const OS::Point cp = OS::getMouseScreenPos();
					Vec2 rel_mp = {(float)cp.x, (float)cp.y};
					rel_mp.x -= m_screen_x;
					rel_mp.y -= m_screen_y;
					m_view->onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)event.mouse_move.xrel, (int)event.mouse_move.yrel);
				}
				break;
		}
	}
}

void SceneView::statsUI(float x, float y) {
	if (!m_show_stats) return;

	float toolbar_height = 24 + ImGui::GetStyle().FramePadding.y * 2;
	ImVec2 view_pos(x, y);
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
		const auto& stats = m_pipeline->getStats();
		ImGui::LabelText("Draw calls (scene view only)", "%d", stats.draw_call_count);
		ImGui::LabelText("Instances (scene view only)", "%d", stats.instance_count);
		char buf[30];
		toCStringPretty(stats.triangle_count, Span(buf));
		ImGui::LabelText("Triangles (scene view only)", "%s", buf);
		ImGui::LabelText("Resolution", "%dx%d", m_width, m_height);
	}
	ImGui::End();
	ImGui::PopStyleColor();
}

void SceneView::onWindowGUI()
{
	PROFILE_FUNCTION();
	m_pipeline->setUniverse(m_editor.getUniverse());

	bool is_open = false;
	ImVec2 view_pos;
	const char* title = ICON_FA_GLOBE "Scene View###Scene View";
	if (m_log_ui.getUnreadErrorCount() > 0) title = ICON_FA_GLOBE "Scene View | " ICON_FA_EXCLAMATION_TRIANGLE " errors in log###Scene View";

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoScrollWithMouse)) {
		is_open = true;
		ImGui::Dummy(ImVec2(2, 2));
		onToolbar();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		Viewport vp = m_view->getViewport();
		vp.w = (int)size.x;
		vp.h = (int)size.y;
		m_view->setViewport(vp);
		m_pipeline->setViewport(vp);
		m_pipeline->render(false);
		m_view->m_draw_vertices.clear();
		m_view->m_draw_cmds.clear();
		m_view->inputFrame();

		const gpu::TextureHandle texture_handle = m_pipeline->getOutput();
		if (size.x > 0 && size.y > 0) {
			const ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
			m_screen_x = int(cursor_pos.x);
			m_screen_y = int(cursor_pos.y);
			m_width = int(size.x);
			m_height = int(size.y);
			view_pos = ImGui::GetCursorScreenPos();
			
			if (texture_handle.isValid()) {
				void* t = (void*)(uintptr)texture_handle.value;
				if (gpu::isOriginBottomLeft()) {
					ImGui::Image(t, size, ImVec2(0, 1), ImVec2(1, 0));
				} 
				else {
					ImGui::Image(t, size);
				}
			}

			if (m_is_mouse_captured) {
				const ImVec2 pos = ImGui::GetItemRectMin();
				const ImVec2 size = ImGui::GetItemRectSize();
				OS::clipCursor((int)pos.x, (int)pos.y, (int)size.x, (int)size.y);
			}

			if (ImGui::BeginDragDropTarget()) {
				if (auto* payload = ImGui::AcceptDragDropPayload("path")) {
					const ImVec2 drop_pos = (ImGui::GetMousePos() - view_pos) / size;
					handleDrop((const char*)payload->Data, drop_pos.x, drop_pos.y);
				}
				ImGui::EndDragDropTarget();
			}

			handleEvents();
		}
	}
	else {
		m_view->inputFrame();
		m_view->m_draw_vertices.clear();
		m_view->m_draw_cmds.clear();
	}

	if (m_is_mouse_captured && OS::getFocused() != ImGui::GetWindowViewport()->PlatformHandle) {
		captureMouse(false);
	}
	ImGui::End();
	ImGui::PopStyleVar();

	if (is_open) statsUI(view_pos.x, view_pos.y);
}


} // namespace Lumix
