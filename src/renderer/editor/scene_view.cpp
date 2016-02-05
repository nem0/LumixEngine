#include "scene_view.h"
#include "core/crc32.h"
#include "core/input_system.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/string.h"
#include "editor/gizmo.h"
#include "editor/imgui/imgui.h"
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
	m_current_pipeline = nullptr;
	m_deferred_pipeline = nullptr;
	m_forward_pipeline = nullptr;
	m_editor = nullptr;
	m_camera_speed = 0.1f;
	m_is_pipeline_switch = false;
	m_is_mouse_captured = false;
	m_show_stats = false;
}


SceneView::~SceneView()
{
}


void SceneView::setWireframe(bool wireframe)
{
	m_forward_pipeline->setWireframe(wireframe);
	m_deferred_pipeline->setWireframe(wireframe);
}


void SceneView::setScene(Lumix::RenderScene* scene)
{
	m_deferred_pipeline->setScene(scene);
	m_forward_pipeline->setScene(scene);
}


void SceneView::shutdown()
{
	m_editor->universeCreated().unbind<SceneView, &SceneView::onUniverseCreated>(this);
	m_editor->universeDestroyed().unbind<SceneView, &SceneView::onUniverseDestroyed>(this);
	Lumix::Pipeline::destroy(m_forward_pipeline);
	Lumix::Pipeline::destroy(m_deferred_pipeline);
	m_current_pipeline = nullptr;
	m_deferred_pipeline = nullptr;
	m_forward_pipeline = nullptr;
}


void SceneView::onUniverseCreated()
{
	auto* scene = m_editor->getScene(Lumix::crc32("renderer"));
	m_forward_pipeline->setScene(static_cast<Lumix::RenderScene*>(scene));
	m_deferred_pipeline->setScene(static_cast<Lumix::RenderScene*>(scene));
	auto* settings = Settings::getInstance();
	if (!settings) return;

	int count = m_forward_pipeline->getParameterCount();
	for (int i = 0; i < count; ++i)
	{
		bool b = settings->getValue(m_forward_pipeline->getParameterName(i), m_forward_pipeline->getParameter(i));
		m_forward_pipeline->setParameter(i, b);
	}
	
	count = m_deferred_pipeline->getParameterCount();
	for (int i = 0; i < count; ++i)
	{
		bool b = settings->getValue(m_deferred_pipeline->getParameterName(i), m_deferred_pipeline->getParameter(i));
		m_deferred_pipeline->setParameter(i, b);
	}
}


void SceneView::onUniverseDestroyed()
{
	m_forward_pipeline->setScene(nullptr);
	m_deferred_pipeline->setScene(nullptr);
}


bool SceneView::init(Lumix::WorldEditor& editor, Lumix::Array<Action*>& actions)
{
	m_editor = &editor;
	auto& engine = editor.getEngine();
	auto& allocator = engine.getAllocator();
	auto* renderer = static_cast<Lumix::Renderer*>(engine.getPluginManager().getPlugin("renderer"));

	Lumix::Path path("pipelines/main.lua");
	m_forward_pipeline = Lumix::Pipeline::create(*renderer, path, engine.getAllocator());
	m_forward_pipeline->load();
	m_forward_pipeline->addCustomCommandHandler("renderGizmos")
		.callback.bind<SceneView, &SceneView::renderGizmos>(this);
	m_forward_pipeline->addCustomCommandHandler("renderIcons")
		.callback.bind<SceneView, &SceneView::renderIcons>(this);

	path = "pipelines/deferred_main.lua";
	m_deferred_pipeline = Lumix::Pipeline::create(*renderer, path, engine.getAllocator());
	m_deferred_pipeline->load();
	m_deferred_pipeline->addCustomCommandHandler("renderGizmos")
		.callback.bind<SceneView, &SceneView::renderGizmos>(this);
	m_deferred_pipeline->addCustomCommandHandler("renderIcons")
		.callback.bind<SceneView, &SceneView::renderIcons>(this);

	m_current_pipeline = m_forward_pipeline;

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
	if (m_is_pipeline_switch)
	{
		bool is_deferred = m_current_pipeline == m_deferred_pipeline;
		m_current_pipeline = is_deferred ? m_forward_pipeline : m_deferred_pipeline;
	}

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
	if (ImGui::BeginDock("Scene View"))
	{
		m_is_opened = true;
		auto size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();
		auto* fb = m_current_pipeline->getFramebuffer("default");
		if (size.x > 0 && size.y > 0 && fb)
		{
			auto pos = ImGui::GetWindowPos();
			m_current_pipeline->setViewport(0, 0, int(size.x), int(size.y));
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
				m_editor->setGizmoUseStep(m_toggle_gizmo_step_action->isActive());
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
					if (ImGui::IsMouseReleased(i))
					{
						captureMouse(false);
						m_editor->onMouseUp((int)rel_mp.x, (int)rel_mp.y, (Lumix::MouseButton::Value)i);
					}

					auto& input = m_editor->getEngine().getInputSystem();
					auto delta = Lumix::Vec2(input.getMouseXMove(), input.getMouseYMove());
					if(delta.x != 0 || delta.y != 0)
					{
						m_editor->onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)delta.x, (int)delta.y);
					}
				}
			}
			if(m_is_mouse_captured)
			{
				PlatformInterface::clipCursor(
					content_min.x, content_min.y, content_max.x, content_max.y);
			}
			m_current_pipeline->render();
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

		int count = m_current_pipeline->getParameterCount();
		if (count)
		{
			ImGui::SameLine();
			if (ImGui::Button("Pipeline"))
			{
				ImGui::OpenPopup("pipeline_parameters_popup");
			}

			if (ImGui::BeginPopup("pipeline_parameters_popup"))
			{
				for (int i = 0; i < count; ++i)
				{
					bool b = m_current_pipeline->getParameter(i);
					if (ImGui::Checkbox(m_current_pipeline->getParameterName(i), &b))
					{
						auto* settings = Settings::getInstance();
						if (settings)
						{
							settings->setValue(m_current_pipeline->getParameterName(i), b);
						}
						m_current_pipeline->setParameter(i, b);
					}
				}

				ImGui::EndPopup();
			}
		}

		ImGui::SameLine();
		ImGui::Checkbox("Stats", &m_show_stats);

		bool is_deferred = m_current_pipeline == m_deferred_pipeline;
		ImGui::SameLine();
		m_is_pipeline_switch = ImGui::Checkbox("Deferred", &is_deferred);
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
			const auto& stats = m_current_pipeline->getStats();
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