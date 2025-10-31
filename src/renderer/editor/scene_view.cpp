#include <imgui/imgui.h>

#include "core/delegate_list.h"
#include "core/geometry.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/string.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/gizmo.h"
#include "editor/log_ui.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/component_types.h"
#include "engine/engine.h"
#include "engine/prefab.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "gui/gui_module.h"
#include "renderer/culling_system.h"
#include "renderer/draw_stream.h"
#include "renderer/draw2d.h"
#include "renderer/editor/editor_icon.h"
#include "renderer/editor/game_view.h"
#include "renderer/font.h"
#include "renderer/gpu/gpu.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/pose.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "scene_view.h"

namespace Lumix
{

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
		Path font_path("engine/editor/fonts/Roboto-Light.ttf");
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

	void onMouseUp(int x, int y, os::MouseButton button) {
		m_mouse_pos = {(float)x, (float)y};
		if (m_mouse_mode == MouseMode::SELECT) {
			if ((m_rect_selection_start.x != m_mouse_pos.x || m_rect_selection_start.y != m_mouse_pos.y) && m_rect_selection_timer > 0.1f) {
				rectSelect();
			}
			else {
				const Ray ray = m_viewport.getRay(m_mouse_pos);
				const RayCastModelHit hit = m_module->castRay(ray, INVALID_ENTITY);
				GUIModule* gui_module = (GUIModule*)m_module->getWorld().getModule("gui");

				Span<const EntityRef> selected_entities = m_editor.getSelectedEntities();
				if (m_snap_mode != SnapMode::NONE && selected_entities.size() != 0 && hit.is_hit) {
					DVec3 snap_pos = ray.origin + ray.dir * hit.t;
					if (m_snap_mode == SnapMode::VERTEX) snap_pos = getClosestVertex(hit);
					const Quat rot = m_editor.getWorld()->getRotation(selected_entities[0]);
					const Gizmo::Config& gizmo_cfg = m_app.getGizmoConfig();
					const Vec3 offset = rot.rotate(gizmo_cfg.offset);
					m_editor.snapEntities(snap_pos - offset, gizmo_cfg.isTranslateMode());
				}
				else {
					auto icon_hit = m_icons->raycast(ray.origin, ray.dir);
					if (icon_hit.entity != INVALID_ENTITY) {
						if(icon_hit.entity.isValid()) {
							EntityRef e = (EntityRef)icon_hit.entity;
							m_editor.selectEntities(Span(&e, 1), ImGui::GetIO().KeyCtrl);
						}
					}
					else {
						GUIRayHit gui_hit; 
						if (gui_module) {
							gui_hit = gui_module->raycast(ray);
						}
						if (hit.is_hit && (gui_hit.entity == INVALID_ENTITY || gui_hit.t > hit.t)) {
							if (hit.entity.isValid()) {
								EntityRef entity = (EntityRef)hit.entity;
								m_editor.selectEntities(Span(&entity, 1), ImGui::GetIO().KeyCtrl);
							}
						}
						else if (gui_hit.entity.isValid()) {
							m_editor.selectEntities(Span((EntityRef*)&gui_hit.entity, 1), ImGui::GetIO().KeyCtrl);
						}
						else {
							m_editor.selectEntities({}, false);
						}
					}
				}
			}
		}

		m_is_mouse_down[(int)button] = false;
		if (m_mouse_handling_plugin) {
			m_mouse_handling_plugin->onMouseUp(*this, x, y, button);
			m_mouse_handling_plugin = nullptr;
		}
		m_mouse_mode = MouseMode::NONE;
	}

	Vec2 getMousePos() const override { return m_mouse_pos; }
	
	void resetPivot() override {
		m_app.getGizmoConfig().offset = Vec3::ZERO;
	}
	
	void setCustomPivot() override
	{
		Span<const EntityRef> selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.size() == 0) return;

		const Ray ray = m_viewport.getRay(m_mouse_pos);
		const RayCastModelHit hit = m_module->castRay(ray, INVALID_ENTITY);
		if (!hit.is_hit || hit.entity != selected_entities[0]) return;

		const DVec3 snap_pos = getClosestVertex(hit);

		const Transform tr = m_editor.getWorld()->getTransform(selected_entities[0]);
		m_app.getGizmoConfig().offset = tr.rot.conjugated() * Vec3(snap_pos - tr.pos);
	}


	DVec3 getClosestVertex(const RayCastModelHit& hit) const {
		ASSERT(hit.entity.isValid());
		const EntityRef entity = (EntityRef)hit.entity;
		const DVec3& wpos = hit.origin + hit.t * hit.dir;
		World& world = m_module->getWorld();
		const Transform tr = world.getTransform(entity);
		const Vec3 lpos = tr.rot.conjugated() * Vec3(wpos - tr.pos);
		if (!world.hasComponent(entity, types::model_instance)) return wpos;

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
				const float yaw = -m_app.getSettings().m_mouse_sensitivity_x.eval((float)relx);
				const float pitch = -m_app.getSettings().m_mouse_sensitivity_y.eval((float)rely);
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
		if (m_editor.getSelectedEntities().size() == 0) return;

		m_editor.setEntitiesPositionsAndRotations(m_editor.getSelectedEntities().begin(), &m_viewport.pos, &m_viewport.rot, 1);
	}

	void lookAtSelected() override {
		const World* world = m_editor.getWorld();
		if (m_editor.getSelectedEntities().size() == 0) return;

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
		Span<const EntityRef> selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.size() != 0) {
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
		Span<const EntityRef> selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.size() != 0) {
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
		Span<const EntityRef> selected_entities = m_editor.getSelectedEntities();
		if (selected_entities.size() != 0) {
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

		if (m_app.getCommonActions().cam_orbit.isActive() && m_editor.getSelectedEntities().size() != 0) {
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

struct SceneView::RenderPlugin : Lumix::RenderPlugin {
	RenderPlugin(SceneView& view, Renderer& renderer)
		: m_scene_view(view)
	{
		ResourceManagerHub& rm = renderer.getEngine().getResourceManager();
		m_grid_shader = rm.load<Shader>(Path("engine/shaders/grid.hlsl"));
		m_debug_shape_shader = rm.load<Shader>(Path("engine/shaders/debug_shape.hlsl"));
		m_selection_outline_shader = rm.load<Shader>(Path("engine/shaders/selection_outline.hlsl"));
	}

	~RenderPlugin() {
		m_grid_shader->decRefCount();
		m_debug_shape_shader->decRefCount();
		m_selection_outline_shader->decRefCount();
	}

	RenderBufferHandle renderBeforeTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
		if (m_scene_view.m_pipeline.get() != &pipeline) return input;
		if (pipeline.m_debug_show == Pipeline::DebugShow::PLUGIN) return input;
		Renderer& renderer = pipeline.getRenderer();

		// selection
		Span<const EntityRef> entities = m_scene_view.m_editor.getSelectedEntities();
		const Viewport& vp = m_scene_view.m_view->getViewport();
		if (entities.size() < 5000) {
			const RenderBufferHandle selection_mask = renderer.createRenderbuffer({
				.size = {vp.w, vp.h},
				.format = gpu::TextureFormat::D32,
				.debug_name = "selection_depth"
			});
			renderer.setRenderTargets({}, selection_mask);
			pipeline.clear(gpu::ClearFlags::ALL, 0, 0, 0, 0, 0);
			UniformPool& uniform_pool = renderer.getUniformPool();
			TransientPool& transient_pool = renderer.getTransientPool();
	
			renderer.pushJob("selection", [&pipeline, &renderer, this, entities, &uniform_pool, &transient_pool](DrawStream& stream) {
				RenderModule* module = pipeline.getModule();
				const World& world = module->getWorld();
				const u32 skinned_define = 1 << renderer.getShaderDefineIdx("SKINNED");
				const u32 depth_define = 1 << renderer.getShaderDefineIdx("DEPTH");
				const DVec3 view_pos = m_scene_view.m_view->getViewport().pos;
				Array<DualQuat> dq_pose(renderer.getAllocator());
				for (EntityRef e : entities) {
					if (!module->getWorld().hasComponent(e, types::model_instance)) continue;

					const Model* model = module->getModelInstanceModel(e);
					if (!model || !model->isReady()) continue;

					const Pose* pose = module->lockPose(e);
					for (int i = 0; i <= model->getLODIndices()[0].to; ++i) {
						const Mesh& mesh = model->getMesh(i);
						const MeshMaterial& mesh_mat = model->getMeshMaterial(i);
					
						Material* material = mesh_mat.material;
						u32 define_mask = material->getDefineMask() | depth_define;
						const Matrix mtx = world.getRelativeMatrix(e, view_pos);
						dq_pose.clear();
						if (pose && pose->count > 0 && mesh.type == Mesh::SKINNED) {
							define_mask |= skinned_define;
							dq_pose.reserve(pose->count);
							for (int j = 0, c = pose->count; j < c; ++j) {
								const LocalRigidTransform& inv_bind = model->getInverseBindTransform(j);
								const LocalRigidTransform tmp = {pose->positions[j], pose->rotations[j]};
								dq_pose.push((tmp * inv_bind).toDualQuat());
							}
						}
			
						const gpu::StateFlags state = gpu::StateFlags::DEPTH_WRITE | gpu::StateFlags::DEPTH_FUNCTION;
						if (dq_pose.empty()) {
							struct UBData {
								Matrix mtx;
								MaterialIndex material_index;
							} ub_data = {
								mtx,
								material->getIndex()
							};
							TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
							stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
							stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
							gpu::ProgramHandle program = mesh_mat.material->getShader()->getProgram(material->m_render_states | state, mesh.vertex_decl, define_mask, mesh.semantics_defines);
							stream.useProgram(program);
						}
						else {
							struct SkinnedInstanceData {
								IVec3 idata;
								Vec3 pos;
								Quat rot;
								Vec3 scale;
								Vec3 prev_pos;
								Quat prev_rot;
								Vec3 prev_scale;
							};

							TransientSlice inst_buf = alloc(transient_pool, sizeof(SkinnedInstanceData));
							TransientSlice bones = alloc(transient_pool, dq_pose.byte_size());
							SkinnedInstanceData* idata = (SkinnedInstanceData*)inst_buf.ptr;
							
							Transform tr = world.getTransform(e);
							idata->pos = Vec3(tr.pos - view_pos);
							idata->prev_pos = idata->pos;
							idata->rot = tr.rot;
							idata->prev_rot = idata->rot;
							idata->scale = tr.scale;
							idata->prev_scale = idata->scale;
							idata->idata.x = (i32)material->getIndex();
							idata->idata.y = gpu::getBindlessHandle(bones.buffer).value;
							idata->idata.z = bones.offset;

							memcpy(bones.ptr, dq_pose.begin(), dq_pose.byte_size());
							gpu::VertexDecl skinned_instanced_decl(gpu::PrimitiveType::NONE);
							skinned_instanced_decl.addAttribute(0, 3, gpu::AttributeType::U32, gpu::Attribute::INSTANCED); // material index, bones_buffer, bones_buffer_offset
							skinned_instanced_decl.addAttribute(12, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // pos
							skinned_instanced_decl.addAttribute(24, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // rot
							skinned_instanced_decl.addAttribute(40, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // scale
							skinned_instanced_decl.addAttribute(52, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // prev_pos
							skinned_instanced_decl.addAttribute(64, 4, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // prev_rot
							skinned_instanced_decl.addAttribute(80, 3, gpu::AttributeType::FLOAT, gpu::Attribute::INSTANCED); // prev_scale
							const u32 skinned_instance_stride = skinned_instanced_decl.getStride();
							stream.bindVertexBuffer(1, inst_buf.buffer, inst_buf.offset, inst_buf.size);
							gpu::ProgramHandle program = mesh_mat.material->getShader()->getProgram(material->m_render_states | state, mesh.vertex_decl, skinned_instanced_decl, define_mask, mesh.semantics_defines);
							stream.useProgram(program);
						}
		
						stream.bindIndexBuffer(mesh.index_buffer_handle);
						stream.bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
						stream.drawIndexed(0, mesh.indices_count, mesh.index_type);
					}
					module->unlockPose(e, false);
				}
			});

			DrawStream& stream = pipeline.getRenderer().getDrawStream();
			struct {
				gpu::BindlessHandle mask;
				gpu::RWBindlessHandle output;
			} ub = {
				pipeline.toBindless(selection_mask, stream),
				pipeline.toRWBindless(input, stream)
			};
			pipeline.setUniform(ub);
			pipeline.dispatch(*m_selection_outline_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);
			renderer.releaseRenderbuffer(selection_mask);
		}

		// grid
		renderer.setRenderTargets(Span(&input, 1), gbuffer.DS, gpu::FramebufferFlags::READONLY_DEPTH_STENCIL);
		if (m_grid_shader->isReady() && m_show_grid) {
			renderer.setRenderTargets(Span(&input, 1), gbuffer.DS);
			pipeline.drawArray(0, 4, *m_grid_shader, 0, gpu::StateFlags::DEPTH_FUNCTION | gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA));
		}

		const RenderBufferHandle icons_ds = renderer.createRenderbuffer({
			.size = {vp.w, vp.h},
			.format = gpu::TextureFormat::D32,
			.debug_name = "icons_ds"
		});

		renderer.setRenderTargets({}, icons_ds);
		pipeline.clear(gpu::ClearFlags::DEPTH, 0, 0, 0, 0, 0);
		UniformPool& uniform_pool = renderer.getUniformPool();
		TransientPool& transient_pool = renderer.getTransientPool();

		// icons
		if (m_show_icons) {
			renderer.setRenderTargets(Span(&input, 1), icons_ds);
			DrawStream& stream = pipeline.getRenderer().getDrawStream();
			pipeline.setUniform(pipeline.toBindless(gbuffer.DS, stream), UniformBuffer::DRAWCALL2);
			
			renderer.pushJob("icons", [this, &uniform_pool](DrawStream& stream) {
				const Viewport& vp = m_scene_view.m_view->getViewport();
				const Matrix camera_mtx({ 0, 0, 0 }, vp.rot);

				EditorIcons* icon_manager = m_scene_view.m_view->m_icons.get();
				icon_manager->computeScales();
				const HashMap<EntityRef, EditorIcons::Icon>& icons = icon_manager->getIcons();
				for (const EditorIcons::Icon& icon : icons) {
					const Model* model = icon_manager->getModel(icon.type);
					if (!model || !model->isReady()) continue;

					const Matrix mtx = icon_manager->getIconMatrix(icon, camera_mtx, vp.pos, vp.is_ortho, vp.ortho_size);

					for (int i = 0; i <= model->getLODIndices()[0].to; ++i) {
						const Mesh& mesh = model->getMesh(i);
						const MeshMaterial& mesh_mat = model->getMeshMaterial(i);
						const Material* material = mesh_mat.material;
						const gpu::StateFlags state = material->m_render_states | gpu::StateFlags::DEPTH_FN_GREATER | gpu::StateFlags::DEPTH_WRITE;
						gpu::ProgramHandle program = material->getShader()->getProgram(state, mesh.vertex_decl, material->getDefineMask(), mesh.semantics_defines);

						struct UB {
							Matrix mtx;
							MaterialIndex material_index;
						} ub_data = {
							mtx,
							material->getIndex()
						};
						const TransientSlice ub = alloc(uniform_pool, &ub_data, sizeof(ub_data));
						stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);

						stream.useProgram(program);
						stream.bindIndexBuffer(mesh.index_buffer_handle);
						stream.bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
						stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
						stream.drawIndexed(0, mesh.indices_count, mesh.index_type);
					}
				}
			});
		}

		// gizmo
		auto& vertices = m_scene_view.m_view->m_draw_vertices;
		if (m_debug_shape_shader->isReady() && !vertices.empty()) {
			renderer.setRenderTargets(Span(&input, 1), icons_ds);
			pipeline.clear(gpu::ClearFlags::DEPTH, 0, 0, 0, 0, 0);

			renderer.pushJob("gizmos", [&vertices, this, &transient_pool, &uniform_pool](DrawStream& stream) {
				TransientSlice vb = alloc(transient_pool, vertices.byte_size());
				memcpy(vb.ptr, vertices.begin(), vertices.byte_size());
				const TransientSlice ub = alloc(uniform_pool, &Matrix::IDENTITY.columns[0].x, sizeof(Matrix));

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
				for (const WorldViewImpl::DrawCmd& cmd : m_scene_view.m_view->m_draw_cmds) {
					const gpu::ProgramHandle program = cmd.lines ? lines_program : triangles_program;
					stream.useProgram(program);
					stream.bindIndexBuffer(gpu::INVALID_BUFFER);
					stream.bindVertexBuffer(0, vb.buffer, vb.offset + offset, sizeof(WorldView::Vertex));
					stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
					stream.drawArrays(0, cmd.vertex_count);

					offset += cmd.vertex_count * sizeof(WorldView::Vertex);
				}

				m_scene_view.m_view->m_draw_cmds.clear();
			});

		}
		renderer.releaseRenderbuffer(icons_ds);

		return input;
	}

	void debugUI(Pipeline& pipeline) override {
		ImGui::Checkbox("Grid", &m_show_grid);
		ImGui::Checkbox("Icons", &m_show_icons);
	}

	SceneView& m_scene_view;
	Shader* m_grid_shader;
	Shader* m_debug_shape_shader;
	Shader* m_selection_outline_shader;
	bool m_show_grid = true;
	bool m_show_icons = true;
};

SceneView::SceneView(StudioApp& app)
	: m_app(app)
	, m_log_ui(app.getLogUI())
	, m_editor(m_app.getWorldEditor())
{
	m_camera_speed = 0.1f;
	m_is_mouse_captured = false;

	m_debug_show_actions[(u32)Pipeline::DebugShow::NONE].create("Debug view", "No debug", "None", "debug_view_disable", "");
	m_debug_show_actions[(u32)Pipeline::DebugShow::ALBEDO].create("Debug view", "Albedo", "Albedo", "debug_view_albedo", "");
	m_debug_show_actions[(u32)Pipeline::DebugShow::NORMAL].create("Debug view", "Normal", "Normal", "debug_view_normal", "");
	m_debug_show_actions[(u32)Pipeline::DebugShow::ROUGHNESS].create("Debug view", "Roughness", "Roughness", "debug_view_roughness", "");
	m_debug_show_actions[(u32)Pipeline::DebugShow::METALLIC].create("Debug view", "Metallic", "Metallic", "debug_view_metalic", "");
	m_debug_show_actions[(u32)Pipeline::DebugShow::AO].create("Debug view", "AO", "AO", "debug_view_ao", "");
	m_debug_show_actions[(u32)Pipeline::DebugShow::VELOCITY].create("Debug view", "Velocity", "Velocity", "debug_view_velocity", "");
	m_debug_show_actions[(u32)Pipeline::DebugShow::LIGHT_CLUSTERS].create("Debug view", "Light clusters", "Light clusters", "debug_view_light_clusters", "");
	m_debug_show_actions[(u32)Pipeline::DebugShow::PROBE_CLUSTERS].create("Debug view", "Probe clusters", "Probe clusters", "debug_view_probe_clusters", "");

	Gizmo::Config& gizmo_cfg = m_app.getGizmoConfig();
	m_app.getSettings().registerOption("gizmo_position_step", &gizmo_cfg.step_config.position, "Scene view", "Grid size when aligning on a grid");
	m_app.getSettings().registerOption("gizmo_rotation_step", &gizmo_cfg.step_config.rotation, "Scene view", "Rotation step when rotating with gizmo");
	m_app.getSettings().registerOption("gizmo_scale_step", &gizmo_cfg.step_config.scale, "Scene view", "Scale step when scaling with gizmo");
	m_app.getSettings().registerOption("quicksearch_preview", &m_search_preview, "Scene view", "Show previews in quick search");
	m_app.getSettings().registerOption("show_camera_preview", &m_show_camera_preview, "Scene view", "Show camera preview");
	m_app.getSettings().registerOption("mouse_wheel_changes_speed", &m_mouse_wheel_changes_speed, "Scene view", "Mouse wheel changes speed");
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
	m_render_plugin = UniquePtr<RenderPlugin>::create(renderer->getAllocator(), *this, *renderer);
	renderer->addPlugin(*m_render_plugin.get());
	m_app.getSettings().registerOption("show_grid", &m_render_plugin->m_show_grid, "Scene view", "Show grid");
	
	m_camera_preview_pipeline = Pipeline::create(*renderer, PipelineType::GAME_VIEW);

	m_pipeline = Pipeline::create(*renderer, PipelineType::SCENE_VIEW);
}


SceneView::~SceneView()
{
	Engine& engine = m_app.getEngine();
	auto* renderer = static_cast<Renderer*>(engine.getSystemManager().getSystem("renderer"));
	renderer->removePlugin(*m_render_plugin.get());

	m_editor.setView(nullptr);
	LUMIX_DELETE(m_app.getAllocator(), m_view);
}


void SceneView::toggleWireframe() {
	WorldEditor& editor = m_app.getWorldEditor();
	Span<const EntityRef> selected = m_editor.getSelectedEntities();
	if (selected.size() == 0) return;

	World& world = *editor.getWorld();
	RenderModule& module = *(RenderModule*)world.getModule(types::model_instance);

	Array<Material*> materials(m_app.getAllocator());
	for (EntityRef e : selected) {
		if (world.hasComponent(e, types::model_instance)) {
			Model* model = module.getModelInstanceModel(e);
			if (!model->isReady()) continue;
			
			for (u32 i = 0; i < (u32)model->getMeshCount(); ++i) {
				const MeshMaterial& mesh = model->getMeshMaterial(i);
				materials.push(mesh.material);
			}
		}
		if (world.hasComponent(e, types::terrain)) {
			materials.push(module.getTerrainMaterial(e));
		}
		if (world.hasComponent(e, types::procedural_geom)) {
			materials.push(module.getProceduralGeometry(e).material);
		}
	}
	materials.removeDuplicates();
	for (Material* m : materials) {
		m->setWireframe(!m->wireframe());
	}
}

void SceneView::rotate90Degrees() {
	WorldEditor& editor = m_app.getWorldEditor();
	World& world = *editor.getWorld();
	editor.beginCommandGroup("rot90deg");
	for (EntityRef e : editor.getSelectedEntities()) {
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
	Vec3 V = m_view->m_viewport.rot * Vec3(0, 0, -1);  
	if (fabsf(V.x) > fabsf(V.z)) {
		V = {signum(V.x), 0, 0};
	}
	else {
		V = {0, 0, signum(V.z)};
	}

	Vec3 step = m_app.getGizmoConfig().step_config.position;
	if (step.x <= 0) step.x = 1;
	if (step.y <= 0) step.y = 1;
	if (step.z <= 0) step.z = 1;
	V = V * step;

	Vec3 S(V.z, 0, -V.x);

	WorldEditor& editor = m_app.getWorldEditor();
	World& world = *editor.getWorld();
	editor.beginCommandGroup("rot90deg");
	for (EntityRef e : editor.getSelectedEntities()) {
		DVec3 pos = world.getPosition(e);
		pos += V * v.y + S * v.x;
		// round position to multiple of step
		pos += Vec3(step * 0.5f);
		pos.x = floor(pos.x / step.x) * step.x;
		pos.y = floor(pos.y / step.y) * step.y;
		pos.z = floor(pos.z / step.z) * step.z;
		editor.setEntitiesPositions(&e, &pos, 1);
	}
	editor.endCommandGroup();
}

void SceneView::manipulate() {
	PROFILE_FUNCTION();
	Span<const EntityRef> selected = m_editor.getSelectedEntities();
	if (selected.size() == 0) return;

	const bool is_anisotropic_scale = m_anisotropic_scale_action.isActive();
	Gizmo::Config& cfg = m_app.getGizmoConfig();
	cfg.setAnisotropicScale(is_anisotropic_scale);

	cfg.enableStep(m_use_grid_snapping);

	Transform tr = m_editor.getWorld()->getTransform(selected[0]);
	tr.pos += tr.rot.rotate(cfg.offset);
	const Transform old_pivot_tr = tr;
			
	const bool copy_move = m_copy_move_action.isActive();
	if (!copy_move || !m_view->m_is_mouse_down[0]) {
		m_copy_moved = false;
	}

	if (m_view->isMouseClick(os::MouseButton::LEFT)) cfg.ungrab();
	if (!Gizmo::manipulate(selected[0].index, *m_view, tr, cfg)) return;

	if (copy_move && !m_copy_moved) {
		m_editor.copyEntities();
		m_editor.pasteEntities(&tr.pos);
		selected = m_editor.getSelectedEntities();
		Gizmo::setDragged(selected[0].index);
		m_copy_moved = true;
	}

	// keep only topmost entities in selection
	Array<EntityRef> filtered_selection(m_app.getAllocator());
	World& world = *m_editor.getWorld();
	for (EntityRef e : selected) {
		bool is_topmost = true;
		for (u32 i = 0; i < (u32)filtered_selection.size(); ++i) {
			if (world.isDescendant(e, filtered_selection[i])) {
				filtered_selection.swapAndPop(i);
				--i;
			}
			else if (world.isDescendant(filtered_selection[i], e)) {
				is_topmost = false;
				break;
			}
		}
		if (is_topmost) filtered_selection.push(e);
	}

	const Transform new_pivot_tr = tr;
	tr.pos -= tr.rot.rotate(cfg.offset);
	switch (cfg.mode) {
		case Gizmo::Config::TRANSLATE: {
			const DVec3 diff = new_pivot_tr.pos - old_pivot_tr.pos;
			Array<DVec3> positions(m_app.getAllocator());
			positions.resize(filtered_selection.size());
			for (u32 i = 0, c = filtered_selection.size(); i < c; ++i) {
				positions[i] = world.getPosition(filtered_selection[i]) + diff;
			}
			m_editor.setEntitiesPositions(filtered_selection.begin(), positions.begin(), positions.size());
			break;
		}
		case Gizmo::Config::ROTATE: {
			Array<DVec3> poss(m_app.getAllocator());
			Array<Quat> rots(m_app.getAllocator());
			rots.resize(filtered_selection.size());
			poss.resize(filtered_selection.size());
			const Quat rot_diff = new_pivot_tr.rot * old_pivot_tr.rot.conjugated();
			const DVec3 pivot_pos = old_pivot_tr.pos;
			for (u32 i = 0, c = filtered_selection.size(); i < c; ++i) {
				const Transform old_tr = world.getTransform(filtered_selection[i]);
				
				poss[i] = rot_diff.rotate(old_tr.pos - pivot_pos) + pivot_pos;
				rots[i] = normalize(rot_diff * old_tr.rot);
			}
			m_editor.setEntitiesPositionsAndRotations(filtered_selection.begin(), poss.begin(), rots.begin(), rots.size());
			break;
		}
		case Gizmo::Config::SCALE: {
			const Vec3 diff = new_pivot_tr.scale / old_pivot_tr.scale;
			Array<Vec3> scales(m_app.getAllocator());
			scales.resize(filtered_selection.size());
			for (u32 i = 0, c = filtered_selection.size(); i < c; ++i) {
				scales[i] = world.getScale(filtered_selection[i]) * diff;
			}
			m_editor.setEntitiesScales(filtered_selection.begin(), scales.begin(), scales.size());
			break;
		}
	}
	if (cfg.autosnap) snapDown();
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
	m_view->update(time_delta);

	if (m_is_measure_active) {
		m_view->addCross(m_measure_from, 0.3f, Color::BLUE);
		m_view->addCross(m_measure_to, 0.3f, Color::BLUE);
		addLine(*m_view, m_measure_from, m_measure_to, Color::BLUE);
	}

	if (!m_game_view->m_game_view_merged_with_scene_view || !m_editor.isGameMode() || !m_game_view->isMouseCaptured()) {
		manipulate();
	}

	if (ImGui::IsAnyItemActive()) return;
	if (!m_is_mouse_captured) return;
	if (io.KeyCtrl) return;

	int screen_x = int(io.MousePos.x);
	int screen_y = int(io.MousePos.y);
	bool is_inside = screen_x >= m_screen_x && screen_y >= m_screen_y && screen_x <= m_screen_x + m_width &&
					 screen_y <= m_screen_y + m_height;
	if (!is_inside) return;

	if (m_mouse_wheel_changes_speed) {
		m_camera_speed = maximum(0.01f, m_camera_speed + io.MouseWheel / 20.0f);
	}
	else {
		if (io.MouseWheel > 0) m_view->moveCamera(1.0f, 0, 0, io.MouseWheel);
		if (io.MouseWheel < 0) m_view->moveCamera(-1.0f, 0, 0, -io.MouseWheel);
	}

	float speed = m_camera_speed * time_delta * 60.f;
	if (io.KeyShift) speed *= 10;
	const CommonActions& actions = m_app.getCommonActions();
	if (actions.cam_forward.isActive()) m_view->moveCamera(1.0f, 0, 0, speed);
	if (actions.cam_backward.isActive()) m_view->moveCamera(-1.0f, 0, 0, speed);
	if (actions.cam_left.isActive()) m_view->moveCamera(0.0f, -1.0f, 0, speed);
	if (actions.cam_right.isActive()) m_view->moveCamera(0.0f, 1.0f, 0, speed);
	if (actions.cam_down.isActive()) m_view->moveCamera(0, 0, -1.0f, speed);
	if (actions.cam_up.isActive()) m_view->moveCamera(0, 0, 1.0f, speed);
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

	const ResourceType type = m_app.getAssetCompiler().getResourceType(path);

	ResourceType physics_geom_type("physics_geometry");

	if (type == ParticleSystemResource::TYPE) {
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 5) * hit.dir;
		
		m_editor.beginCommandGroup("insert_particle");
		EntityRef entity = m_editor.addEntity();
		m_editor.setEntitiesPositions(&entity, &pos, 1);
		m_editor.addComponent(Span(&entity, 1), types::particle_emitter);
		m_editor.setProperty(types::particle_emitter, "", -1, "Source", Span(&entity, 1), Path(path));
		m_editor.endCommandGroup();
		m_editor.selectEntities(Span(&entity, 1), false);
	}
	else if (type == PrefabResource::TYPE) {
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
	else if (type == Model::TYPE) {
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 5) * hit.dir;

		m_editor.beginCommandGroup("insert_mesh");
		EntityRef entity = m_editor.addEntity();
		m_editor.selectEntities(Span(&entity, 1), false);
		m_editor.setEntitiesPositions(&entity, &pos, 1);
		m_editor.addComponent(Span(&entity, 1), types::model_instance);
		m_editor.setProperty(types::model_instance, "", -1, "Source", Span(&entity, 1), Path(path));
		m_editor.endCommandGroup();
	}
	else if (type == physics_geom_type) {
		if (hit.is_hit && hit.entity.isValid())
		{
			m_editor.beginCommandGroup("insert_phy_component");
			const EntityRef e = (EntityRef)hit.entity;
			m_editor.selectEntities(Span(&e, 1), false);
			m_editor.addComponent(Span(&e, 1), types::rigid_actor);
			m_editor.setProperty(types::rigid_actor, "", -1, "Mesh", Span(&e, 1), Path(path));
			m_editor.endCommandGroup();
		}
		else
		{
			const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;
			m_editor.beginCommandGroup("insert_phy");
			EntityRef entity = m_editor.addEntity();
			m_editor.setEntitiesPositions(&entity, &pos, 1);
			m_editor.selectEntities(Span(&entity, 1), false);
			m_editor.addComponent(Span(&entity, 1), types::rigid_actor);
			m_editor.setProperty(types::rigid_actor, "", -1, "Mesh", Span(&entity, 1), Path(path));
			m_editor.endCommandGroup();
		}
	}
}


void SceneView::onToolbar()
{
	struct {
		Action* action;
		bool selected;
	} actions[] = {
		{ &m_translate_gizmo_mode, false },
		{ &m_rotate_gizmo_mode, false },
		{ &m_scale_gizmo_mode, false },
		{ nullptr, false },
		{ &m_local_coord_gizmo, false },
		{ &m_global_coord_gizmo, false },
		{ nullptr, false },
		{ &m_use_grid_snapping_action, m_use_grid_snapping },
		{ nullptr, false },
		// &m_top_view_action,
		// &m_front_view_action,
		// &m_side_view_action,
	};

	auto pos = ImGui::GetCursorScreenPos();
	if (ImGuiEx::BeginToolbar("scene_view_toolbar", pos)) {
		for (auto& a : actions) {
			if (a.action) {
				a.action->toolbarButton(m_app.getBigIconFont(), a.selected);
			}
			else {
				ImGui::SameLine();
				ImGuiEx::VSeparator(3);
			}
		}
	}

	const ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_Text];
	const ImVec4 col_active = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];

	ImGui::SameLine();
	bool open_camera_transform = false;
	if(ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_CAMERA, bg_color, "Camera details")) {
		open_camera_transform = true;
	}

	ImGui::PushItemWidth(50);
	
	Action* mode_action;
	if (m_app.getGizmoConfig().isTranslateMode()) {
		mode_action = &m_translate_gizmo_mode;
	}
	else {
		mode_action = &m_rotate_gizmo_mode;
	}
	
	ImGui::SameLine();
	const ImVec4 measure_icon_color = m_is_measure_active ? col_active : ImGui::GetStyle().Colors[ImGuiCol_Text];
	if (ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_RULER_VERTICAL, measure_icon_color, "Measure")) {
		m_is_measure_active = !m_is_measure_active;
	}

	ImGui::SameLine();
	if (ImGuiEx::ToolbarButton(m_app.getBigIconFont(), ICON_FA_EYE, bg_color, "View")) ImGui::OpenPopup("Debug");

	if (m_game_view->m_game_view_merged_with_scene_view && m_editor.isGameMode()) {
		ImGui::SameLine();
		if (m_game_view->m_posses_game_camera.toolbarButton(m_app.getBigIconFont()) || m_app.checkShortcut(m_game_view->m_posses_game_camera, true)) {
			m_game_view->captureMouse(true);
		}
	}

	if (ImGui::BeginPopup("Debug")) {
		auto option = [&](const char* label, Pipeline::DebugShow value) {
			StaticString<64> tmp(label);
			char shortcut[32];
			if (m_debug_show_actions[(u32)value]->shortcutText(shortcut)) {
				tmp.append(" (", shortcut, ")");
			}
			if (ImGui::RadioButton(tmp, m_pipeline->m_debug_show == value)) {
				m_pipeline->m_debug_show = value;
				m_pipeline->m_debug_show_plugin = nullptr;
			}
		};
		option("No debug", Pipeline::DebugShow::NONE);
		option("Albedo", Pipeline::DebugShow::ALBEDO);
		option("Normal", Pipeline::DebugShow::NORMAL);
		option("Roughness", Pipeline::DebugShow::ROUGHNESS);
		option("Metallic", Pipeline::DebugShow::METALLIC);
		option("AO", Pipeline::DebugShow::AO);
		option("Velocity", Pipeline::DebugShow::VELOCITY);
		option("Light clusters", Pipeline::DebugShow::LIGHT_CLUSTERS);
		option("Probe clusters", Pipeline::DebugShow::PROBE_CLUSTERS);
		Renderer& renderer = m_pipeline->getRenderer();
		for (Lumix::RenderPlugin* plugin : renderer.getPlugins()) {
			plugin->debugUI(*m_pipeline);
		}
		ImGui::EndPopup();
	}

	ImGui::PopItemWidth();
	ImGuiEx::EndToolbar();

	if (open_camera_transform) ImGui::OpenPopup("Camera details");

	if (ImGui::BeginPopup("Camera details")) {
		ImGui::DragFloat("Speed", &m_camera_speed, 0.1f, 0.01f, 999.0f, "%.2f");
		ImGui::Checkbox("Camera preview", &m_show_camera_preview);
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
						m_app.getGizmoConfig().ungrab();
						m_editor.selectEntities(Span((const EntityRef*)nullptr, (u64)0), false);
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

void SceneView::insertModelUI() {
	if (m_insert_model_request) ImGui::OpenPopup("Insert model");

	ImVec2 pos = {(float)m_screen_x, (float)m_screen_y};
	ImVec2 size = {(float)m_width, (float)m_height};
	ImGui::SetNextWindowPos(pos + size * 0.2f);
	ImGui::SetNextWindowSize(size * 0.6f, ImGuiCond_Always);

	if (ImGui::BeginPopup("Insert model", ImGuiWindowFlags_NoNavInputs)) {
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

		if(m_insert_model_request) m_search_selected = 0;
		if (m_filter.gui("Search", -1, m_insert_model_request, nullptr, false)) {
			m_search_selected = 0;
		}
		bool scroll = false;
		const bool insert_enter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
		ImGui::Separator();
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
					m_editor.addComponent(Span(&entity, 1), types::model_instance);
					m_editor.setProperty(types::model_instance, "", -1, "Source", Span(&entity, 1), path);
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
						if (idx == 0 || ImGui::GetContentRegionAvail().x < ab.getThumbnailWidth()) ImGui::NewLine();
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

void SceneView::cameraPreviewGUI(Vec2 size) {
	if (!m_show_camera_preview) return;
	if (size.x <= 0) return;
	if (size.y <= 0) return;

	Span<const EntityRef> selected = m_editor.getSelectedEntities();
	if (selected.size() != 1) return;

	World* world = m_editor.getWorld();
	if (!world->hasComponent(selected[0], types::camera)) return;

	RenderModule* module = (RenderModule*)m_editor.getWorld()->getModule("renderer");
	Viewport vp = module->getCameraViewport(selected[0]);
	vp.w = (i32)size.x;
	vp.h = (i32)size.y;

	m_camera_preview_pipeline->setWorld(m_editor.getWorld());
	m_camera_preview_pipeline->setViewport(vp);
	m_camera_preview_pipeline->render(false);
	const gpu::TextureHandle texture_handle = m_camera_preview_pipeline->getOutput();

	if (!texture_handle) return;

	void* t = texture_handle;
	if (gpu::isOriginBottomLeft()) {
		ImGui::Image(t, size, ImVec2(0, 1), ImVec2(1, 0));
	} 
	else {
		ImGui::Image(t, size);
	}
}

void SceneView::snapDown() {
	WorldEditor& editor = m_app.getWorldEditor();
	Span<const EntityRef> selected = editor.getSelectedEntities();
	if (selected.size() == 0) return;

	Array<DVec3> new_positions(m_app.getAllocator());
	World* world = editor.getWorld();

	for (EntityRef entity : selected) {
		const DVec3 origin = world->getPosition(entity);
		auto hit = m_app.getRenderInterface()->castRay(*world, Ray{origin, Vec3(0, -1, 0)}, entity);
		if (hit.is_hit) {
			new_positions.push(origin + Vec3(0, -hit.t, 0));
		}
		else {
			hit = m_app.getRenderInterface()->castRay(*world, Ray{origin, Vec3(0, 1, 0)}, entity);
			if (hit.is_hit) {
				new_positions.push(origin + Vec3(0, hit.t, 0));
			}
			else {
				new_positions.push(world->getPosition(entity));
			}
		}
	}
	editor.setEntitiesPositions(&selected[0], &new_positions[0], new_positions.size());
}

static void selectParent(WorldEditor& editor) {
	Span<const EntityRef> selected = editor.getSelectedEntities();
	if (selected.size() != 1) return;

	const EntityPtr parent = editor.getWorld()->getParent(selected[0]);
	if (parent.isValid()) {
		EntityRef parent_ref = *parent;
		editor.selectEntities(Span(&parent_ref, 1), false);
	}
}

static void selectFirstChild(WorldEditor& editor) {
	Span<const EntityRef> selected = editor.getSelectedEntities();
	if (selected.size() != 1) return;

	const EntityPtr child = editor.getWorld()->getFirstChild(selected[0]);
	if (child.isValid()) {
		EntityRef child_ref = *child;
		editor.selectEntities(Span(&child_ref, 1), false);
	}
}

static void selectNextSibling(WorldEditor& editor) {
	Span<const EntityRef> selected = editor.getSelectedEntities();
	if (selected.size() != 1) return;

	const EntityPtr sibling = editor.getWorld()->getNextSibling(selected[0]);
	if (sibling.isValid()) {
		EntityRef sibling_ref = *sibling;
		editor.selectEntities(Span(&sibling_ref, 1), false);
	}
}

static void selectPrevSibling(WorldEditor& editor) {
	Span<const EntityRef> selected = editor.getSelectedEntities();
	if (selected.size() != 1) return;

	const World& world = *editor.getWorld();
	const EntityRef e = selected[0];
	const EntityPtr parent = world.getParent(e);
	if (!parent) return;

	EntityPtr child = world.getFirstChild(*parent);
	ASSERT(child);
	// we do not cycle, so if we are at the first child, we do not select the last one, we do nothing
	if (child == e) return;
	for (;;) {
		const EntityPtr next = world.getNextSibling(*child);
		if (next == e) {
			EntityRef child_ref = *child;
			editor.selectEntities(Span(&child_ref, 1), false);
			return;
		}
		child = next;
	}
}

void SceneView::onGUI() {
	const char* title = ICON_FA_GLOBE "Scene View###Scene View";
	if (m_log_ui.getUnreadErrorCount() > 0) title = ICON_FA_GLOBE "Scene View | " ICON_FA_EXCLAMATION_TRIANGLE " errors in log###Scene View";

	bool is_game_mode = m_editor.isGameMode();
	if (m_was_game_mode && !is_game_mode && m_game_view->m_game_view_merged_with_scene_view) {
		m_game_view->captureMouse(false);
	}
	if (m_game_view->m_game_view_merged_with_scene_view && is_game_mode) {
		if (!m_was_game_mode) {
			m_game_view->captureMouse(true);
		}
		m_was_game_mode = is_game_mode;
		if (m_game_view->isMouseCaptured()) {
			m_view->m_draw_vertices.clear();
			m_view->m_draw_cmds.clear();
			m_game_view->windowUI(title);
			return;
		}
	}
	m_was_game_mode = is_game_mode;
	PROFILE_FUNCTION();
	WorldEditor& editor = m_app.getWorldEditor();		
	if (m_is_mouse_captured && !m_app.isMouseCursorClipped()) captureMouse(false);
	if (m_app.checkShortcut(m_wireframe_action, true)) toggleWireframe();
	else if (m_app.checkShortcut(m_select_parent, true)) selectParent(editor);
	else if (m_app.checkShortcut(m_select_child, true)) selectFirstChild(editor);
	else if (m_app.checkShortcut(m_select_next_sibling, true)) selectNextSibling(editor);
	else if (m_app.checkShortcut(m_select_prev_sibling, true)) selectPrevSibling(editor);
	else if (m_app.checkShortcut(m_use_grid_snapping_action)) m_use_grid_snapping = !m_use_grid_snapping;

	m_pipeline->setWorld(m_editor.getWorld());
	bool is_open = false;
	ImVec2 view_pos;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImVec2 view_size;
	if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs)) {
		if (m_app.checkShortcut(m_top_view_action)) m_view->setTopView();
		else if (m_app.checkShortcut(m_side_view_action)) m_view->setSideView();
		else if (m_app.checkShortcut(m_front_view_action)) m_view->setFrontView();
		else if (m_app.checkShortcut(m_insert_model_action)) m_insert_model_request = true;
		else if (m_app.checkShortcut(m_set_pivot_action)) m_view->setCustomPivot();
		else if (m_app.checkShortcut(m_reset_pivot_action)) m_view->resetPivot();
		else if (m_app.checkShortcut(m_toggle_projection_action)) toggleProjection();
		else if (m_app.checkShortcut(m_look_at_selected_action)) m_view->lookAtSelected();
		else if (m_app.checkShortcut(m_copy_view_action)) m_view->copyTransform();
		else if (m_app.checkShortcut(m_rotate_entity_90_action)) rotate90Degrees();
		else if (m_app.checkShortcut(m_move_entity_E_action)) moveEntity(Vec2(-1, 0));
		else if (m_app.checkShortcut(m_move_entity_N_action)) moveEntity(Vec2(0, 1));
		else if (m_app.checkShortcut(m_move_entity_S_action)) moveEntity(Vec2(0, -1));
		else if (m_app.checkShortcut(m_move_entity_W_action)) moveEntity(Vec2(1, 0));
		else if (m_app.checkShortcut(m_local_coord_gizmo)) m_app.getGizmoConfig().coord_system = Gizmo::Config::LOCAL;
		else if (m_app.checkShortcut(m_global_coord_gizmo)) m_app.getGizmoConfig().coord_system = Gizmo::Config::GLOBAL;
		else if (m_app.checkShortcut(m_translate_gizmo_mode)) m_app.getGizmoConfig().mode = Gizmo::Config::TRANSLATE;
		else if (m_app.checkShortcut(m_rotate_gizmo_mode)) m_app.getGizmoConfig().mode = Gizmo::Config::ROTATE;
		else if (m_app.checkShortcut(m_scale_gizmo_mode)) m_app.getGizmoConfig().mode = Gizmo::Config::SCALE;
		else if (m_app.checkShortcut(m_grab_action)) m_app.getGizmoConfig().grab();
		else if (m_app.checkShortcut(m_grab_x)) m_app.getGizmoConfig().lockXAxis();
		else if (m_app.checkShortcut(m_grab_y)) m_app.getGizmoConfig().lockYAxis();
		else if (m_app.checkShortcut(m_grab_z)) m_app.getGizmoConfig().lockZAxis();
		else if (m_app.checkShortcut(m_snap_down)) snapDown();
		else if (m_app.checkShortcut(m_create_entity)) {
			const EntityRef e = editor.addEntity();
			editor.selectEntities(Span(&e, 1), false);
		} else if (m_app.checkShortcut(m_make_parent)) {
			const auto& entities = editor.getSelectedEntities();
			if (entities.size() == 2) editor.makeParent(entities[0], entities[1]);
		} else if (m_app.checkShortcut(m_unparent)) {
			const auto& entities = editor.getSelectedEntities();
			if (entities.size() != 1) return;
			editor.makeParent(INVALID_ENTITY, entities[0]);
		}
		else if (m_app.checkShortcut(m_autosnap_down)) {
			Gizmo::Config& cfg = m_app.getGizmoConfig();
			cfg.autosnap = !cfg.autosnap;
		}
		else {
			for (u32 i = 0; i < lengthOf(m_debug_show_actions); ++i) {
				if (m_app.checkShortcut(*m_debug_show_actions[i])) {
					m_pipeline->m_debug_show = (Pipeline::DebugShow)i;
					m_pipeline->m_debug_show_plugin = nullptr;
					break;
				}
			}
		}

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
		profiler::pushInt("Width", vp.w);
		profiler::pushInt("Height", vp.h);
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

			ImGui::SetCursorScreenPos(view_pos + view_size * 0.66f);
			cameraPreviewGUI(view_size * Vec2(0.33f) - Vec2(20.f));
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
