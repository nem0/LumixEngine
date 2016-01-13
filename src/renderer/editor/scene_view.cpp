#include "scene_view.h"
#include "core/crc32.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "renderer/frame_buffer.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "editor/imgui/imgui.h"
#include "editor/settings.h"


static const char* WINDOW_NAME = "Scene View";


SceneView::SceneView()
{
	m_pipeline = nullptr;
	m_editor = nullptr;
	m_camera_speed = 0.1f;
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
	auto* settings = Settings::getInstance();
	if (!settings) return;

	int count = m_pipeline->getParameterCount();
	for (int i = 0; i < count; ++i)
	{
		bool b = settings->getValue(m_pipeline->getParameterName(i), m_pipeline->getParameter(i));
		m_pipeline->setParameter(i, b);
	}
}


void SceneView::onUniverseDestroyed()
{
	m_pipeline->setScene(nullptr);
}


bool SceneView::init(Lumix::WorldEditor& editor, Lumix::Array<Action*>& actions)
{
	m_editor = &editor;
	auto& engine = editor.getEngine();
	auto& allocator = engine.getAllocator();
	auto* renderer = static_cast<Lumix::Renderer*>(engine.getPluginManager().getPlugin("renderer"));

	Lumix::Path path("pipelines/main.lua");
	m_pipeline = Lumix::Pipeline::create(*renderer, path, engine.getAllocator());
	m_pipeline->load();
	m_pipeline->addCustomCommandHandler("renderGizmos")
		.callback.bind<SceneView, &SceneView::renderGizmos>(this);

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


void SceneView::renderGizmos()
{
	m_editor->renderIcons();
	m_editor->getGizmo().updateScale(m_editor->getEditCamera().index);
	m_editor->getGizmo().render();
}


void SceneView::onGUI()
{
	PROFILE_FUNCTION();
	m_is_opened = false;
	if (ImGui::BeginDock(WINDOW_NAME))
	{
		m_is_opened = true;
		auto size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();
		if (size.x > 0 && size.y > 0)
		{
			auto pos = ImGui::GetWindowPos();
			m_pipeline->setViewport(0, 0, int(size.x), int(size.y));
			auto* fb = m_pipeline->getFramebuffer("default");
			m_texture_handle = fb->getRenderbufferHandle(0);
			auto cursor_pos = ImGui::GetCursorScreenPos();
			m_screen_x = int(cursor_pos.x);
			m_screen_y = int(cursor_pos.y);
			m_width = int(size.x);
			m_height = int(size.y);
			ImGui::Image(&m_texture_handle, size);
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
						m_editor->onMouseDown((int)rel_mp.x, (int)rel_mp.y, (Lumix::MouseButton::Value)i);
					}
					if (ImGui::IsMouseReleased(i))
					{
						m_editor->onMouseUp((int)rel_mp.x, (int)rel_mp.y, (Lumix::MouseButton::Value)i);
					}

					auto delta = Lumix::Vec2(rel_mp.x, rel_mp.y) - m_last_mouse_pos;
					if(delta.x != 0 || delta.y != 0)
					{
						m_editor->onMouseMove((int)rel_mp.x, (int)rel_mp.y, (int)delta.x, (int)delta.y);
						m_last_mouse_pos = {rel_mp.x, rel_mp.y};
					}
				}
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
		int count = m_pipeline->getParameterCount();
		if (count)
		{
			if (ImGui::Button("Pipeline"))
			{
				ImGui::OpenPopup("pipeline_parameters_popup");
			}
			ImGui::SameLine();

			if (ImGui::BeginPopup("pipeline_parameters_popup"))
			{
				for (int i = 0; i < count; ++i)
				{
					bool b = m_pipeline->getParameter(i);
					if (ImGui::Checkbox(m_pipeline->getParameterName(i), &b))
					{
						auto* settings = Settings::getInstance();
						if (settings)
						{
							settings->setValue(m_pipeline->getParameterName(i), b);
						}
						m_pipeline->setParameter(i, b);
					}
				}

				ImGui::EndPopup();
			}
		}
	}

	ImGui::EndDock();
}