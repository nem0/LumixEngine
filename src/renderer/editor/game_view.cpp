#include "game_view.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/lua_wrapper.h"
#include "engine/plugin_manager.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "gui/gui_system.h"
#include "imgui/imgui.h"
#include "renderer/ffr/ffr.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"


namespace Lumix
{


struct GUIInterface : GUISystem::Interface
{
	explicit GUIInterface(GameView& game_view)
		: m_game_view(game_view)
	{
	}

	Pipeline* getPipeline() override { return m_game_view.m_pipeline; }
	Vec2 getPos() const override { return m_game_view.m_pos; }
	Vec2 getSize() const override { return m_game_view.m_size; }

	
	void enableCursor(bool enable) override
	{ 
		m_game_view.enableIngameCursor(enable);
	}


	GameView& m_game_view;
};


GameView::GameView(StudioApp& app)
	: m_studio_app(app)
	, m_is_open(false)
	, m_is_fullscreen(false)
	, m_pipeline(nullptr)
	, m_is_mouse_captured(false)
	, m_is_ingame_cursor(false)
	, m_is_mouse_hovering_window(false)
	, m_time_multiplier(1.0f)
	, m_paused(false)
	, m_show_stats(false)
	, m_texture_handle(ffr::INVALID_TEXTURE)
	, m_gui_interface(nullptr)
	, m_editor(app.getWorldEditor())
{
	Engine& engine = app.getWorldEditor().getEngine();
	auto f = &LuaWrapper::wrapMethodClosure<GameView, decltype(&GameView::forceViewport), &GameView::forceViewport>;
	LuaWrapper::createSystemClosure(engine.getState(), "GameView", this, "forceViewport", f);

	WorldEditor& editor = app.getWorldEditor();
	Action* action = LUMIX_NEW(editor.getAllocator(), Action)("Game View", "Toggle game view", "game_view");
	action->func.bind<GameView, &GameView::onAction>(this);
	action->is_selected.bind<GameView, &GameView::isOpen>(this);
	app.addWindowAction(action);

	Action* fullscreen_action = LUMIX_NEW(editor.getAllocator(), Action)("Game View fullscreen", "Game View fullscreen", "game_view_fullscreen");
	fullscreen_action->func.bind<GameView, &GameView::toggleFullscreen>(this);
	app.addAction(fullscreen_action);

	auto* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
	PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
	m_pipeline = Pipeline::create(*renderer, pres, "GAME_VIEW", engine.getAllocator());

	editor.universeCreated().bind<GameView, &GameView::onUniverseCreated>(this);
	editor.universeDestroyed().bind<GameView, &GameView::onUniverseDestroyed>(this);
	if (editor.getUniverse()) onUniverseCreated();

	auto* gui = static_cast<GUISystem*>(engine.getPluginManager().getPlugin("gui"));
	if (gui)
	{
		m_gui_interface = LUMIX_NEW(engine.getAllocator(), GUIInterface)(*this);
		gui->setInterface(m_gui_interface);
	}
}


GameView::~GameView()
{
	m_editor.universeCreated().unbind<GameView, &GameView::onUniverseCreated>(this);
	m_editor.universeDestroyed().unbind<GameView, &GameView::onUniverseDestroyed>(this);
	auto* gui = static_cast<GUISystem*>(m_editor.getEngine().getPluginManager().getPlugin("gui"));
	if (gui)
	{
		gui->setInterface(nullptr);
		LUMIX_DELETE(m_editor.getEngine().getAllocator(), m_gui_interface);
	}
	Pipeline::destroy(m_pipeline);
	m_pipeline = nullptr;

}


void GameView::enableIngameCursor(bool enable)
{
	m_is_ingame_cursor = enable;
	if (!m_is_mouse_captured) return;

	OS::showCursor(m_is_ingame_cursor);
}


void GameView::onUniverseCreated()
{
	auto* scene = m_editor.getUniverse()->getScene(crc32("renderer"));
	m_pipeline->setScene(static_cast<RenderScene*>(scene));
}


void GameView::onUniverseDestroyed()
{
	m_pipeline->setScene(nullptr);
}



void GameView::setScene(RenderScene* scene)
{
	m_pipeline->setScene(scene);
}


void GameView::captureMouse(bool capture)
{
	if (m_is_mouse_captured == capture) return;

	m_is_mouse_captured = capture;
	m_editor.getEngine().getInputSystem().enable(m_is_mouse_captured);
	OS::showCursor(!capture || m_is_ingame_cursor);
	
	if (capture) {
		const OS::Point cp = OS::getMouseScreenPos();
		m_captured_mouse_x = cp.x;
		m_captured_mouse_y = cp.y;
	}
	else {
		OS::unclipCursor();
		OS::setMouseScreenPos(m_captured_mouse_x, m_captured_mouse_y);
	}
}


void GameView::onFullscreenGUI()
{
	processInputEvents();

	ImGuiIO& io = ImGui::GetIO();
	bool open = true;
	ImVec2 size = io.DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(size);
	if (!ImGui::Begin("game view fullscreen",
		&open,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		return;
	}

	EntityPtr camera = m_pipeline->getScene()->getActiveCamera();
	if (camera.isValid()) {
		Viewport vp = m_pipeline->getScene()->getCameraViewport((EntityRef)camera);
		vp.w = (int)size.x;
		vp.h = (int)size.y;
		m_pipeline->getScene()->setCameraScreenSize((EntityRef)camera, vp.w, vp.h);
		m_pipeline->setViewport(vp);
		m_pipeline->render();
		m_texture_handle = m_pipeline->getOutput();
		if (ffr::isOriginBottomLeft())
		{
			ImGui::Image((void*)(uintptr_t)m_texture_handle.value, size, ImVec2(0, 1), ImVec2(1, 0));
		}
		else
		{
			ImGui::Image((void*)(uintptr_t)m_texture_handle.value, size);
		}
	}
	else {
		ImGui::Rect(size.x, size.y, 0xff0000FF);
	}
	m_pos.x = ImGui::GetItemRectMin().x;
	m_pos.y = ImGui::GetItemRectMin().y;
	m_size.x = ImGui::GetItemRectSize().x;
	m_size.y = ImGui::GetItemRectSize().y;

	ImGui::End();

	if (m_is_fullscreen && (io.KeysDown[ImGui::GetKeyIndex(ImGuiKey_Escape)] || !m_editor.isGameMode()))
	{
		setFullscreen(false);
	}
}


void GameView::toggleFullscreen()
{
	if (!m_editor.isGameMode()) return;
	setFullscreen(!m_is_fullscreen);
}


void GameView::setFullscreen(bool fullscreen)
{
	captureMouse(fullscreen);
	m_studio_app.setFullscreen(fullscreen);
	m_is_fullscreen = fullscreen;
}


void GameView::onStatsGUI(const ImVec2& view_pos)
{
	if (!m_show_stats || !m_is_open) return;
	
			// TODO
			ASSERT(false);
			/*
	float toolbar_height = 24 + ImGui::GetStyle().FramePadding.y * 2;
	ImVec2 v = view_pos;
	v.x += ImGui::GetStyle().FramePadding.x;
	v.y += ImGui::GetStyle().FramePadding.y + toolbar_height;
	ImGui::SetNextWindowPos(v);
	auto col = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
	col.w = 0.3f;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, col);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
	if (ImGui::Begin("###stats_overlay", nullptr, flags))
	{
		const bgfx::Stats* bgfx_stats = bgfx::getStats();
		const auto& stats = m_pipeline->getStats();
		ImGui::LabelText("Draw calls", "%d", stats.draw_call_count);
		ImGui::LabelText("Instances", "%d", stats.instance_count);
		char buf[30];
		toCStringPretty(stats.triangle_count, buf, lengthOf(buf));
		ImGui::LabelText("Triangles", "%s", buf);
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
	ImGui::PopStyleColor();*/
}


void GameView::forceViewport(bool enable, int w, int h)
{
	m_forced_viewport.enabled = enable;
	m_forced_viewport.width = w;
	m_forced_viewport.height = h;
}

void GameView::processInputEvents()
{
	if (!m_is_mouse_captured) return;
	
	InputSystem& input = m_editor.getEngine().getInputSystem();
	const OS::Event* events = m_studio_app.getEvents();
	for (int i = 0, c = m_studio_app.getEventsCount(); i < c; ++i) {
		input.injectEvent(events[i]);
	}
}


void GameView::onWindowGUI()
{
	PROFILE_FUNCTION();
	if (!m_pipeline->isReady()) return;

	auto& io = ImGui::GetIO();

	bool is_focus = OS::getFocused() == m_studio_app.getWindow();
	if (m_is_mouse_captured &&
		(io.KeysDown[ImGui::GetKeyIndex(ImGuiKey_Escape)] || !m_editor.isGameMode() || !is_focus))
	{
		captureMouse(false);
	}

	const char* window_name = "Game View###game_view";
	if (m_is_mouse_captured) window_name = "Game View (mouse captured)###game_view";
	
	if (m_is_fullscreen) {
		onFullscreenGUI();
		return;
	}
	ImVec2 view_pos;
	bool is_game_view_visible = false;
	if (!m_is_open) return;
	if (ImGui::Begin(window_name, &m_is_open, ImGuiWindowFlags_NoNavInputs)) {
		is_game_view_visible = true;
		view_pos = ImGui::GetCursorScreenPos();
		m_is_mouse_hovering_window = ImGui::IsWindowHovered();

		auto content_min = ImGui::GetCursorScreenPos();
		auto size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();
		ImVec2 content_max(content_min.x + size.x, content_min.y + size.y);
		if (m_forced_viewport.enabled) size = { (float)m_forced_viewport.width, (float)m_forced_viewport.height };
		if (size.x > 0 && size.y > 0) {
			const EntityPtr camera = m_pipeline->getScene()->getActiveCamera();
			if (camera.isValid()) {
				Viewport vp = m_pipeline->getScene()->getCameraViewport((EntityRef)camera);
				vp.w = (int)size.x;
				vp.h = (int)size.y;
				m_pipeline->getScene()->setCameraScreenSize((EntityRef)camera, vp.w, vp.h);
				m_pipeline->setViewport(vp);
				m_pipeline->render();
				m_texture_handle = m_pipeline->getOutput();

				if(m_texture_handle.isValid()) {
					if (ffr::isOriginBottomLeft()) {
						ImGui::Image((void*)(uintptr_t)m_texture_handle.value, size, ImVec2(0, 1), ImVec2(1, 0));
					}
					else {
						ImGui::Image((void*)(uintptr_t)m_texture_handle.value, size);
					}
				}
				else {
					ImGui::Rect(size.x, size.y, 0xffFF00FF);
				}
			}
			else {
				ImGui::Rect(size.x, size.y, 0xffFF00FF);
			}
			m_pos.x = ImGui::GetItemRectMin().x;
			m_pos.y = ImGui::GetItemRectMin().y;
			m_size.x = ImGui::GetItemRectSize().x;
			m_size.y = ImGui::GetItemRectSize().y;

			if (m_is_mouse_captured && m_is_ingame_cursor) {
				ImVec2 pos = ImGui::GetItemRectMin();
				OS::clipCursor(m_studio_app.getWindow(), (int)pos.x, (int)pos.y, (int)m_size.x, (int)m_size.y);
			}

			processInputEvents();

			if (ImGui::Checkbox("Pause", &m_paused)) m_editor.getEngine().pause(m_paused);
			if (m_paused) {
				ImGui::SameLine();
				if (ImGui::Button("Next frame")) m_editor.getEngine().nextFrame();
			}
			ImGui::SameLine();
			ImGui::PushItemWidth(50);
			if (ImGui::DragFloat("Time multiplier", &m_time_multiplier, 0.01f, 0.01f, 30.0f)) {
				m_editor.getEngine().setTimeMultiplier(m_time_multiplier);
			}
			ImGui::PopItemWidth();
			if(m_editor.isGameMode()) {
				ImGui::SameLine();
				if (ImGui::Button("Fullscreen")) setFullscreen(true);
			}
			ImGui::SameLine();
			ImGui::Checkbox("Stats", &m_show_stats);
			ImGui::SameLine();
			m_pipeline->callLuaFunction("onGUI");
		}

		
		if (m_is_mouse_captured && (io.KeysDown[ImGui::GetKeyIndex(ImGuiKey_Escape)] || !m_editor.isGameMode())) {
			captureMouse(false);
		}

		if (ImGui::IsMouseHoveringRect(content_min, content_max) && m_is_mouse_hovering_window &&
			ImGui::IsMouseClicked(0) && m_editor.isGameMode())
		{
			captureMouse(true);
		}
	}
	ImGui::End();
	if(is_game_view_visible) onStatsGUI(view_pos);
}


} // namespace Lumix
