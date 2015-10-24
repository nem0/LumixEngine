#include "game_view.h"
#include "core/crc32.h"
#include "core/input_system.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "engine/engine.h"
#include "ocornut-imgui/imgui.h"
#include "renderer/frame_buffer.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/texture.h"


GameView::GameView()
	: m_is_opened(true)
	, m_pipeline(nullptr)
	, m_pipeline_source(nullptr)
	, m_is_mouse_captured(false)
	, m_editor(nullptr)
	, m_is_mouse_hovering_window(false)
	, m_hwnd(NULL)
{
}


GameView::~GameView()
{
}


void GameView::onUniverseCreated()
{
	auto* scene = m_editor->getScene(Lumix::crc32("renderer"));
	m_pipeline->setScene(static_cast<Lumix::RenderScene*>(scene));
}


void GameView::onUniverseDestroyed()
{
	m_pipeline->setScene(nullptr);
}


void GameView::init(HWND hwnd, Lumix::WorldEditor& editor)
{
	m_hwnd = hwnd;
	m_editor = &editor;
	auto& engine = editor.getEngine();
	auto* pipeline_manager = engine.getResourceManager().get(Lumix::ResourceManager::PIPELINE);
	auto* resource = pipeline_manager->load(Lumix::Path("pipelines/game_view.lua"));

	m_pipeline_source = static_cast<Lumix::Pipeline*>(resource);
	m_pipeline = Lumix::PipelineInstance::create(*m_pipeline_source, engine.getAllocator());

	editor.universeCreated().bind<GameView, &GameView::onUniverseCreated>(this);
	editor.universeDestroyed().bind<GameView, &GameView::onUniverseDestroyed>(this);
	onUniverseCreated();
}


void GameView::shutdown()
{
	Lumix::PipelineInstance::destroy(m_pipeline);
	auto& rm = m_pipeline_source->getResourceManager();
	rm.get(Lumix::ResourceManager::PIPELINE)->unload(*m_pipeline_source);

	m_pipeline = nullptr;
	m_pipeline_source = nullptr;
}


void GameView::setScene(Lumix::RenderScene* scene)
{
	m_pipeline->setScene(scene);
}


void GameView::captureMouse(bool capture)
{
	m_is_mouse_captured = capture;
	m_editor->getEngine().getInputSystem().enable(m_is_mouse_captured);
	ShowCursor(!m_is_mouse_captured);
	if (!m_is_mouse_captured) ClipCursor(NULL);
}


void GameView::onGui()
{
	PROFILE_FUNCTION();
	if (!m_is_opened) return;
	if (!m_pipeline_source->isReady()) return;

	auto& io = ImGui::GetIO();

	HWND foreground_win = GetForegroundWindow();
	if (m_is_mouse_captured &&
		(io.KeysDown[VK_ESCAPE] || !m_editor->isGameMode() || foreground_win != m_hwnd))
	{
		captureMouse(false);
	}

	const char* window_name = "Game view###game_view";
	if (m_is_mouse_captured) window_name = "Game view (mouse captured)###game_view";
	if (ImGui::Begin(window_name, &m_is_opened))
	{
		m_is_mouse_hovering_window = ImGui::IsMouseHoveringWindow();

		auto content_min = ImGui::GetCursorScreenPos();
		auto size = ImGui::GetContentRegionAvail();
		ImVec2 content_max(content_min.x + size.x, content_min.y + size.y);
		if (size.x > 0 && size.y > 0)
		{
			auto pos = ImGui::GetWindowPos();
			auto cp = ImGui::GetCursorPos();
			m_pipeline->setViewport(0, 0, int(size.x), int(size.y));

			auto* fb = m_pipeline->getFramebuffer("default");
			m_texture_handle = fb->getRenderbufferHandle(0);
			ImGui::Image(&m_texture_handle, size);
			m_pipeline->render();
		}

		if (m_is_mouse_captured)
		{
			POINT min;
			POINT max;
			min.x = LONG(content_min.x);
			min.y = LONG(content_min.y);
			max.x = LONG(content_max.x);
			max.y = LONG(content_max.y);
			ClientToScreen(m_hwnd, &min);
			ClientToScreen(m_hwnd, &max);
			RECT rect;
			rect.left = min.x;
			rect.right = max.x;
			rect.top = min.y;
			rect.bottom = max.y;
			ClipCursor(&rect);
			if (io.KeysDown[VK_ESCAPE] || !m_editor->isGameMode()) captureMouse(false);
		}

		if (ImGui::IsMouseHoveringRect(content_min, content_max) && m_is_mouse_hovering_window &&
			ImGui::IsMouseClicked(0) && m_editor->isGameMode())
		{
			captureMouse(true);
		}
	}
	ImGui::End();
}
