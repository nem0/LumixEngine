#include "scene_view.h"
#include "editor/asset_browser.h"
#include "editor/gizmo.h"
#include "editor/ieditor_command.h"
#include "editor/log_ui.h"
#include "editor/platform_interface.h"
#include "editor/prefab_system.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/input_system.h"
#include "engine/json_serializer.h"
#include "engine/path.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "engine/string.h"
#include "engine/universe/component.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "renderer/frame_buffer.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include <SDL.h>


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("renderable");
static const ResourceType MODEL_TYPE("model");
static const ResourceType SHADER_TYPE("shader");
static const ResourceType TEXTURE_TYPE("texture");


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
	Path path("pipelines/main.lua");
	m_pipeline = Pipeline::create(*renderer, path, "SCENE_VIEW", engine.getAllocator());
	m_pipeline->load();
	m_pipeline->addCustomCommandHandler("renderSelection").callback.bind<SceneView, &SceneView::renderSelection>(this);
	m_pipeline->addCustomCommandHandler("renderGizmos").callback.bind<SceneView, &SceneView::renderGizmos>(this);
	m_pipeline->addCustomCommandHandler("renderIcons").callback.bind<SceneView, &SceneView::renderIcons>(this);

	m_editor.universeCreated().bind<SceneView, &SceneView::onUniverseCreated>(this);
	m_editor.universeDestroyed().bind<SceneView, &SceneView::onUniverseDestroyed>(this);

	m_toggle_gizmo_step_action =
		LUMIX_NEW(allocator, Action)("Enable/disable gizmo step", "toggleGizmoStep");
	m_toggle_gizmo_step_action->is_global = false;
	m_app.addAction(m_toggle_gizmo_step_action);

	m_move_forward_action = LUMIX_NEW(allocator, Action)("Move forward", "moveForward");
	m_move_forward_action->is_global = false;
	m_app.addAction(m_move_forward_action);

	m_move_back_action = LUMIX_NEW(allocator, Action)("Move back", "moveBack");
	m_move_back_action->is_global = false;
	m_app.addAction(m_move_back_action);

	m_move_left_action = LUMIX_NEW(allocator, Action)("Move left", "moveLeft");
	m_move_left_action->is_global = false;
	m_app.addAction(m_move_left_action);

	m_move_right_action = LUMIX_NEW(allocator, Action)("Move right", "moveRight");
	m_move_right_action->is_global = false;
	m_app.addAction(m_move_right_action);

	m_move_up_action = LUMIX_NEW(allocator, Action)("Move up", "moveUp");
	m_move_up_action->is_global = false;
	m_app.addAction(m_move_up_action);

	m_move_down_action = LUMIX_NEW(allocator, Action)("Move down", "moveDown");
	m_move_down_action->is_global = false;
	m_app.addAction(m_move_down_action);

	m_camera_speed_action = LUMIX_NEW(allocator, Action)("Camera speed", "cameraSpeed");
	m_camera_speed_action->is_global = false;
	m_camera_speed_action->func.bind<SceneView, &SceneView::resetCameraSpeed>(this);
	m_app.addAction(m_camera_speed_action);

	m_app.getAssetBrowser().resourceChanged().bind<SceneView, &SceneView::onResourceChanged>(this);
}


void SceneView::onResourceChanged(const Path& path, const char* /*ext*/)
{
	if (getPipeline()->getPath() == path) getPipeline()->load();
}


void SceneView::resetCameraSpeed()
{
	m_camera_speed = 0.1f;
}


SceneView::~SceneView()
{
	m_app.getAssetBrowser().resourceChanged().unbind<SceneView, &SceneView::onResourceChanged>(this);
	m_editor.universeCreated().unbind<SceneView, &SceneView::onUniverseCreated>(this);
	m_editor.universeDestroyed().unbind<SceneView, &SceneView::onUniverseDestroyed>(this);
	Pipeline::destroy(m_pipeline);
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

	m_camera_speed = Math::maximum(0.01f, m_camera_speed + ImGui::GetIO().MouseWheel / 20.0f);

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
	m_editor.renderIcons();
}


void SceneView::renderSelection()
{
	const Array<Entity>& entities = m_editor.getSelectedEntities();
	RenderScene* scene = m_pipeline->getScene();
	Universe& universe = scene->getUniverse();
	for (Entity e : entities)
	{
		ComponentHandle cmp = scene->getModelInstanceComponent(e);
		if (!cmp.isValid()) continue;
		Model* model = scene->getModelInstanceModel(cmp);
		Matrix mtx = universe.getMatrix(e);
		if (model)
		{
			Pose* pose = scene->lockPose(cmp);
			m_pipeline->renderModel(*model, pose, mtx);
			scene->unlockPose(cmp, false);
		}
	}
}


void SceneView::renderGizmos()
{
	auto& entities = m_editor.getSelectedEntities();
	if(entities.empty() || entities[0] != m_editor.getEditCamera().entity)
		m_editor.getGizmo().render();
}


void SceneView::captureMouse(bool capture)
{
	if(m_is_mouse_captured == capture) return;
	m_is_mouse_captured = capture;
	SDL_ShowCursor(m_is_mouse_captured ? 0 : 1);
	SDL_SetRelativeMouseMode(capture ? SDL_TRUE : SDL_FALSE);
	if (capture) SDL_GetMouseState(&m_captured_mouse_x, &m_captured_mouse_y);
	else SDL_WarpMouseInWindow(nullptr, m_captured_mouse_x, m_captured_mouse_y);
}


RayCastModelHit SceneView::castRay(float x, float y)
{
	auto* scene =  m_pipeline->getScene();
	ASSERT(scene);
	
	ComponentUID camera_cmp = m_editor.getEditCamera();
	Vec2 screen_size = scene->getCameraScreenSize(camera_cmp.handle);
	screen_size.x *= x;
	screen_size.y *= y;

	Vec3 origin;
	Vec3 dir;
	scene->getRay(camera_cmp.handle, screen_size, origin, dir);
	return scene->castRay(origin, dir, INVALID_COMPONENT);
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

	if (hit.m_is_hit)
	{
		
		if (PathUtils::hasExtension(path, "fab"))
		{
		}
		else if (PathUtils::hasExtension(path, "msh"))
		{
			m_editor.beginCommandGroup(crc32("insert_mesh"));
			Entity entity = m_editor.addEntity();
			Vec3 pos = hit.m_origin + hit.m_t * hit.m_dir;
			m_editor.setEntitiesPositions(&entity, &pos, 1);
			m_editor.selectEntities(&entity, 1);
			m_editor.addComponent(MODEL_INSTANCE_TYPE);
			auto* prop = Reflection::getProperty(MODEL_INSTANCE_TYPE, "Source");
			m_editor.setProperty(MODEL_INSTANCE_TYPE, -1, *prop, &entity, 1, path, stringLength(path) + 1);
			m_editor.endCommandGroup();
		}
		else if (PathUtils::hasExtension(path, "mat") && hit.m_mesh)
		{
			m_editor.selectEntities(&hit.m_entity, 1);
			auto* model = m_pipeline->getScene()->getModelInstanceModel(hit.m_component);
			int mesh_index = 0;
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				if (&model->getMesh(i) == hit.m_mesh)
				{
					mesh_index = i;
					break;
				}
			}
			auto* prop= Reflection::getProperty(MODEL_INSTANCE_TYPE, "Materials", "Source");
			m_editor.setProperty(MODEL_INSTANCE_TYPE, mesh_index, *prop, &hit.m_entity, 1, path, stringLength(path) + 1);
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
	ImGui::Image(mode_action->icon, ImVec2(24, 24), ImVec2(0, 0), ImVec2(1, 1), tint_color);
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

	m_editor.inputFrame();

	if (ImGui::BeginDock(title, nullptr, ImGuiWindowFlags_NoScrollWithMouse))
	{
		m_is_open = true;
		onToolbar();
		auto size = ImGui::GetContentRegionAvail();
		m_texture_handle = m_pipeline->getRenderbuffer("default", 0);
		if (size.x > 0 && size.y > 0)
		{
			auto pos = ImGui::GetWindowPos();
			m_pipeline->resize(int(size.x), int(size.y));
			auto cursor_pos = ImGui::GetCursorScreenPos();
			m_screen_x = int(cursor_pos.x);
			m_screen_y = int(cursor_pos.y);
			m_width = int(size.x);
			m_height = int(size.y);
			auto content_min = ImGui::GetCursorScreenPos();
			ImVec2 content_max(content_min.x + size.x, content_min.y + size.y);
			if (bgfx::getCaps()->originBottomLeft)
			{
				ImGui::Image(&m_texture_handle, size, ImVec2(0, 1), ImVec2(1, 0));
			}
			else
			{
				ImGui::Image(&m_texture_handle, size);
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

			bool handle_input = ImGui::IsItemHovered();
			if(handle_input)
			{
				const SDL_Event* events = m_app.getEvents();
				for (int i = 0, c = m_app.getEventsCount(); i < c; ++i)
				{
					SDL_Event event = events[i];
					switch (event.type)
					{
						case SDL_MOUSEBUTTONDOWN:
							{
								ImGui::ResetActiveID();
								if (event.button.button == SDL_BUTTON_RIGHT) captureMouse(true);
								Vec2 rel_mp = { (float)event.button.x, (float)event.button.y };
								rel_mp.x -= m_screen_x;
								rel_mp.y -= m_screen_y;
								m_editor.onMouseDown((int)rel_mp.x, (int)rel_mp.y, (MouseButton::Value)event.button.button);
							}
							break;
						case SDL_MOUSEBUTTONUP:
							{
								if (event.button.button == SDL_BUTTON_RIGHT) captureMouse(false);
								Vec2 rel_mp = { (float)event.button.x, (float)event.button.y };
								rel_mp.x -= m_screen_x;
								rel_mp.y -= m_screen_y;
								m_editor.onMouseUp((int)rel_mp.x, (int)rel_mp.y, (MouseButton::Value)event.button.button);
							}
							break;
						case SDL_MOUSEMOTION:
							{
								Vec2 rel_mp = {(float)event.motion.x, (float)event.motion.y};
								rel_mp.x -= m_screen_x;
								rel_mp.y -= m_screen_y;
								m_editor.onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)event.motion.xrel, (int)event.motion.yrel);
							}
							break;
					}
				}
			}
			m_pipeline->render();
		}
	}

	ImGui::EndDock();

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
					ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
					ImGuiWindowFlags_ShowBorders))
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
	}
}


} // namespace Lumix
