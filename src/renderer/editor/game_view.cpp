#include "game_view.h"
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

	GameView& m_game_view;
};


GameView::GameView(StudioApp& app)
	: m_studio_app(app)
	, m_is_opened(true)
	, m_pipeline(nullptr)
	, m_is_mouse_captured(false)
	, m_editor(nullptr)
	, m_is_mouse_hovering_window(false)
	, m_time_multiplier(1.0f)
	, m_paused(false)
	, m_is_opengl(false)
	, m_texture_handle(BGFX_INVALID_HANDLE)
	, m_gui_interface(nullptr)
{
}


GameView::~GameView()
{
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
	m_is_mouse_captured = capture;
	m_editor->getEngine().getInputSystem().enable(m_is_mouse_captured);
	SDL_ShowCursor(m_is_mouse_captured ? 0 : 1);
	SDL_SetRelativeMouseMode(capture ? SDL_TRUE : SDL_FALSE);
}


void GameView::onGui()
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
	if (ImGui::BeginDock(window_name, &m_is_opened))
	{
		m_is_mouse_hovering_window = ImGui::IsMouseHoveringWindow();

		auto content_min = ImGui::GetCursorScreenPos();
		auto size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();
		ImVec2 content_max(content_min.x + size.x, content_min.y + size.y);
		ImVec2 pos;
		if (size.x > 0 && size.y > 0)
		{
			pos = ImGui::GetWindowPos();
			m_pos.x = pos.x;
			m_pos.y = pos.y;
			auto cp = ImGui::GetCursorPos();
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
			m_size.x = ImGui::GetItemRectSize().x;
			m_size.y = ImGui::GetItemRectSize().y;
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
			m_pipeline->render();
		}

		if (m_is_mouse_captured)
		{
			Lumix::InputSystem::InputEvent event;
			if (ImGui::IsMouseReleased(0))
			{
				event.type = Lumix::InputSystem::InputEvent::POINTER_UP;
				m_editor->getEngine().getInputSystem().injectEvent(event);
			}
			if (ImGui::IsMouseClicked(0))
			{
				event.type = Lumix::InputSystem::InputEvent::POINTER_DOWN;
				m_editor->getEngine().getInputSystem().injectEvent(event);
			}

			if (io.KeysDown[ImGui::GetKeyIndex(ImGuiKey_Escape)] || !m_editor->isGameMode())
			{
				captureMouse(false);
			}
			static bool was_down[512] = {};
			auto& io = ImGui::GetIO();
			auto& input = m_editor->getEngine().getInputSystem();
			for (int i = 0; i < 512; ++i)
			{
				if (io.KeysDownDuration[i] == 0)
				{
					event.type = Lumix::InputSystem::InputEvent::KEY_DOWN;
					event.key.sym = i;
					input.injectEvent(event);
					was_down[i] = true;
				}
				if (!io.KeysDown[i] && was_down[i])
				{
					was_down[i] = false;
					event.type = Lumix::InputSystem::InputEvent::KEY_UP;
					event.key.sym = i;
					input.injectEvent(event);
				}
			}
			static ImVec2 old_mouse_pos = io.MousePos;
			if (io.MousePos.x != old_mouse_pos.x || io.MousePos.y != old_mouse_pos.y)
			{
				event.type = Lumix::InputSystem::InputEvent::POINTER_MOVE;
				input.injectEvent(event);
				old_mouse_pos = io.MousePos;
			}
		}

		if (ImGui::IsMouseHoveringRect(content_min, content_max) && m_is_mouse_hovering_window &&
			ImGui::IsMouseClicked(0) && m_editor->isGameMode())
		{
			captureMouse(true);
		}
	}
	ImGui::EndDock();
}
