#include "scene_view.h"
#include "editor/gizmo.h"
#include "editor/ieditor_command.h"
#include "editor/log_ui.h"
#include "editor/platform_interface.h"
#include "editor/prefab_system.h"
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
#include "engine/property_register.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/universe/component.h"
#include "engine/universe/universe.h"
#include "imgui/imgui.h"
#include "renderer/frame_buffer.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include <SDL.h>


namespace Lumix
{


static const ComponentType MODEL_INSTANCE_TYPE = PropertyRegister::getComponentType("renderable");


SceneView::SceneView(StudioApp& app)
	: m_app(app)
{
	m_camera_speed = 0.1f;
	m_is_mouse_captured = false;
	m_show_stats = false;

	m_log_ui = m_app.getLogUI();
	m_editor = m_app.getWorldEditor();
	auto& engine = m_editor->getEngine();
	auto& allocator = engine.getAllocator();
	auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
	m_is_opengl = renderer->isOpenGL();
	Path path("pipelines/main.lua");
	m_pipeline = Pipeline::create(*renderer, path, engine.getAllocator());
	m_pipeline->load();
	m_pipeline->addCustomCommandHandler("renderGizmos").callback.bind<SceneView, &SceneView::renderGizmos>(this);
	m_pipeline->addCustomCommandHandler("renderIcons").callback.bind<SceneView, &SceneView::renderIcons>(this);

	m_editor->universeCreated().bind<SceneView, &SceneView::onUniverseCreated>(this);
	m_editor->universeDestroyed().bind<SceneView, &SceneView::onUniverseDestroyed>(this);

	m_toggle_gizmo_step_action =
		LUMIX_NEW(m_editor->getAllocator(), Action)("Enable/disable gizmo step", "toggleGizmoStep");
	m_toggle_gizmo_step_action->is_global = false;
	m_app.addAction(m_toggle_gizmo_step_action);

	m_move_forward_action = LUMIX_NEW(m_editor->getAllocator(), Action)("Move forward", "moveForward");
	m_move_forward_action->is_global = false;
	m_app.addAction(m_move_forward_action);

	m_move_back_action = LUMIX_NEW(m_editor->getAllocator(), Action)("Move back", "moveBack");
	m_move_back_action->is_global = false;
	m_app.addAction(m_move_back_action);

	m_move_left_action = LUMIX_NEW(m_editor->getAllocator(), Action)("Move left", "moveLeft");
	m_move_left_action->is_global = false;
	m_app.addAction(m_move_left_action);

	m_move_right_action = LUMIX_NEW(m_editor->getAllocator(), Action)("Move right", "moveRight");
	m_move_right_action->is_global = false;
	m_app.addAction(m_move_right_action);

	m_move_up_action = LUMIX_NEW(m_editor->getAllocator(), Action)("Move up", "moveUp");
	m_move_up_action->is_global = false;
	m_app.addAction(m_move_up_action);

	m_move_down_action = LUMIX_NEW(m_editor->getAllocator(), Action)("Move down", "moveDown");
	m_move_down_action->is_global = false;
	m_app.addAction(m_move_down_action);

	m_camera_speed_action = LUMIX_NEW(m_editor->getAllocator(), Action)("Camera speed", "cameraSpeed");
	m_camera_speed_action->is_global = false;
	m_camera_speed_action->func.bind<SceneView, &SceneView::resetCameraSpeed>(this);
	m_app.addAction(m_camera_speed_action);
}


void SceneView::resetCameraSpeed()
{
	m_camera_speed = 0.1f;
}


SceneView::~SceneView()
{
}


void SceneView::setScene(RenderScene* scene)
{
	m_pipeline->setScene(scene);
}


void SceneView::shutdown()
{
	m_editor->universeCreated().unbind<SceneView, &SceneView::onUniverseCreated>(this);
	m_editor->universeDestroyed().unbind<SceneView, &SceneView::onUniverseDestroyed>(this);
	Pipeline::destroy(m_pipeline);
	m_pipeline = nullptr;
}


void SceneView::onUniverseCreated()
{
	auto* scene = m_editor->getUniverse()->getScene(crc32("renderer"));
	m_pipeline->setScene(static_cast<RenderScene*>(scene));
}


void SceneView::onUniverseDestroyed()
{
	m_pipeline->setScene(nullptr);
}


void SceneView::update()
{
	PROFILE_FUNCTION();
	if (ImGui::IsAnyItemActive()) return;
	if (!m_is_opened) return;
	if (ImGui::GetIO().KeyCtrl) return;

	int screen_x = int(ImGui::GetIO().MousePos.x);
	int screen_y = int(ImGui::GetIO().MousePos.y);
	bool is_inside = screen_x >= m_screen_x && screen_y >= m_screen_y && screen_x <= m_screen_x + m_width &&
					 screen_y <= m_screen_y + m_height;
	if (!is_inside) return;

	m_camera_speed = Math::maximum(0.01f, m_camera_speed + ImGui::GetIO().MouseWheel / 20.0f);

	float speed = m_camera_speed;
	if (ImGui::GetIO().KeyShift) speed *= 10;
	if (m_move_forward_action->isActive()) m_editor->navigate(1.0f, 0, 0, speed);
	if (m_move_back_action->isActive()) m_editor->navigate(-1.0f, 0, 0, speed);
	if (m_move_left_action->isActive()) m_editor->navigate(0.0f, -1.0f, 0, speed);
	if (m_move_right_action->isActive()) m_editor->navigate(0.0f, 1.0f, 0, speed);
	if (m_move_down_action->isActive()) m_editor->navigate(0, 0, -1.0f, speed);
	if (m_move_up_action->isActive()) m_editor->navigate(0, 0, 1.0f, speed);
}


void SceneView::renderIcons()
{
	m_editor->renderIcons();
}


void SceneView::renderGizmos()
{
	m_editor->getGizmo().render();
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
	
	ComponentUID camera_cmp = m_editor->getEditCamera();
	Vec2 screen_size = scene->getCameraScreenSize(camera_cmp.handle);
	screen_size.x *= x;
	screen_size.y *= y;

	Vec3 origin;
	Vec3 dir;
	scene->getRay(camera_cmp.handle, (float)screen_size.x, (float)screen_size.y, origin, dir);
	return scene->castRay(origin, dir, INVALID_COMPONENT);
}


void SceneView::handleDrop(float x, float y)
{
	const char* path = (const char*)m_app.getDragData().data;
	auto hit = castRay(x, y);

	if (hit.m_is_hit)
	{
		if (PathUtils::hasExtension(path, "fab"))
		{
			
		}
		else if (PathUtils::hasExtension(path, "msh"))
		{
			m_editor->beginCommandGroup(crc32("insert_mesh"));
			Entity entity = m_editor->addEntity();
			Vec3 pos = hit.m_origin + hit.m_t * hit.m_dir;
			m_editor->setEntitiesPositions(&entity, &pos, 1);
			m_editor->selectEntities(&entity, 1);
			m_editor->addComponent(MODEL_INSTANCE_TYPE);
			const auto* desc = PropertyRegister::getDescriptor(MODEL_INSTANCE_TYPE, crc32("Source"));
			m_editor->setProperty(MODEL_INSTANCE_TYPE, -1, *desc, &entity, 1, path, stringLength(path) + 1);
			m_editor->endCommandGroup();
		}
		else if (PathUtils::hasExtension(path, "mat") && hit.m_mesh)
		{
			auto* desc = PropertyRegister::getDescriptor(MODEL_INSTANCE_TYPE, crc32("Material"));
			auto drag_data = m_app.getDragData();
			m_editor->selectEntities(&hit.m_entity, 1);
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
			
			m_editor->setProperty(MODEL_INSTANCE_TYPE, mesh_index, *desc, &hit.m_entity, 1, drag_data.data, drag_data.size);
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
	
	int step = m_editor->getGizmo().getStep();
	Action* mode_action;
	if (m_editor->getGizmo().isTranslateMode())
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
		m_editor->getGizmo().setStep(step);
	}

	ImGui::SameLine(0, 20);
	ImGui::Checkbox("Stats", &m_show_stats);

	ImGui::SameLine(0, 20);
	m_pipeline->callLuaFunction("onGUI");

	if (m_editor->isMeasureToolActive())
	{
		ImGui::SameLine(0, 20);
		ImGui::Text(" | Measured distance: %f", m_editor->getMeasuredDistance());
	}

	ImGui::PopItemWidth();

	ImGui::EndToolbar();
}


void SceneView::onGUI()
{
	PROFILE_FUNCTION();
	m_is_opened = false;
	ImVec2 view_pos;
	const char* title = "Scene View###Scene View";
	if (m_log_ui && m_log_ui->getUnreadErrorCount() > 0)
	{
		title = "Scene View | errors in log###Scene View";
	}

	m_editor->inputFrame();

	if (ImGui::BeginDock(title, nullptr, ImGuiWindowFlags_NoScrollWithMouse))
	{
		m_is_opened = true;
		onToolbar();
		auto size = ImGui::GetContentRegionAvail();
		auto* fb = m_pipeline->getFramebuffer("default");
		if (size.x > 0 && size.y > 0 && fb)
		{
			auto pos = ImGui::GetWindowPos();
			m_pipeline->setViewport(0, 0, int(size.x), int(size.y));
			m_texture_handle = fb->getRenderbufferHandle(0);
			auto cursor_pos = ImGui::GetCursorScreenPos();
			m_screen_x = int(cursor_pos.x);
			m_screen_y = int(cursor_pos.y);
			m_width = int(size.x);
			m_height = int(size.y);
			auto content_min = ImGui::GetCursorScreenPos();
			ImVec2 content_max(content_min.x + size.x, content_min.y + size.y);
			if (m_is_opengl)
			{
				ImGui::Image(&m_texture_handle, size, ImVec2(0, 1), ImVec2(1, 0));
			}
			else
			{
				ImGui::Image(&m_texture_handle, size);
			}
			if (ImGui::IsItemHoveredRect())
			{
				if (ImGui::IsMouseReleased(0) && m_app.getDragData().type == StudioApp::DragData::PATH)
				{
					float x = (ImGui::GetMousePos().x - content_min.x) / size.x;
					float y = (ImGui::GetMousePos().y - content_min.y) / size.y;
					handleDrop(x, y);
				}
			}
			view_pos = content_min;
			auto rel_mp = ImGui::GetMousePos();
			rel_mp.x -= m_screen_x;
			rel_mp.y -= m_screen_y;
			if (ImGui::IsItemHovered())
			{
				m_editor->getGizmo().enableStep(m_toggle_gizmo_step_action->isActive());
				for (int i = 0; i < 3; ++i)
				{
					if (ImGui::IsMouseClicked(i))
					{
						ImGui::ResetActiveID();
						if(i == 1) captureMouse(true);
						m_editor->onMouseDown((int)rel_mp.x, (int)rel_mp.y, (MouseButton::Value)i);
						break;
					}
				}
			}
			if (m_is_mouse_captured || ImGui::IsItemHovered())
			{
				auto& input = m_editor->getEngine().getInputSystem();
				auto delta = Vec2(input.getMouseXMove(), input.getMouseYMove());
				if (delta.x != 0 || delta.y != 0)
				{
					m_editor->onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)delta.x, (int)delta.y);
				}
			}
			for (int i = 0; i < 3; ++i)
			{
				auto rel_mp = ImGui::GetMousePos();
				rel_mp.x -= m_screen_x;
				rel_mp.y -= m_screen_y;
				if (ImGui::IsMouseReleased(i))
				{
					if (i == 1) captureMouse(false);
					m_editor->onMouseUp((int)rel_mp.x, (int)rel_mp.y, (MouseButton::Value)i);
				}
			}
			m_pipeline->render();
		}
	}

	ImGui::EndDock();

	if(m_show_stats && m_is_opened)
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
			const auto& stats = m_pipeline->getStats();
			ImGui::LabelText("Draw calls", "%d", stats.draw_call_count);
			ImGui::LabelText("Instances", "%d", stats.instance_count);
			char buf[30];
			toCStringPretty(stats.triangle_count, buf, lengthOf(buf));
			ImGui::LabelText("Triangles", "%s", buf);
			ImGui::LabelText("Resolution", "%dx%d", m_pipeline->getWidth(), m_pipeline->getHeight());
			ImGui::LabelText("FPS", "%.2f", m_editor->getEngine().getFPS());
			ImGui::LabelText("CPU time", "%.2f", m_pipeline->getCPUTime() * 1000.0f);
			ImGui::LabelText("GPU time", "%.2f", m_pipeline->getGPUTime() * 1000.0f);
			ImGui::LabelText("Waiting for submit", "%.2f", m_pipeline->getWaitSubmitTime() * 1000.0f);
			ImGui::LabelText("Waiting for render thread", "%.2f", m_pipeline->getGPUTime() * 1000.0f);
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
}


} // namespace Lumix
