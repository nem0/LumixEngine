#include <imgui/imgui.h>

#include "scene_view.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_icon.h"
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
#include "renderer/gpu/gpu.h"
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

SceneView::SceneView(StudioApp& app)
	: m_app(app)
	, m_drop_handlers(app.getAllocator())
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

	m_editor.universeCreated().bind<&SceneView::onUniverseCreated>(this);
	m_editor.universeDestroyed().bind<&SceneView::onUniverseDestroyed>(this);

	m_toggle_gizmo_step_action =
		LUMIX_NEW(allocator, Action)("Enable/disable gizmo step", "Enable/disable gizmo step", "toggleGizmoStep");
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

	m_camera_speed_action = LUMIX_NEW(allocator, Action)("Camera speed", "Reset camera speed", "cameraSpeed");
	m_camera_speed_action->is_global = false;
	m_camera_speed_action->func.bind<&SceneView::resetCameraSpeed>(this);
	m_app.addAction(m_camera_speed_action);

	const ResourceType pipeline_type("pipeline");
	m_app.getAssetCompiler().registerExtension("pln", pipeline_type); 
}


void SceneView::resetCameraSpeed()
{
	m_camera_speed = 0.1f;
}


SceneView::~SceneView()
{
	m_editor.universeCreated().unbind<&SceneView::onUniverseCreated>(this);
	m_editor.universeDestroyed().unbind<&SceneView::onUniverseDestroyed>(this);
	Pipeline::destroy(m_pipeline);
	m_debug_shape_shader->getResourceManager().unload(*m_debug_shape_shader);
	m_pipeline = nullptr;
}


void SceneView::setUniverse(Universe* universe)
{
	m_pipeline->setUniverse(universe);
}


void SceneView::onUniverseCreated()
{
	m_pipeline->setUniverse(m_editor.getUniverse());
}


void SceneView::onUniverseDestroyed()
{
	m_pipeline->setUniverse(nullptr);
}


void SceneView::update(float time_delta)
{
	PROFILE_FUNCTION();

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
	m_editor.getGizmo().enableStep(m_toggle_gizmo_step_action->isActive());
	if (m_move_forward_action->isActive()) m_editor.getView().moveCamera(1.0f, 0, 0, speed);
	if (m_move_back_action->isActive()) m_editor.getView().moveCamera(-1.0f, 0, 0, speed);
	if (m_move_left_action->isActive()) m_editor.getView().moveCamera(0.0f, -1.0f, 0, speed);
	if (m_move_right_action->isActive()) m_editor.getView().moveCamera(0.0f, 1.0f, 0, speed);
	if (m_move_down_action->isActive()) m_editor.getView().moveCamera(0, 0, -1.0f, speed);
	if (m_move_up_action->isActive()) m_editor.getView().moveCamera(0, 0, 1.0f, speed);
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
			m_view->m_editor.getIcons().getRenderData(&data);
			
			RenderInterfaceBase* ri = (RenderInterfaceBase*)m_view->m_editor.getRenderInterface();

			for (EditorIcons::RenderData& rd : data) {
				const Model* model = (Model*)ri->getModel(rd.model);
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
			const gpu::BufferHandle drawcall_ub = m_view->m_pipeline->getDrawcallUniformBuffer();

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
		SceneView* m_view;
	};

	Engine& engine = m_editor.getEngine();
	Renderer* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));

	IAllocator& allocator = renderer->getAllocator();
	RenderJob* cmd = LUMIX_NEW(allocator, RenderJob)(allocator);
	cmd->m_view = this;
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
					item.mtx = universe.getRelativeMatrix(e, m_editor->getView().getViewport().pos);
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
	};

	Engine& engine = m_editor.getEngine();
	Renderer* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
	IAllocator& allocator = renderer->getAllocator();
	RenderJob* job = LUMIX_NEW(allocator, RenderJob)(allocator);
	job->m_define_mask = 1 << renderer->getShaderDefineIdx("DEPTH");
	job->m_pipeline = m_pipeline;
	job->m_editor = &m_editor;
	renderer->queue(job, 0);
}


void SceneView::renderGizmos()
{
	struct Cmd : Renderer::RenderJob
	{
		Cmd(IAllocator& allocator)
			: data(allocator)
		{}

		void setup() override
		{
			PROFILE_FUNCTION();
			viewport = view->m_editor.getView().getViewport();
			view->m_editor.getGizmo().getRenderData(&data, viewport);
			Engine& engine = view->m_editor.getEngine();
			renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));

			ib = renderer->allocTransient(data.indices.byte_size());
			vb = renderer->allocTransient(data.vertices.byte_size());
			memcpy(ib.ptr, data.indices.begin(), data.indices.byte_size());
			memcpy(vb.ptr, data.vertices.begin(), data.vertices.byte_size());
		}

		void execute() override
		{
			PROFILE_FUNCTION();
			if (data.cmds.empty()) return;

			renderer->beginProfileBlock("gizmos", 0);
			gpu::pushDebugGroup("gizmos");
			gpu::setState(u64(gpu::StateFlags::DEPTH_TEST) | u64(gpu::StateFlags::DEPTH_WRITE));
			u32 vb_offset = 0;
			u32 ib_offset = 0;
			const gpu::BufferHandle drawcall_ub = view->getPipeline()->getDrawcallUniformBuffer();
			for (Gizmo::RenderData::Cmd& cmd : data.cmds) {
				gpu::update(drawcall_ub, &cmd.mtx.m11, sizeof(cmd.mtx));
				gpu::useProgram(program);
				gpu::bindIndexBuffer(ib.buffer);
				gpu::bindVertexBuffer(0, vb.buffer, vb.offset + vb_offset, 16);
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				const gpu::PrimitiveType primitive_type = cmd.lines ? gpu::PrimitiveType::LINES : gpu::PrimitiveType::TRIANGLES;
				gpu::drawElements(ib.offset + ib_offset, cmd.indices_count, primitive_type, gpu::DataType::U16);

				vb_offset += cmd.vertices_count * sizeof(Gizmo::RenderData::Vertex);
				ib_offset += cmd.indices_count * sizeof(u16);
			}
			gpu::popDebugGroup();
			
			renderer->endProfileBlock();
		}

		Renderer* renderer;
		Renderer::TransientSlice ib;
		Renderer::TransientSlice vb;
		Gizmo::RenderData data;
		Viewport viewport;
		SceneView* view;
		gpu::ProgramHandle program;
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
	
	const Viewport& vp = m_editor.getView().getViewport();
	DVec3 origin;
	Vec3 dir;
	vp.getRay({x * vp.w, y * vp.h}, origin, dir);
	return scene->castRay(origin, dir, INVALID_ENTITY);
}


void SceneView::addDropHandler(DropHandler handler)
{
	m_drop_handlers.push(handler);
}


void SceneView::removeDropHandler(DropHandler handler)
{
	m_drop_handlers.swapAndPopItem(handler);
}


void SceneView::handleDrop(const char* path, float x, float y)
{
	auto hit = castRay(x, y);

	for (DropHandler handler : m_drop_handlers)
	{
		if (handler.invoke(m_app, x, y, hit)) return;
	}

	if (Path::hasExtension(path, "fbx"))
	{
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;

		m_editor.beginCommandGroup(crc32("insert_mesh"));
		EntityRef entity = m_editor.addEntity();
		m_editor.setEntitiesPositions(&entity, &pos, 1);
		m_editor.addComponent(Span(&entity, 1), MODEL_INSTANCE_TYPE);
		m_editor.setProperty(MODEL_INSTANCE_TYPE, -1, "Source", Span(&entity, 1), Path(path));
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
			m_editor.selectEntities(&e, 1, false);
			m_editor.addComponent(Span(&e, 1), MESH_ACTOR_TYPE);
			m_editor.setProperty(MESH_ACTOR_TYPE, -1, "Source", Span(&e, 1), path);
			m_editor.endCommandGroup();
		}
		else
		{
			const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;
			m_editor.beginCommandGroup(crc32("insert_phy"));
			EntityRef entity = m_editor.addEntity();
			m_editor.setEntitiesPositions(&entity, &pos, 1);
			m_editor.selectEntities(&entity, 1, false);
			m_editor.addComponent(Span(&entity, 1), MESH_ACTOR_TYPE);
			m_editor.setProperty(MESH_ACTOR_TYPE, -1, "Source", Span(&entity, 1), path);
			m_editor.endCommandGroup();
		}
	}
}


void SceneView::onToolbar()
{
	static const char* actions_names[] = { "setTranslateGizmoMode",
		"setRotateGizmoMode",
		"setLocalCoordSystem",
		"setGlobalCoordSystem",
		"setPivotCenter",
		"setPivotOrigin",
		"viewTop",
		"viewFront",
		"viewSide" };

	auto pos = ImGui::GetCursorScreenPos();
	if (ImGui::BeginToolbar("scene_view_toolbar", pos, ImVec2(0, 24)))
	{
		for (auto* action_name : actions_names)
		{
			auto* action = m_app.getAction(action_name);
			action->toolbarButton();
		}
	}

	m_app.getAction("cameraSpeed")->toolbarButton();

	ImGui::PushItemWidth(50);
	ImGui::SameLine();
	float offset = (24 - ImGui::GetTextLineHeightWithSpacing()) / 2;
	pos = ImGui::GetCursorPos();
	pos.y += offset;
	ImGui::SetCursorPos(pos);
	ImGui::DragFloat("##camera_speed", &m_camera_speed, 0.1f, 0.01f, 999.0f, "%.2f");
	
	int step = m_editor.getGizmo().getStep();
	Action* mode_action;
	if (m_editor.getGizmo().isTranslateMode())
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
	const gpu::TextureHandle t = *(gpu::TextureHandle*)mode_action->icon;
	ImGui::Image((void*)(uintptr)t.value, ImVec2(24, 24), ImVec2(0, 0), ImVec2(1, 1), tint_color);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Snap amount");

	ImGui::SameLine();
	pos = ImGui::GetCursorPos();
	pos.y += offset;
	ImGui::SetCursorPos(pos);
	if (ImGui::DragInt("##gizmoStep", &step, 1.0f, 0, 200))
	{
		m_editor.getGizmo().setStep(step);
	}

	ImGui::SameLine(0, 20);
	ImGui::Checkbox("Stats", &m_show_stats);

	ImGui::SameLine(0, 20);
	m_pipeline->callLuaFunction("onGUI");

	if (m_editor.isMeasureToolActive())
	{
		ImGui::SameLine(0, 20);
		ImGui::Text(" | Measured distance: %f", m_editor.getMeasuredDistance());
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
						m_editor.getView().onMouseDown((int)rel_mp.x, (int)rel_mp.y, event.mouse_button.button);
					}
					else {
						m_editor.getView().onMouseUp((int)rel_mp.x, (int)rel_mp.y, event.mouse_button.button);
					}
				}
				else if (!event.mouse_button.down) {
					m_editor.getView().onMouseUp((int)rel_mp.x, (int)rel_mp.y, event.mouse_button.button);
				}
				break;
			}
			case OS::Event::Type::MOUSE_MOVE: 
				if (handle_input) {
					const OS::Point cp = OS::getMouseScreenPos();
					Vec2 rel_mp = {(float)cp.x, (float)cp.y};
					rel_mp.x -= m_screen_x;
					rel_mp.y -= m_screen_y;
					m_editor.getView().onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)event.mouse_move.xrel, (int)event.mouse_move.yrel);
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
	view_pos.y += ImGui::GetStyle().FramePadding.y + toolbar_height;
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
	bool is_open = false;
	ImVec2 view_pos;
	const char* title = "Scene View###Scene View";
	if (m_log_ui.getUnreadErrorCount() > 0) title = "Scene View | errors in log###Scene View";

	if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoScrollWithMouse)) {
		is_open = true;
		onToolbar();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		Viewport vp = m_editor.getView().getViewport();
		vp.w = (int)size.x;
		vp.h = (int)size.y;
		m_editor.getView().setViewport(vp);
		m_pipeline->setViewport(vp);
		m_pipeline->render(false);
		m_editor.getView().inputFrame();

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
					const ImVec2 drop_pos = ImGui::GetMousePos() - view_pos / size;
					handleDrop((const char*)payload->Data, drop_pos.x, drop_pos.y);
				}
				ImGui::EndDragDropTarget();
			}

			handleEvents();
		}
	}
	else {
		m_editor.getView().inputFrame();
	}

	if (m_is_mouse_captured && OS::getFocused() != ImGui::GetWindowViewport()->PlatformHandle) {
		captureMouse(false);
	}
	ImGui::End();

	if (is_open) statsUI(view_pos.x, view_pos.y);
}


} // namespace Lumix
