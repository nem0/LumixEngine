#include <imgui/imgui.h>

#include "game_view.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/input_system.h"
#include "engine/lua_wrapper.h"
#include "engine/profiler.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "gui/gui_system.h"
#include "renderer/gpu/gpu.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
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
{
	Engine& engine = app.getEngine();
	auto f = &LuaWrapper::wrapMethodClosure<&GameView::forceViewport>;
	LuaWrapper::createSystemClosure(engine.getState(), "GameView", this, "forceViewport", f);
}


void GameView::init() {
	m_toggle_ui.init("Game View", "Toggle game view", "game_view", "", Action::IMGUI_PRIORITY);
	m_toggle_ui.func.bind<&GameView::onToggleOpen>(this);
	m_toggle_ui.is_selected.bind<&GameView::isOpen>(this);
	m_app.addWindowAction(&m_toggle_ui);

	m_fullscreen_action.init("Game View fullscreen", "Game View fullscreen", "game_view_fullscreen", "", Action::IMGUI_PRIORITY);
	m_app.addAction(&m_fullscreen_action);

	Engine& engine = m_app.getEngine();
	auto* renderer = (Renderer*)engine.getSystemManager().getSystem("renderer");
	PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
	m_pipeline = Pipeline::create(*renderer, pres, "GAME_VIEW");

	auto* gui = static_cast<GUISystem*>(engine.getSystemManager().getSystem("gui"));
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
	auto* gui = static_cast<GUISystem*>(engine.getSystemManager().getSystem("gui"));
	if (gui) {
		gui->setInterface(nullptr);
	}
}

bool GameView::onAction(const Action& action) {
	if (&action == &m_fullscreen_action) toggleFullscreen();
	else return false;
	return true;
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
	if (!capture) os::grabMouse(os::INVALID_WINDOW);
}

void GameView::onSettingsLoaded() {
	m_is_open = m_app.getSettings().getValue(Settings::GLOBAL, "is_game_view_open", false);
}

void GameView::onBeforeSettingsSaved() {
	m_app.getSettings().setValue(Settings::GLOBAL, "is_game_view_open", m_is_open);
}

void GameView::onFullscreenGUI(WorldEditor& editor)
{
	processInputEvents();

	ImGuiIO& io = ImGui::GetIO();
	bool open = true;
	ImVec2 size = io.DisplaySize;
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
	ImGui::SetNextWindowSize(size);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (!ImGui::Begin("game view fullscreen",
		&open,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		ImGui::PopStyleVar(2);
		return;
	}

	RenderModule* render_module = m_pipeline->getModule();
	EntityPtr camera = render_module->getActiveCamera();
	if (camera.isValid()) {
		Viewport vp = render_module->getCameraViewport((EntityRef)camera);
		vp.w = (int)size.x;
		vp.h = (int)size.y;
		render_module->setCameraScreenSize((EntityRef)camera, vp.w, vp.h);
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
	ImGui::PopStyleVar(2);

	if (m_is_fullscreen && (ImGui::IsKeyPressed(ImGuiKey_Escape) || !editor.isGameMode()))
	{
		setFullscreen(false);
	}
}


void GameView::toggleFullscreen() {
	if (!m_app.getWorldEditor().isGameMode()) return;
	setFullscreen(!m_is_fullscreen);
}


void GameView::setFullscreen(bool fullscreen)
{
	captureMouse(fullscreen);
	m_app.setFullscreen(fullscreen);
	m_is_fullscreen = fullscreen;
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
	for (const os::Event e : m_app.getEvents()) {
		input.injectEvent(e, int(m_pos.x), int(m_pos.y));
	}
}

void GameView::controlsGUI(WorldEditor& editor) {
	Engine& engine = m_app.getEngine();
	ImGui::SetNextItemWidth(50);
	if (ImGui::DragFloat("Time multiplier", &m_time_multiplier, 0.01f, 0.01f, 30.0f)) {
		engine.setTimeMultiplier(m_time_multiplier);
	}
	if(editor.isGameMode()) {
		ImGui::SameLine();
		if (ImGui::Button("Fullscreen")) setFullscreen(true);
	}
	m_pipeline->callLuaFunction("onGUI");
}


void GameView::onGUI()
{
	PROFILE_FUNCTION();
	WorldEditor& editor = m_app.getWorldEditor();
	m_pipeline->setWorld(editor.getWorld());

	const bool is_game_mode = m_app.getWorldEditor().isGameMode();
	if (is_game_mode && !m_was_game_mode) {
		ImGui::SetNextWindowFocus();
		m_is_open = true;
	}
	m_was_game_mode = is_game_mode;

	if (m_is_mouse_captured && (ImGui::IsKeyDown(ImGuiKey_Escape) || !editor.isGameMode())) {
		captureMouse(false);
	}

	const char* window_name = ICON_FA_CAMERA "Game View###game_view";
	if (m_is_mouse_captured) {
		window_name = ICON_FA_CAMERA "Game View (mouse captured)###game_view";
		os::setCursor(m_cursor_type);
	}
	
	if (m_is_fullscreen && m_pipeline->isReady()) {
		onFullscreenGUI(editor);
		return;
	}

	if (!m_is_open) {
		captureMouse(false);
		return;
	}

	if (!m_pipeline->isReady()) captureMouse(false);

	ImVec2 view_pos;
	ImVec2 view_size;
	bool is_game_view_visible = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (ImGui::Begin(window_name, &m_is_open, ImGuiWindowFlags_NoNavInputs)) {
		is_game_view_visible = true;
		view_pos = ImGui::GetCursorScreenPos();

		const ImVec2 content_min = view_pos;
		view_size = ImGui::GetContentRegionAvail();
		view_size.y -= ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3;
		ImVec2 content_max(content_min.x + view_size.x, content_min.y + view_size.y);
		if (m_forced_viewport.enabled) view_size = { (float)m_forced_viewport.width, (float)m_forced_viewport.height };
		if (view_size.x > 0 && view_size.y > 0) {
			RenderModule* module = m_pipeline->getModule();
			const EntityPtr camera = module->getActiveCamera();
			Viewport vp;
			if (camera.isValid()) {
				vp = module->getCameraViewport((EntityRef)camera);
				vp.w = (int)view_size.x;
				vp.h = (int)view_size.y;
				module->setCameraScreenSize((EntityRef)camera, vp.w, vp.h);
			}
			else {
				vp.w = (int)view_size.x;
				vp.h = (int)view_size.y;
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
			
			controlsGUI(editor);

			if (texture_handle) {
				if (gpu::isOriginBottomLeft()) {
					ImGui::Image(texture_handle, view_size, ImVec2(0, 1), ImVec2(1, 0));
				}
				else {
					ImGui::Image(texture_handle, view_size);
				}
			}
			else {
				ImGuiEx::Rect(view_size.x, view_size.y, 0xffFF00FF);
			}
			const bool is_hovered = ImGui::IsItemHovered();
			if (is_hovered && ImGui::IsMouseReleased(0) && editor.isGameMode()) captureMouse(true);
			m_pos = ImGui::GetItemRectMin();
			m_size = ImGui::GetItemRectSize();

			processInputEvents();
		}

	}

	if (m_is_mouse_captured && os::getFocused() != ImGui::GetWindowViewport()->PlatformHandle) captureMouse(false);
	if (m_is_mouse_captured && is_game_view_visible) {
		os::grabMouse(ImGui::GetWindowViewport()->PlatformHandle);
	}
	ImGui::End();
	ImGui::PopStyleVar();
}


} // namespace Lumix
