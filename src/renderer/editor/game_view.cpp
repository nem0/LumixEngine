#include <imgui/imgui.h>

#include "game_view.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/lua_wrapper.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/universe.h"
#include "gui/gui_system.h"
#include "renderer/gpu/gpu.h"
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

	Pipeline* getPipeline() override { return m_game_view.m_pipeline.get(); }
	Vec2 getPos() const override { return m_game_view.m_pos; }
	Vec2 getSize() const override { return m_game_view.m_size; }
	void setCursor(os::CursorType type) override { m_game_view.setCursor(type); }
	void enableCursor(bool enable) override { m_game_view.enableIngameCursor(enable); }

	GameView& m_game_view;
};


GameView::GameView(StudioApp& app)
	: m_app(app)
	, m_is_open(false)
	, m_is_fullscreen(false)
	, m_is_mouse_captured(false)
	, m_is_ingame_cursor(false)
	, m_time_multiplier(1.0f)
	, m_show_stats(false)
	, m_editor(app.getWorldEditor())
{
	Engine& engine = app.getEngine();
	auto f = &LuaWrapper::wrapMethodClosure<&GameView::forceViewport>;
	LuaWrapper::createSystemClosure(engine.getState(), "GameView", this, "forceViewport", f);
}


void GameView::init() {
	IAllocator& allocator = m_app.getAllocator();
	m_toggle_ui.init("Game View", "Toggle game view", "game_view", "", true);
	m_toggle_ui.func.bind<&GameView::onAction>(this);
	m_toggle_ui.is_selected.bind<&GameView::isOpen>(this);
	m_app.addWindowAction(&m_toggle_ui);

	m_fullscreen_action.init("Game View fullscreen", "Game View fullscreen", "game_view_fullscreen", "", true);
	m_fullscreen_action.func.bind<&GameView::toggleFullscreen>(this);
	m_app.addAction(&m_fullscreen_action);

	Engine& engine = m_app.getEngine();
	auto* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
	PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
	m_pipeline = Pipeline::create(*renderer, pres, "GAME_VIEW", engine.getAllocator());

	auto* gui = static_cast<GUISystem*>(engine.getPluginManager().getPlugin("gui"));
	if (gui)
	{
		m_gui_interface = UniquePtr<GUIInterface>::create(engine.getAllocator(), *this);
		gui->setInterface(m_gui_interface.get());
	}
}


GameView::~GameView()
{
	m_app.removeAction(&m_toggle_ui);
	m_app.removeAction(&m_fullscreen_action);
	Engine& engine = m_app.getEngine();
	auto* gui = static_cast<GUISystem*>(engine.getPluginManager().getPlugin("gui"));
	if (gui) {
		gui->setInterface(nullptr);
	}
}

void GameView::setCursor(os::CursorType type)
{
	m_cursor_type = type;
}

void GameView::enableIngameCursor(bool enable)
{
	m_is_ingame_cursor = enable;
	if (!m_is_mouse_captured) return;

	os::showCursor(m_is_ingame_cursor);
}


void GameView::captureMouse(bool capture)
{
	if (m_is_mouse_captured == capture) return;

	m_app.setCursorCaptured(capture);
	m_is_mouse_captured = capture;
	os::showCursor(!capture || m_is_ingame_cursor);
	
	if (capture) {
		const os::Point cp = os::getMouseScreenPos();
		m_captured_mouse_x = cp.x;
		m_captured_mouse_y = cp.y;
	}
	else {
		os::unclipCursor();
		os::setMouseScreenPos(m_captured_mouse_x, m_captured_mouse_y);
	}
}

void GameView::onSettingsLoaded() {
	m_is_open = m_app.getSettings().getValue("is_game_view_open", false);
}

void GameView::onBeforeSettingsSaved() {
	m_app.getSettings().setValue("is_game_view_open", m_is_open);
}

void GameView::onFullscreenGUI()
{
	processInputEvents();

	ImGuiIO& io = ImGui::GetIO();
	bool open = true;
	ImVec2 size = io.DisplaySize;
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
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
		m_pipeline->render(false);
		const gpu::TextureHandle texture_handle = m_pipeline->getOutput();
		if (gpu::isOriginBottomLeft())
		{
			ImGui::Image(texture_handle, size, ImVec2(0, 1), ImVec2(1, 0));
		}
		else
		{
			ImGui::Image(texture_handle, size);
		}
	}
	else {
		ImGuiEx::Rect(size.x, size.y, 0xff0000FF);
	}
	m_pos = ImGui::GetItemRectMin();
	m_size = ImGui::GetItemRectSize();

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
	m_app.setFullscreen(fullscreen);
	m_is_fullscreen = fullscreen;
}


void GameView::onStatsGUI(const ImVec2& view_pos)
{
	if (!m_show_stats || !m_is_open) return;
	
	float toolbar_height = 24 + ImGui::GetStyle().FramePadding.y * 2;
	ImVec2 v = view_pos;
	v.x += ImGui::GetStyle().FramePadding.x;
	v.y += ImGui::GetStyle().FramePadding.y + toolbar_height;
	ImGui::SetNextWindowPos(v);
	auto col = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
	col.w = 0.3f;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, col);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
	if (ImGui::Begin("###stats_overlay", nullptr, flags)) {
		const auto& stats = m_pipeline->getStats();
		ImGui::LabelText("Draw calls", "%d", stats.draw_call_count);
		ImGui::LabelText("Instances", "%d", stats.instance_count);
		char buf[30];
		toCStringPretty(stats.triangle_count, Span(buf));
		ImGui::LabelText("Triangles", "%s", buf);
		ImGui::LabelText("Resolution", "%dx%d", (int)m_size.x, (int)m_size.y);
	}
	ImGui::End();
	ImGui::PopStyleColor();
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
	
	Engine& engine = m_app.getEngine();
	InputSystem& input = engine.getInputSystem();
	const os::Event* events = m_app.getEvents();
	for (int i = 0, c = m_app.getEventsCount(); i < c; ++i) {
		input.injectEvent(events[i], int(m_pos.x), int(m_pos.y));
	}
}

void GameView::controlsGUI() {
	Engine& engine = m_app.getEngine();
	ImGui::PushItemWidth(50);
	if (ImGui::DragFloat("Time multiplier", &m_time_multiplier, 0.01f, 0.01f, 30.0f)) {
		engine.setTimeMultiplier(m_time_multiplier);
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


void GameView::onWindowGUI()
{
	PROFILE_FUNCTION();
	m_pipeline->setUniverse(m_editor.getUniverse());

	ImGuiIO& io = ImGui::GetIO();
	if (m_is_mouse_captured && (io.KeysDown[ImGui::GetKeyIndex(ImGuiKey_Escape)] || !m_editor.isGameMode())) {
		captureMouse(false);
	}

	const char* window_name = ICON_FA_CAMERA "Game View###game_view";
	if (m_is_mouse_captured) {
		window_name = ICON_FA_CAMERA "Game View (mouse captured)###game_view";
		os::setCursor(m_cursor_type);
	}
	
	if (m_is_fullscreen && m_pipeline->isReady()) {
		onFullscreenGUI();
		return;
	}

	if (!m_is_open) {
		captureMouse(false);
		return;
	}

	if (!m_pipeline->isReady()) captureMouse(false);

	ImVec2 view_pos;
	bool is_game_view_visible = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (ImGui::Begin(window_name, &m_is_open, ImGuiWindowFlags_NoNavInputs)) {
		is_game_view_visible = true;
		view_pos = ImGui::GetCursorScreenPos();

		const ImVec2 content_min = view_pos;
		ImVec2 size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3;
		ImVec2 content_max(content_min.x + size.x, content_min.y + size.y);
		if (m_forced_viewport.enabled) size = { (float)m_forced_viewport.width, (float)m_forced_viewport.height };
		if (size.x > 0 && size.y > 0) {
			RenderScene* scene = m_pipeline->getScene();
			const EntityPtr camera = scene->getActiveCamera();
			Viewport vp;
			if (camera.isValid()) {
				vp = scene->getCameraViewport((EntityRef)camera);
				vp.w = (int)size.x;
				vp.h = (int)size.y;
				scene->setCameraScreenSize((EntityRef)camera, vp.w, vp.h);
			}
			else {
				vp.w = (int)size.x;
				vp.h = (int)size.y;
				vp.fov = degreesToRadians(90.f);
				vp.is_ortho = false;
				vp.far = 10'000.f;
				vp.near = 1.f;
				vp.pos = DVec3(0);
				vp.rot = Quat(0, 0, 0, 1);
			}
			m_pipeline->setViewport(vp);
			m_pipeline->render(false);
			const gpu::TextureHandle texture_handle = m_pipeline->getOutput();

			if (texture_handle) {
				if (gpu::isOriginBottomLeft()) {
					ImGui::Image(texture_handle, size, ImVec2(0, 1), ImVec2(1, 0));
				}
				else {
					ImGui::Image(texture_handle, size);
				}
			}
			else {
				ImGuiEx::Rect(size.x, size.y, 0xffFF00FF);
			}
			const bool is_hovered = ImGui::IsItemHovered();
			if (is_hovered && ImGui::IsMouseClicked(0) && m_editor.isGameMode()) captureMouse(true);
			m_pos = ImGui::GetItemRectMin();
			m_size = ImGui::GetItemRectSize();

			if (m_is_mouse_captured) {
				os::clipCursor((int)m_pos.x, (int)m_pos.y, (int)m_size.x, (int)m_size.y);
			}

			processInputEvents();
			controlsGUI();
		}

	}
	if (m_is_mouse_captured && os::getFocused() != ImGui::GetWindowViewport()->PlatformHandle) captureMouse(false);
	ImGui::End();
	ImGui::PopStyleVar();
	if (is_game_view_visible) onStatsGUI(view_pos);
}


} // namespace Lumix
