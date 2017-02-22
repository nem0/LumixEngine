#include "game_view.h"
#include "editor/platform_interface.h"
#include "editor/studio_app.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/input_system.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "gui/gui_system.h"
#include "imgui/imgui.h"
#include "renderer/frame_buffer.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include <SDL.h>


struct GUIInterface : Lumix::GUISystem::Interface
{
	GUIInterface(GameView& game_view)
		: m_game_view(game_view)
	{
	}

	Lumix::Pipeline* getPipeline() override { return m_game_view.m_pipeline; }
	Lumix::Vec2 getPos() const override { return m_game_view.m_pos; }
	Lumix::Vec2 getSize() const override { return m_game_view.m_size; }
	
	
	void enableCursor(bool enable) override
	{ 
		m_game_view.enableIngameCursor(enable);
	}


	GameView& m_game_view;
};


GameView::GameView(StudioApp& app)
	: m_studio_app(app)
	, m_is_opened(true)
	, m_is_fullscreen(false)
	, m_pipeline(nullptr)
	, m_is_mouse_captured(false)
	, m_is_ingame_cursor(false)
	, m_editor(nullptr)
	, m_is_mouse_hovering_window(false)
	, m_time_multiplier(1.0f)
	, m_paused(false)
	, m_is_opengl(false)
	, m_show_stats(false)
	, m_texture_handle(BGFX_INVALID_HANDLE)
	, m_gui_interface(nullptr)
{
}


GameView::~GameView()
{
}


void GameView::enableIngameCursor(bool enable)
{
	m_is_ingame_cursor = enable;
	if (!m_is_mouse_captured) return;

	SDL_ShowCursor(m_is_ingame_cursor ? 1 : 0);
	SDL_SetRelativeMouseMode(m_is_ingame_cursor ? SDL_FALSE : SDL_TRUE);
}


void GameView::onUniverseCreated()
{
	auto* scene = m_editor->getUniverse()->getScene(Lumix::crc32("renderer"));
	m_pipeline->setScene(static_cast<Lumix::RenderScene*>(scene));
}


void GameView::onUniverseDestroyed()
{
	m_pipeline->setScene(nullptr);
}


void GameView::init(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	auto& engine = editor.getEngine();
	auto* renderer = static_cast<Lumix::Renderer*>(engine.getPluginManager().getPlugin("renderer"));
	m_is_opengl = renderer->isOpenGL();
	Lumix::Path path("pipelines/game_view.lua");
	m_pipeline = Lumix::Pipeline::create(*renderer, path, engine.getAllocator());
	m_pipeline->load();

	editor.universeCreated().bind<GameView, &GameView::onUniverseCreated>(this);
	editor.universeDestroyed().bind<GameView, &GameView::onUniverseDestroyed>(this);
	onUniverseCreated();

	auto* gui = static_cast<Lumix::GUISystem*>(m_editor->getEngine().getPluginManager().getPlugin("gui"));
	if (gui)
	{
		m_gui_interface = LUMIX_NEW(m_editor->getEngine().getAllocator(), GUIInterface)(*this);
		gui->setInterface(m_gui_interface);
	}
}


void GameView::shutdown()
{
	m_editor->universeCreated().unbind<GameView, &GameView::onUniverseCreated>(this);
	m_editor->universeDestroyed().unbind<GameView, &GameView::onUniverseDestroyed>(this);
	auto* gui = static_cast<Lumix::GUISystem*>(m_editor->getEngine().getPluginManager().getPlugin("gui"));
	if (gui)
	{
		gui->setInterface(nullptr);
		LUMIX_DELETE(m_editor->getEngine().getAllocator(), m_gui_interface);
	}
	Lumix::Pipeline::destroy(m_pipeline);
	m_pipeline = nullptr;
}


void GameView::setScene(Lumix::RenderScene* scene)
{
	m_pipeline->setScene(scene);
}


void GameView::captureMouse(bool capture)
{
	if (m_is_mouse_captured == capture) return;

	m_is_mouse_captured = capture;
	m_editor->getEngine().getInputSystem().enable(m_is_mouse_captured);
	SDL_ShowCursor(capture && !m_is_ingame_cursor ? 0 : 1);
	SDL_SetRelativeMouseMode(capture && !m_is_ingame_cursor ? SDL_TRUE : SDL_FALSE);
	if (capture) SDL_GetMouseState(&m_captured_mouse_x, &m_captured_mouse_y);
	else SDL_WarpMouseInWindow(nullptr, m_captured_mouse_x, m_captured_mouse_y);
	if (!capture) PlatformInterface::unclipCursor();
}


void GameView::onFullscreenGUI()
{
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	bool opened = true;
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 size = io.DisplaySize;
	if (!ImGui::Begin("game view fullscreen",
		&opened,
		size,
		1.0f,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_ShowBorders))
	{
		ImGui::End();
		return;
	}

	m_pipeline->setViewport(0, 0, int(size.x), int(size.y));

	auto* fb = m_pipeline->getFramebuffer("default");
	m_texture_handle = fb->getRenderbufferHandle(0);
	if (m_is_opengl)
	{
		ImGui::Image(&m_texture_handle, size, ImVec2(0, 1), ImVec2(1, 0));
	}
	else
	{
		ImGui::Image(&m_texture_handle, size);
	}
	m_pos.x = ImGui::GetItemRectMin().x;
	m_pos.y = ImGui::GetItemRectMin().y;
	m_size.x = ImGui::GetItemRectSize().x;
	m_size.y = ImGui::GetItemRectSize().y;

	m_pipeline->render();
	ImGui::End();

	if (m_is_fullscreen && (io.KeysDown[ImGui::GetKeyIndex(ImGuiKey_Escape)] || !m_editor->isGameMode()))
	{
		setFullscreen(false);
	}
}


void GameView::setFullscreen(bool fullscreen)
{
	captureMouse(fullscreen);
	m_studio_app.setFullscreen(fullscreen);
	m_is_fullscreen = fullscreen;
}


void GameView::onStatsGUI(const ImVec2& view_pos)
{
	if (!m_show_stats || !m_is_opened) return;
	
	float toolbar_height = 24 + ImGui::GetStyle().FramePadding.y * 2;
	ImVec2 v = view_pos;
	v.x += ImGui::GetStyle().FramePadding.x;
	v.y += ImGui::GetStyle().FramePadding.y + toolbar_height;
	ImGui::SetNextWindowPos(v);
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
		Lumix::toCStringPretty(stats.triangle_count, buf, Lumix::lengthOf(buf));
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


void GameView::onGUI()
{
	PROFILE_FUNCTION();
	if (!m_pipeline->isReady()) return;

	auto& io = ImGui::GetIO();

	bool is_focus = (SDL_GetWindowFlags(m_studio_app.getWindow()) & SDL_WINDOW_INPUT_FOCUS) != 0;
	if (m_is_mouse_captured &&
		(io.KeysDown[ImGui::GetKeyIndex(ImGuiKey_Escape)] || !m_editor->isGameMode() || !is_focus))
	{
		captureMouse(false);
	}

	const char* window_name = "Game View###game_view";
	if (m_is_mouse_captured) window_name = "Game View (mouse captured)###game_view";
	
	if (m_is_fullscreen)
	{
		onFullscreenGUI();
		return;
	}
	ImVec2 view_pos;
	bool is_game_view_visible = false;
	if (ImGui::BeginDock(window_name, &m_is_opened))
	{
		is_game_view_visible = true;
		m_is_mouse_hovering_window = ImGui::IsMouseHoveringWindow();

		auto content_min = ImGui::GetCursorScreenPos();
		auto size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();
		ImVec2 content_max(content_min.x + size.x, content_min.y + size.y);
		if (size.x > 0 && size.y > 0)
		{
			m_pipeline->setViewport(0, 0, int(size.x), int(size.y));

			auto* fb = m_pipeline->getFramebuffer("default");
			m_texture_handle = fb->getRenderbufferHandle(0);
			view_pos = ImGui::GetCursorScreenPos();
			if (m_is_opengl)
			{
				ImGui::Image(&m_texture_handle, size, ImVec2(0, 1), ImVec2(1, 0));
			}
			else
			{
				ImGui::Image(&m_texture_handle, size);
			}
			m_pos.x = ImGui::GetItemRectMin().x;
			m_pos.y = ImGui::GetItemRectMin().y;
			m_size.x = ImGui::GetItemRectSize().x;
			m_size.y = ImGui::GetItemRectSize().y;

			if (m_is_mouse_captured && m_is_ingame_cursor)
			{
				ImVec2 pos = ImGui::GetItemRectMin();
				PlatformInterface::clipCursor((int)pos.x, (int)pos.y, (int)m_size.x, (int)m_size.y);
			}

			if (ImGui::Checkbox("Pause", &m_paused))
			{
				m_editor->getEngine().pause(m_paused);
			}
			if (m_paused)
			{
				ImGui::SameLine();
				if (ImGui::Button("Next frame"))
				{
					m_editor->getEngine().nextFrame();
				}
			}
			ImGui::SameLine();
			if (ImGui::DragFloat("Time multiplier", &m_time_multiplier, 0.01f, 0.01f, 30.0f))
			{
				m_editor->getEngine().setTimeMultiplier(m_time_multiplier);
			}
			if(m_editor->isGameMode())
			{
				ImGui::SameLine();
				if (ImGui::Button("Fullscreen"))
				{
					setFullscreen(true);
				}
			}
			ImGui::SameLine();
			ImGui::Checkbox("Stats", &m_show_stats);
			m_pipeline->render();
		}

		if (m_is_mouse_captured && (io.KeysDown[ImGui::GetKeyIndex(ImGuiKey_Escape)] || !m_editor->isGameMode()))
		{
			captureMouse(false);
		}

		if (ImGui::IsMouseHoveringRect(content_min, content_max) && m_is_mouse_hovering_window &&
			ImGui::IsMouseClicked(0) && m_editor->isGameMode())
		{
			captureMouse(true);
		}
	}
	ImGui::EndDock();
	if(is_game_view_visible) onStatsGUI(view_pos);
}
