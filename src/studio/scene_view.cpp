#include "scene_view.h"
#include "core/crc32.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "gui_interface.h"
#include "renderer/frame_buffer.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "settings.h"


static const char* WINDOW_NAME = "Scene view";


SceneView::SceneView()
{
	m_pipeline = nullptr;
	m_editor = nullptr;
	m_camera_speed = 0.1f;
	m_gui = nullptr;
}


SceneView::~SceneView()
{
}


void SceneView::setGUIInterface(GUIInterface& gui)
{
	m_gui = &gui;
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
	Lumix::PipelineInstance::destroy(m_pipeline);
	auto* pipeline_manager =
		m_pipeline_source->getResourceManager().get(Lumix::ResourceManager::PIPELINE);
	pipeline_manager->unload(*m_pipeline_source);
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
	auto* pipeline_manager = engine.getResourceManager().get(Lumix::ResourceManager::PIPELINE);

	m_pipeline_source =
		static_cast<Lumix::Pipeline*>(pipeline_manager->load(Lumix::Path("pipelines/main.lua")));
	m_pipeline = Lumix::PipelineInstance::create(*m_pipeline_source, allocator);
	m_pipeline->addCustomCommandHandler("render_gizmos")
		.bind<SceneView, &SceneView::renderGizmos>(this);

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
	m_editor->renderIcons(*m_pipeline);
	m_editor->getGizmo().updateScale(m_editor->getEditCamera().index);
	m_editor->getGizmo().render(*m_pipeline);
}


void SceneView::onMouseUp(Lumix::MouseButton::Value button)
{
	auto pos = ImGui::GetIO().MousePos;
	m_editor->onMouseUp(int(pos.x) - m_screen_x, int(pos.y) - m_screen_y, button);
}


bool SceneView::onMouseDown(int screen_x, int screen_y, Lumix::MouseButton::Value button)
{
	if (!m_is_mouse_hovering_window) return false;

	bool is_inside = screen_x >= m_screen_x && screen_y >= m_screen_y &&
					 screen_x <= m_screen_x + m_width && screen_y <= m_screen_y + m_height;

	if (!is_inside) return false;

	ImGui::ResetActiveID();
	ImGui::SetWindowFocus(WINDOW_NAME);
	m_editor->onMouseDown(screen_x - m_screen_x, screen_y - m_screen_y, button);

	return true;
}


void SceneView::onMouseMove(int mouse_screen_x, int mouse_screen_y, int rel_x, int rel_y)
{
	m_editor->setGizmoUseStep(m_toggle_gizmo_step_action->isActive());
	m_editor->onMouseMove(mouse_screen_x - m_screen_x, mouse_screen_y - m_screen_y, rel_x, rel_y);
}


void SceneView::onGUI()
{
	PROFILE_FUNCTION();
	m_is_opened = false;
	m_is_mouse_hovering_window = false;
	if (m_gui->begin(WINDOW_NAME))
	{
		m_is_mouse_hovering_window = ImGui::IsMouseHoveringWindow();
		m_is_opened = true;
		auto size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();
		if (size.x > 0 && size.y > 0)
		{
			auto pos = ImGui::GetWindowPos();
			auto cp = ImGui::GetCursorPos();
			m_pipeline->setViewport(0, 0, int(size.x), int(size.y));
			auto* fb = m_pipeline->getFramebuffer("default");
			m_texture_handle = fb->getRenderbufferHandle(0);
			auto cursor_pos = ImGui::GetCursorScreenPos();
			m_screen_x = int(cursor_pos.x);
			m_screen_y = int(cursor_pos.y);
			m_width = int(size.x);
			m_height = int(size.y);
			ImGui::Image(&m_texture_handle, size);

			m_pipeline->render();
		}
	}

	ImGui::PushItemWidth(60);
	m_gui->dragFloat("Camera speed", &m_camera_speed, 0.1f, 0.01f, 999.0f, "%.2f");
	m_gui->sameLine();
	if (m_editor->isMeasureToolActive())
	{
		m_gui->text("| Measured distance: %f", m_editor->getMeasuredDistance());
	}

	m_gui->sameLine();
	int step = m_editor->getGizmo().getStep();
	if (ImGui::DragInt("Gizmo step", &step, 1.0f, 0, 200))
	{
		m_editor->getGizmo().setStep(step);
	}

	m_gui->sameLine();
	int count = m_pipeline->getParameterCount();
	if (count)
	{
		if (m_gui->button("Pipeline"))
		{
			ImGui::OpenPopup("pipeline_parameters_popup");
		}

		if (ImGui::BeginPopup("pipeline_parameters_popup"))
		{
			for (int i = 0; i < count; ++i)
			{
				bool b = m_pipeline->getParameter(i);
				if (m_gui->checkbox(m_pipeline->getParameterName(i), &b))
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

	m_gui->end();
}