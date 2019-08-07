#include "scene_view.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_icon.h"
#include "editor/gizmo.h"
#include "editor/log_ui.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "engine/crc32.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/universe/component.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "renderer/ffr/ffr.h"
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
	, m_drop_handlers(app.getWorldEditor().getAllocator())
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
	m_pipeline->addCustomCommandHandler("renderSelection").callback.bind<SceneView, &SceneView::renderSelection>(this);
	m_pipeline->addCustomCommandHandler("renderGizmos").callback.bind<SceneView, &SceneView::renderGizmos>(this);
	m_pipeline->addCustomCommandHandler("renderIcons").callback.bind<SceneView, &SceneView::renderIcons>(this);

	ResourceManagerHub& rm = engine.getResourceManager();
	m_debug_shape_shader = rm.load<Shader>(Path("pipelines/debug_shape.shd"));

	m_editor.universeCreated().bind<SceneView, &SceneView::onUniverseCreated>(this);
	m_editor.universeDestroyed().bind<SceneView, &SceneView::onUniverseDestroyed>(this);

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
	m_camera_speed_action->func.bind<SceneView, &SceneView::resetCameraSpeed>(this);
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
	m_editor.universeCreated().unbind<SceneView, &SceneView::onUniverseCreated>(this);
	m_editor.universeDestroyed().unbind<SceneView, &SceneView::onUniverseDestroyed>(this);
	Pipeline::destroy(m_pipeline);
	m_debug_shape_shader->getResourceManager().unload(*m_debug_shape_shader);
	m_pipeline = nullptr;
}


void SceneView::setScene(RenderScene* scene)
{
	m_pipeline->setScene(scene);
}


void SceneView::onUniverseCreated()
{
	IScene* scene = m_editor.getUniverse()->getScene(crc32("renderer"));
	m_pipeline->setScene((RenderScene*)scene);
}


void SceneView::onUniverseDestroyed()
{
	m_pipeline->setScene(nullptr);
}


void SceneView::update(float)
{
	PROFILE_FUNCTION();

	if (ImGui::IsAnyItemActive()) return;
	if (!m_is_open) return;
	if (ImGui::GetIO().KeyCtrl) return;

	int screen_x = int(ImGui::GetIO().MousePos.x);
	int screen_y = int(ImGui::GetIO().MousePos.y);
	bool is_inside = screen_x >= m_screen_x && screen_y >= m_screen_y && screen_x <= m_screen_x + m_width &&
					 screen_y <= m_screen_y + m_height;
	if (!is_inside) return;

	m_camera_speed = maximum(0.01f, m_camera_speed + ImGui::GetIO().MouseWheel / 20.0f);

	float speed = m_camera_speed;
	if (ImGui::GetIO().KeyShift) speed *= 10;
	m_editor.getGizmo().enableStep(m_toggle_gizmo_step_action->isActive());
	if (m_move_forward_action->isActive()) m_editor.navigate(1.0f, 0, 0, speed);
	if (m_move_back_action->isActive()) m_editor.navigate(-1.0f, 0, 0, speed);
	if (m_move_left_action->isActive()) m_editor.navigate(0.0f, -1.0f, 0, speed);
	if (m_move_right_action->isActive()) m_editor.navigate(0.0f, 1.0f, 0, speed);
	if (m_move_down_action->isActive()) m_editor.navigate(0, 0, -1.0f, speed);
	if (m_move_up_action->isActive()) m_editor.navigate(0, 0, 1.0f, speed);
}


void SceneView::renderIcons()
{
	struct Cmd : Renderer::RenderJob
	{
		Cmd(IAllocator& allocator)
			: data(allocator)
		{}

		void setup() override
		{
			PROFILE_FUNCTION();
			view->m_editor.getIcons().getRenderData(&data);
		}

		void execute() override
		{
			PROFILE_FUNCTION();
			ffr::pushDebugGroup("icons");
			RenderInterface* ri = view->m_editor.getRenderInterface();
			
			for(const EditorIcons::RenderData& i : data) {
				ri->renderModel(i.model, i.mtx);
			}
			ffr::popDebugGroup();
		}

		Array<EditorIcons::RenderData> data;
		SceneView* view;
	};

	Engine& engine = m_editor.getEngine();
	Renderer* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));

	IAllocator& allocator = renderer->getAllocator();
	Cmd* cmd = LUMIX_NEW(allocator, Cmd)(allocator);
	cmd->view = this;
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
			m_mtx_uniform = ffr::allocUniform("u_model", ffr::UniformType::MAT4, 1);
			const Universe& universe = scene->getUniverse();
			for (EntityRef e : entities) {
				if (!scene->getUniverse().hasComponent(e, MODEL_INSTANCE_TYPE)) continue;

				const Model* model = scene->getModelInstanceModel(e);
				if (!model || !model->isReady()) continue;

				for (int i = 0; i < model->getMeshCount(); ++i) {
					const Mesh& mesh = model->getMesh(i);
					Item& item = m_items.emplace();
					item.mesh = mesh.render_data;
					item.shader = mesh.material->getShader()->m_render_data;
					item.mtx = universe.getRelativeMatrix(e, m_editor->getViewport().pos);
					item.material_render_states = mesh.material->getRenderStates();
				}
			}
		}

		void execute() override
		{
			PROFILE_FUNCTION();
			for (const Item& item : m_items) {
				const Mesh::RenderData* rd = item.mesh;
				const ffr::ProgramHandle prog = Shader::getProgram(item.shader, rd->vertex_decl, 0); // TODO define

				if (!prog.isValid()) continue;
			
				ffr::setUniformMatrix4f(m_mtx_uniform, &item.mtx.m11);
				ffr::useProgram(prog);
				ffr::bindVertexBuffer(0, rd->vertex_buffer_handle, 0, rd->vb_stride);
				ffr::bindIndexBuffer(rd->index_buffer_handle);
				ffr::setState(u64(ffr::StateFlags::DEPTH_TEST) | u64(ffr::StateFlags::DEPTH_WRITE) | item.material_render_states);
				ffr::drawTriangles(rd->indices_count, rd->index_type);
			}
		}

		struct Item {
			ShaderRenderData* shader;
			Mesh::RenderData* mesh;
			u64 material_render_states;
			Matrix mtx;
		};

		Array<Item> m_items;
		Pipeline* m_pipeline;
		ffr::UniformHandle m_mtx_uniform;
		WorldEditor* m_editor;
	};

	Engine& engine = m_editor.getEngine();
	Renderer* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
	IAllocator& allocator = renderer->getAllocator();
	RenderJob* job = LUMIX_NEW(allocator, RenderJob)(allocator);
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
			viewport = view->m_editor.getViewport();
			view->m_editor.getGizmo().getRenderData(&data, viewport);
			Engine& engine = view->m_editor.getEngine();
			renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
			model_uniform = ffr::allocUniform("u_model", ffr::UniformType::MAT4, 1);

			ib = renderer->allocTransient(data.indices.byte_size());
			vb = renderer->allocTransient(data.vertices.byte_size());
			memcpy(ib.ptr, data.indices.begin(), data.indices.byte_size());
			memcpy(vb.ptr, data.vertices.begin(), data.vertices.byte_size());
		}

		void execute() override
		{
			PROFILE_FUNCTION();
			if (data.cmds.empty()) return;

			ffr::VertexDecl decl;
			decl.addAttribute(0, 0, 3, ffr::AttributeType::FLOAT, 0);
			decl.addAttribute(1, 12, 4, ffr::AttributeType::U8, ffr::Attribute::NORMALIZED);
			
			const ffr::ProgramHandle prg = Shader::getProgram(shader, decl, 0);
			if (!prg.isValid()) return;

			renderer->beginProfileBlock("gizmos", 0);
			ffr::pushDebugGroup("gizmos");
			ffr::setState(u64(ffr::StateFlags::DEPTH_TEST) | u64(ffr::StateFlags::DEPTH_WRITE));
			u32 vb_offset = 0;
			u32 ib_offset = 0;
			for (Gizmo::RenderData::Cmd& cmd : data.cmds) {
				ffr::setUniformMatrix4f(model_uniform, &cmd.mtx.m11);
				ffr::useProgram(prg);
				ffr::bindVertexBuffer(0, vb.buffer, vb.offset + vb_offset, 16);
				ffr::bindIndexBuffer(ib.buffer);
				const ffr::PrimitiveType primitive_type = cmd.lines ? ffr::PrimitiveType::LINES : ffr::PrimitiveType::TRIANGLES;
				ffr::drawElements((ib.offset + ib_offset) / sizeof(u16), cmd.indices_count, primitive_type, ffr::DataType::U16);

				vb_offset += cmd.vertices_count * sizeof(Gizmo::RenderData::Vertex);
				ib_offset += cmd.indices_count * sizeof(u16);
			}
			ffr::popDebugGroup();
			
			renderer->endProfileBlock();
		}

		Renderer* renderer;
		Renderer::TransientSlice ib;
		Renderer::TransientSlice vb;
		Gizmo::RenderData data;
		Viewport viewport;
		SceneView* view;
		ShaderRenderData* shader;
		ffr::UniformHandle model_uniform;
	};

	if (!m_debug_shape_shader || !m_debug_shape_shader->isReady()) return;

	Engine& engine = m_editor.getEngine();
	Renderer* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));

	IAllocator& allocator = renderer->getAllocator();
	Cmd* cmd = LUMIX_NEW(allocator, Cmd)(allocator);
	cmd->shader = m_debug_shape_shader->m_render_data;
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
	
	const Viewport& vp = m_editor.getViewport();
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
	m_drop_handlers.eraseItemFast(handler);
}


void SceneView::handleDrop(const char* path, float x, float y)
{
	auto hit = castRay(x, y);

	for (DropHandler handler : m_drop_handlers)
	{
		if (handler.invoke(m_app, x, y, hit)) return;
	}

	if (PathUtils::hasExtension(path, "fbx"))
	{
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;

		m_editor.beginCommandGroup(crc32("insert_mesh"));
		EntityRef entity = m_editor.addEntity();
		m_editor.setEntitiesPositions(&entity, &pos, 1);
		m_editor.selectEntities(&entity, 1, false);
		m_editor.addComponent(MODEL_INSTANCE_TYPE);
		auto* prop = Reflection::getProperty(MODEL_INSTANCE_TYPE, "Source");
		m_editor.setProperty(MODEL_INSTANCE_TYPE, -1, *prop, &entity, 1, path, stringLength(path) + 1);
		m_editor.endCommandGroup();
	}
	else if (PathUtils::hasExtension(path, "fab"))
	{
		ResourceManagerHub& manager = m_editor.getEngine().getResourceManager();
		PrefabResource* prefab = manager.load<PrefabResource>(Path(path));
		const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;
		m_editor.getPrefabSystem().instantiatePrefab(*prefab, pos, Quat::IDENTITY, 1);
	}
	else if (PathUtils::hasExtension(path, "phy"))
	{
		if (hit.is_hit && hit.entity.isValid())
		{
			m_editor.beginCommandGroup(crc32("insert_phy_component"));
			const EntityRef e = (EntityRef)hit.entity;
			m_editor.selectEntities(&e, 1, false);
			m_editor.addComponent(MESH_ACTOR_TYPE);
			auto* prop = Reflection::getProperty(MESH_ACTOR_TYPE, "Source");
			m_editor.setProperty(MESH_ACTOR_TYPE, -1, *prop, &e, 1, path, stringLength(path) + 1);
			m_editor.endCommandGroup();
		}
		else
		{
			const DVec3 pos = hit.origin + (hit.is_hit ? hit.t : 1) * hit.dir;
			m_editor.beginCommandGroup(crc32("insert_phy"));
			EntityRef entity = m_editor.addEntity();
			m_editor.setEntitiesPositions(&entity, &pos, 1);
			m_editor.selectEntities(&entity, 1, false);
			m_editor.addComponent(MESH_ACTOR_TYPE);
			auto* prop = Reflection::getProperty(MESH_ACTOR_TYPE, "Source");
			m_editor.setProperty(MESH_ACTOR_TYPE, -1, *prop, &entity, 1, path, stringLength(path) + 1);
			m_editor.endCommandGroup();
		}
	}
	else if (hit.is_hit && PathUtils::hasExtension(path, "mat") && hit.mesh)
	{
		const EntityRef e = (EntityRef)hit.entity;
		m_editor.selectEntities(&e, 1, false);
		RenderScene* scene = m_pipeline->getScene();
		Model* model = scene->getModelInstanceModel(e);
		int mesh_index = 0;
		for (int i = 0; i < model->getMeshCount(); ++i)
		{
			if (&model->getMesh(i) == hit.mesh)
			{
				mesh_index = i;
				break;
			}
		}
		auto* prop= Reflection::getProperty(MODEL_INSTANCE_TYPE, "Materials", "Source");
		m_editor.setProperty(MODEL_INSTANCE_TYPE, mesh_index, *prop, &e, 1, path, stringLength(path) + 1);
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
	const ffr::TextureHandle t = *(ffr::TextureHandle*)mode_action->icon;
	ImGui::Image((void*)(uintptr_t)t.value, ImVec2(24, 24), ImVec2(0, 0), ImVec2(1, 1), tint_color);
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


void SceneView::onWindowGUI()
{
	PROFILE_FUNCTION();
	m_is_open = false;
	ImVec2 view_pos;
	const char* title = "Scene View###Scene View";
	if (m_log_ui.getUnreadErrorCount() > 0)
	{
		title = "Scene View | errors in log###Scene View";
	}

	if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoScrollWithMouse))
	{
		m_is_open = true;
		onToolbar();
		auto size = ImGui::GetContentRegionAvail();
		Viewport vp = m_editor.getViewport();
		vp.w = (int)size.x;
		vp.h = (int)size.y;
		m_editor.setViewport(vp);
		m_pipeline->setViewport(vp);
		m_pipeline->render(false);
		m_editor.inputFrame();

		m_texture_handle = m_pipeline->getOutput();
		if (size.x > 0 && size.y > 0)
		{
			auto cursor_pos = ImGui::GetCursorScreenPos();
			m_screen_x = int(cursor_pos.x);
			m_screen_y = int(cursor_pos.y);
			m_width = int(size.x);
			m_height = int(size.y);
			auto content_min = ImGui::GetCursorScreenPos();
			if(m_texture_handle.isValid()) {
				void* t = (void*)(uintptr_t)m_texture_handle.value;
				if (ffr::isOriginBottomLeft()) {
					ImGui::Image(t, size, ImVec2(0, 1), ImVec2(1, 0));
				} 
				else {
					ImGui::Image(t, size);
				}
			}

			if (m_is_mouse_captured) {
				ImVec2 pos = ImGui::GetItemRectMin();
				ImVec2 size = ImGui::GetItemRectSize();
				OS::clipCursor(m_app.getWindow(), (int)pos.x, (int)pos.y, (int)size.x, (int)size.y);
			}

			if (ImGui::BeginDragDropTarget())
			{
				if (auto* payload = ImGui::AcceptDragDropPayload("path"))
				{
					float x = (ImGui::GetMousePos().x - content_min.x) / size.x;
					float y = (ImGui::GetMousePos().y - content_min.y) / size.y;
					handleDrop((const char*)payload->Data, x, y);
				}
				ImGui::EndDragDropTarget();
			}
			view_pos = content_min;

			const bool h = ImGui::IsItemHovered();
			const bool handle_input = ImGui::IsItemHovered() && OS::getFocused() == m_app.getWindow();
			const OS::Event* events = m_app.getEvents();
			for (int i = 0, c = m_app.getEventsCount(); i < c; ++i) {
				const OS::Event& event = events[i];
				switch (event.type) {
					case OS::Event::Type::MOUSE_BUTTON: {
						if (event.mouse_button.button == OS::MouseButton::RIGHT && handle_input) {
							ImGui::SetWindowFocus();
							captureMouse(event.mouse_button.down);
						}
						if (handle_input) {
							ImGui::ResetActiveID();
							const OS::Point cp = OS::getMousePos(event.window);
							Vec2 rel_mp = { (float)cp.x, (float)cp.y };
							rel_mp.x -= m_screen_x;
							rel_mp.y -= m_screen_y;
							if (event.mouse_button.down) {
								m_editor.onMouseDown((int)rel_mp.x, (int)rel_mp.y, event.mouse_button.button);
							}
							else {
								m_editor.onMouseUp((int)rel_mp.x, (int)rel_mp.y, event.mouse_button.button);
							}
						}
						break;
					}
					case OS::Event::Type::MOUSE_MOVE: 
						if (handle_input) {
							const OS::Point cp = OS::getMousePos(event.window);
							Vec2 rel_mp = {(float)cp.x, (float)cp.y};
							rel_mp.x -= m_screen_x;
							rel_mp.y -= m_screen_y;
							m_editor.onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)event.mouse_move.xrel, (int)event.mouse_move.yrel);
						}
						break;
				}
			}
		}
	}
	else {
		m_editor.inputFrame();
	}

	ImGui::End();

			// TODO
			/*
	if(m_show_stats && m_is_open)
	{
		float toolbar_height = 24 + ImGui::GetStyle().FramePadding.y * 2;
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
			const bgfx::Stats* bgfx_stats = bgfx::getStats();
			const auto& stats = m_pipeline->getStats();
			ImGui::LabelText("Draw calls (scene view only)", "%d", stats.draw_call_count);
			ImGui::LabelText("Instances (scene view only)", "%d", stats.instance_count);
			char buf[30];
			toCStringPretty(stats.triangle_count, buf, lengthOf(buf));
			ImGui::LabelText("Triangles (scene view only)", "%s", buf);
			ImGui::LabelText("GPU memory used", "%dMB", int(bgfx_stats->gpuMemoryUsed / (1024 * 1024)));
			ImGui::LabelText("Resolution", "%dx%d", m_pipeline->getWidth(), m_pipeline->getHeight());
			ImGui::LabelText("FPS", "%.2f", m_editor.getEngine().getFPS());
			double cpu_time = 1000 * bgfx_stats->cpuTimeFrame / (double)bgfx_stats->cpuTimerFreq;
			double gpu_time = 1000 * (bgfx_stats->gpuTimeEnd - bgfx_stats->gpuTimeBegin) / (double)bgfx_stats->gpuTimerFreq;
			double wait_submit_time = 1000 * bgfx_stats->waitSubmit / (double)bgfx_stats->cpuTimerFreq;
			double wait_render_time = 1000 * bgfx_stats->waitRender / (double)bgfx_stats->cpuTimerFreq;
			ImGui::LabelText("CPU time", "%.2f", cpu_time);
			ImGui::LabelText("GPU time", "%.2f", gpu_time);
			ImGui::LabelText("Waiting for submit", "%.2f", wait_submit_time);
			ImGui::LabelText("Waiting for render thread", "%.2f", wait_render_time);
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}*/
}


} // namespace Lumix
