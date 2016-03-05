#include "scene_view.h"
#include "core/crc32.h"
#include "core/input_system.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/string.h"
#include "editor/gizmo.h"
#include "editor/imgui/imgui.h"
#include "editor/log_ui.h"
#include "editor/platform_interface.h"
#include "editor/settings.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "renderer/frame_buffer.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"


SceneView::SceneView()
{
	m_pipeline = nullptr;
	m_editor = nullptr;
	m_camera_speed = 0.1f;
	m_is_mouse_captured = false;
	m_show_stats = false;
	m_log_ui = nullptr;
}


SceneView::~SceneView()
{
}


void SceneView::setWireframe(bool wireframe)
{
	m_pipeline->setWireframe(wireframe);
}


void SceneView::setScene(Lumix::RenderScene* scene)
{
	m_pipeline->setScene(scene);
}


void SceneView::shutdown()
{
	m_editor->universeCreated().unbind<SceneView, &SceneView::onUniverseCreated>(this);
	m_editor->universeDestroyed().unbind<SceneView, &SceneView::onUniverseDestroyed>(this);
	Lumix::Pipeline::destroy(m_pipeline);
	m_pipeline = nullptr;
}


void SceneView::onUniverseCreated()
{
	auto* scene = m_editor->getScene(Lumix::crc32("renderer"));
	m_pipeline->setScene(static_cast<Lumix::RenderScene*>(scene));
}


void SceneView::onUniverseDestroyed()
{
	m_pipeline->setScene(nullptr);
}


bool SceneView::init(LogUI& log_ui, Lumix::WorldEditor& editor, Lumix::Array<Action*>& actions)
{
	m_log_ui = &log_ui;
	m_editor = &editor;
	auto& engine = editor.getEngine();
	auto& allocator = engine.getAllocator();
	auto* renderer = static_cast<Lumix::Renderer*>(engine.getPluginManager().getPlugin("renderer"));

	Lumix::Path path("pipelines/main.lua");
	m_pipeline = Lumix::Pipeline::create(*renderer, path, engine.getAllocator());
	m_pipeline->load();
	m_pipeline->addCustomCommandHandler("renderGizmos")
		.callback.bind<SceneView, &SceneView::renderGizmos>(this);
	m_pipeline->addCustomCommandHandler("renderIcons")
		.callback.bind<SceneView, &SceneView::renderIcons>(this);

	editor.universeCreated().bind<SceneView, &SceneView::onUniverseCreated>(this);
	editor.universeDestroyed().bind<SceneView, &SceneView::onUniverseDestroyed>(this);
	onUniverseCreated();

	m_toggle_gizmo_step_action =
		LUMIX_NEW(editor.getAllocator(), Action)("Enable/disable gizmo step", "toggleGizmoStep");
	m_toggle_gizmo_step_action->is_global = false;

	actions.push(m_toggle_gizmo_step_action);

	return true;
}


void SceneView::update()
{
	PROFILE_FUNCTION();
	if (ImGui::IsAnyItemActive()) return;
	if (!m_is_opened) return;
	if (ImGui::GetIO().KeyCtrl) return;

	m_camera_speed =
		Lumix::Math::maxValue(0.01f, m_camera_speed + ImGui::GetIO().MouseWheel / 20.0f);

	int screen_x = int(ImGui::GetIO().MousePos.x);
	int screen_y = int(ImGui::GetIO().MousePos.y);
	bool is_inside = screen_x >= m_screen_x && screen_y >= m_screen_y &&
					 screen_x <= m_screen_x + m_width && screen_y <= m_screen_y + m_height;
	if (!is_inside) return;

	float speed = m_camera_speed;
	if (ImGui::GetIO().KeyShift)
	{
		speed *= 10;
	}
	if (ImGui::GetIO().KeysDown['W'])
	{
		m_editor->navigate(1.0f, 0, speed);
	}
	if (ImGui::GetIO().KeysDown['S'])
	{
		m_editor->navigate(-1.0f, 0, speed);
	}
	if (ImGui::GetIO().KeysDown['A'])
	{
		m_editor->navigate(0.0f, -1.0f, speed);
	}
	if (ImGui::GetIO().KeysDown['D'])
	{
		m_editor->navigate(0.0f, 1.0f, speed);
	}
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
	PlatformInterface::showCursor(!m_is_mouse_captured);
	if(!m_is_mouse_captured) PlatformInterface::unclipCursor();
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

	if (ImGui::BeginDock(title))
	{
		m_is_opened = true;
		auto size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();
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
			ImGui::Image(&m_texture_handle, size);
			view_pos = content_min;
			if (ImGui::IsItemHovered())
			{
				m_editor->getGizmo().enableStep(m_toggle_gizmo_step_action->isActive());
				auto rel_mp = ImGui::GetMousePos();
				rel_mp.x -= m_screen_x;
				rel_mp.y -= m_screen_y;
				for (int i = 0; i < 3; ++i)
				{
					if (ImGui::IsMouseClicked(i))
					{
						ImGui::ResetActiveID();
						captureMouse(true);
						m_editor->onMouseDown((int)rel_mp.x, (int)rel_mp.y, (Lumix::MouseButton::Value)i);
					}

					auto& input = m_editor->getEngine().getInputSystem();
					auto delta = Lumix::Vec2(input.getMouseXMove(), input.getMouseYMove());
					if(delta.x != 0 || delta.y != 0)
					{
						m_editor->onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)delta.x, (int)delta.y);
					}
				}
			}
			for (int i = 0; i < 3; ++i)
			{
				auto rel_mp = ImGui::GetMousePos();
				rel_mp.x -= m_screen_x;
				rel_mp.y -= m_screen_y;
				if (ImGui::IsMouseReleased(i))
				{
					captureMouse(false);
					m_editor->onMouseUp((int)rel_mp.x, (int)rel_mp.y, (Lumix::MouseButton::Value)i);
				}
			}
			if(m_is_mouse_captured)
			{
				PlatformInterface::clipCursor(
					content_min.x, content_min.y, content_max.x, content_max.y);
			}
			m_pipeline->render();
		}

		ImGui::PushItemWidth(60);
		ImGui::DragFloat("Camera speed", &m_camera_speed, 0.1f, 0.01f, 999.0f, "%.2f");
		ImGui::SameLine();
		if (m_editor->isMeasureToolActive())
		{
			ImGui::Text("| Measured distance: %f", m_editor->getMeasuredDistance());
		}

		ImGui::SameLine();
		int step = m_editor->getGizmo().getStep();
		if (ImGui::DragInt("Gizmo step", &step, 1.0f, 0, 200))
		{
			m_editor->getGizmo().setStep(step);
		}

		ImGui::SameLine();
		ImGui::Checkbox("Stats", &m_show_stats);

		ImGui::SameLine();
	}

	ImGui::EndDock();

	if(m_show_stats)
	{
		view_pos.x += ImGui::GetStyle().FramePadding.x;
		view_pos.y += ImGui::GetStyle().FramePadding.y;
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
			ImGui::LabelText("Draw calls", "%d", stats.m_draw_call_count);
			ImGui::LabelText("Instances", "%d", stats.m_instance_count);
			char buf[30];
			Lumix::toCStringPretty(stats.m_triangle_count, buf, Lumix::lengthOf(buf));
			ImGui::LabelText("Triangles", buf);
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
}