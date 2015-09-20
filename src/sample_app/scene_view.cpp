#include "scene_view.h"
#include "core/resource_manager.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "ocornut-imgui/imgui.h"
#include "renderer/frame_buffer.h"
#include "renderer/pipeline.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


static const char* WINDOW_NAME = "Scene view";


SceneView::SceneView()
{
	m_pipeline = nullptr;
	m_editor = nullptr;
}


SceneView::~SceneView()
{
}


void SceneView::setScene(Lumix::RenderScene* scene)
{
	m_pipeline->setScene(scene);
}


void SceneView::shutdown()
{
	Lumix::PipelineInstance::destroy(m_pipeline);
	m_pipeline_source->getResourceManager()
		.get(Lumix::ResourceManager::PIPELINE)
		->unload(*m_pipeline_source);
}


bool SceneView::init(Lumix::WorldEditor& editor)
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

	return true;
}


void SceneView::update()
{
	if(ImGui::IsAnyItemActive()) return;
	if (!m_is_opened) return;
	if (ImGui::GetIO().KeysDown[VK_CONTROL]) return;

	int screen_x = int(ImGui::GetIO().MousePos.x);
	int screen_y = int(ImGui::GetIO().MousePos.y);
	bool is_inside = screen_x >= m_screen_x && screen_y >= m_screen_y &&
		screen_x <= m_screen_x + m_width && screen_y <= m_screen_y + m_height;
	if (!is_inside) return;

	float speed = 0.1f;
	if (ImGui::GetIO().KeysDown[VK_SHIFT])
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

	ImGui::SetWindowFocus(WINDOW_NAME);
	m_editor->onMouseDown(screen_x - m_screen_x, screen_y - m_screen_y, button);

	return true;
}


void SceneView::onMouseMove(int mouse_screen_x, int mouse_screen_y, int rel_x, int rel_y)
{
	int flags = ImGui::GetIO().KeysDown[VK_MENU]
		? (int)Lumix::WorldEditor::MouseFlags::ALT
		: 0;
	m_editor->onMouseMove(
		mouse_screen_x - m_screen_x, mouse_screen_y - m_screen_y, rel_x, rel_y, flags);
}


void SceneView::onGui()
{
	m_is_opened = false;
	m_is_mouse_hovering_window = false;
	if (ImGui::Begin(WINDOW_NAME))
	{
		m_is_mouse_hovering_window = ImGui::IsMouseHoveringWindow();
		m_is_opened = true;
		auto size = ImGui::GetContentRegionAvail();
		auto pos = ImGui::GetWindowPos();
		auto cp = ImGui::GetCursorPos();
		m_pipeline->setViewport(0, 0, int(size.x), int(size.y));
		auto* fb = m_pipeline->getFramebuffer("default");
		m_texture_handle = fb->getRenderbufferHandle(0);
		auto cursor_pos = ImGui::GetCursorScreenPos();
		m_screen_x = int(cursor_pos.x);
		m_screen_y = int(cursor_pos.y);
		m_width = int(size.x);
		m_height = int(size.x);
		ImGui::Image(&m_texture_handle, size);

		m_pipeline->render();
	}
	ImGui::End();
}