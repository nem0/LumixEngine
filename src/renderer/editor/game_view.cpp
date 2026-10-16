#include <imgui/imgui.h>

#include "core/delegate.h"
#include "core/geometry.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/string.h"
#include "core/profiler.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/input_system.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "game_view.h"
#include "renderer/draw_stream.h"
#include "renderer/gpu/gpu.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/texture.h"
#include "ui/ui_system.h"


namespace Lumix
{

namespace {

struct Resolution {
	const char* label;
	int w;
	int h;
};

const Resolution RESOLUTIONS[] = {
	{ "Fit window", 0, 0 },
	{ "720p (1280x720)", 1280, 720 },
	{ "Steam Deck 16:10 (1280x800)", 1280, 800 },
	{ "Laptop (1366x768)", 1366, 768 },
	{ "Full HD (1920x1080)", 1920, 1080 },
	{ "WUXGA 16:10 (1920x1200)", 1920, 1200 },
	{ "1440p (2560x1440)", 2560, 1440 },
	{ "WQXGA 16:10 (2560x1600)", 2560, 1600 },
	{ "4K (3840x2160)", 3840, 2160 },
	{ "8K (7680x4320)", 7680, 4320 },
	{ "Ultrawide QHD 21:9 (3440x1440)", 3440, 1440 },
	{ "Super ultrawide 32:9 (5120x1440)", 5120, 1440 },
	{ "Square (1024x1024)", 1024, 1024 },
	{ "Portrait HD (1080x1920)", 1080, 1920 },
};

} // anonymous namespace

bool GameView::getForcedSize(int& w, int& h) const {
	if (m_forced_viewport.enabled) {
		w = m_forced_viewport.width;
		h = m_forced_viewport.height;
		return true;
	}
	if (m_resolution_idx > 0 && m_resolution_idx < (int)lengthOf(RESOLUTIONS)) {
		w = RESOLUTIONS[m_resolution_idx].w;
		h = RESOLUTIONS[m_resolution_idx].h;
		return true;
	}
	return false;
}



GameView::GameView(StudioApp& app)
	: m_app(app)
	, m_is_open(false)
	, m_is_fullscreen(false)
	, m_is_mouse_captured(false)
	, m_is_ingame_cursor(false)
	, m_time_multiplier(1.0f)
{
	m_app.getSettings().registerOption("game_view_open", &m_is_open);
	m_app.getSettings().registerOption("game_view_focus_on_game_start", &m_focus_on_game_start, "Game view", "Focus on game start");
	m_app.getSettings().registerOption("game_view_capture_mouse_on_game_start", &m_capture_mouse_on_game_start, "Game view", "Capture mouse on game start");
	m_app.getSettings().registerOption("game_view_merged_with_scene_view", &m_game_view_merged_with_scene_view, "Game view", "Merge game and scene view");
}


void GameView::init() {
	Engine& engine = m_app.getEngine();
	auto* renderer = (Renderer*)engine.getSystemManager().getSystem("renderer");
	m_pipeline = Pipeline::create(*renderer, PipelineType::GAME_VIEW);
}

bool GameView::makeScreenshot(StringView path, int width, int height) {
	if (width > 0 && height > 0) {
		m_pipeline->setWorld(m_app.getWorldEditor().getWorld());
		setupViewport(width, height);
	}
	const IVec2 size = m_pipeline->getDisplaySize();
	if (size.x <= 0 || size.y <= 0) return false;

	// render() can bail out (no world, empty viewport) and leave a stale, already released output behind
	if (!m_pipeline->render(false)) {
		logError("Failed to render the game view when trying to make a screenshot.");
		return false;
	}
	const gpu::TextureHandle texture = m_pipeline->getOutput();
	if (!texture) {
		logError("Failed to get the game view pipeline output when trying to make a screenshot.");
		return false;
	}

	struct Callback {
		StaticString<MAX_PATH> path;
		StudioApp* app;
		IVec2 size;
		IAllocator* allocator;

		void callback(Span<const u8> data) {
			if (data.length() == 0) {
				logError("Failed to read back the screenshot of ", path);
				LUMIX_DELETE(*allocator, this);
				return;
			}
			FileSystem& fs = app->getEngine().getFileSystem();
			os::OutputFile file;
			if (!fs.open(path, file)) {
				logError("Could not save ", path);
				LUMIX_DELETE(*allocator, this);
				return;
			}

			const bool res = Texture::saveTGA(&file, size.x, size.y, gpu::TextureFormat::RGBA8, data.begin(), true, Path(path), *allocator);
			file.close();
			if (!res) logError("Could not save ", path);
			LUMIX_DELETE(*allocator, this);
		}
	};

	Callback* cb = LUMIX_NEW(m_app.getAllocator(), Callback);
	cb->allocator = &m_app.getAllocator();
	cb->path = path;
	cb->size = size;
	cb->app = &m_app;

	auto delegate = makeDelegate<&Callback::callback>(cb);
	m_pipeline->getRenderer().getDrawStream().readTexture(texture, delegate);
	return true;
}


void GameView::setupViewport(int w, int h) {
	RenderModule* module = m_pipeline->getModule();
	const EntityPtr camera = module->getActiveCamera();
	Viewport vp;
	if (camera.isValid()) {
		vp = module->getCameraViewport((EntityRef)camera);
		vp.w = w;
		vp.h = h;
		module->setCameraScreenSize((EntityRef)camera, vp.w, vp.h);
	}
	else {
		vp.w = w;
		vp.h = h;
		vp.fov = degreesToRadians(90.f);
		vp.is_ortho = false;
		vp.far = 10'000.f;
		vp.near = 1.f;
		vp.pos = DVec3(0);
		vp.rot = Quat(0, 0, 0, 1);
	}
	m_pipeline->setViewport(vp);
}

// used when rendering is forced but the game view window did not render this frame (closed or hidden)
void GameView::renderForCapture() {
	const IVec2 display_size = m_pipeline->getDisplaySize();
	int w = display_size.x;
	int h = display_size.y;
	getForcedSize(w, h);
	if (w <= 0 || h <= 0) return;
	m_pipeline->setWorld(m_app.getWorldEditor().getWorld());
	setupViewport(w, h);
	m_pipeline->render(false);
	m_was_rendered = true;
}

void GameView::setCursor(os::CursorType type)
{
	m_cursor_type = type;
}

void GameView::captureMouse(bool capture) {
	if (m_is_mouse_captured == capture) return;

	m_app.setCaptureInput(capture);
	m_is_mouse_captured = capture;
	os::showCursor(!capture || m_is_ingame_cursor);
	if (capture) {
		m_app.clipMouseCursor();
	} else {
		Engine& engine = m_app.getEngine();
		InputSystem& input = engine.getInputSystem();
		input.resetDownKeys();
		m_app.unclipMouseCursor();
	}
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

	if (m_app.checkShortcut(m_fullscreen_action)) toggleFullscreen();
	
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
		m_was_rendered = true;
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
	m_input_scale = Vec2(1.f);

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

void GameView::processInputEvents() {
	if (!m_is_mouse_captured) return;

	Engine& engine = m_app.getEngine();
	InputSystem& input = engine.getInputSystem();
	const os::Point cp = os::getMouseScreenPos();
	const Vec2 mouse_pos((cp.x - int(m_pos.x)) * m_input_scale.x, (cp.y - int(m_pos.y)) * m_input_scale.y);
	for (const os::Event& e : m_app.getEvents()) {
		input.injectEvent(e, mouse_pos);
	}
}

void GameView::controlsGUI(WorldEditor& editor) {
	if (m_game_view_merged_with_scene_view) return;
	
	Engine& engine = m_app.getEngine();
	const ImGuiStyle& style = ImGui::GetStyle();
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("30.00x").x + style.FramePadding.x * 2);
	if (ImGui::DragFloat("##time_multiplier", &m_time_multiplier, 0.01f, 0.01f, 30.0f, "%.2fx")) {
		engine.setTimeMultiplier(m_time_multiplier);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Time multiplier");

	ImGui::SameLine();
	const bool is_forced_by_script = m_forced_viewport.enabled;
	// the preview shows the actual render resolution, even when it follows the window
	StaticString<64> preview_label;
	if (is_forced_by_script) {
		preview_label.append(m_forced_viewport.width, "x", m_forced_viewport.height, " (forced)");
	}
	else {
		preview_label = RESOLUTIONS[m_resolution_idx].label;
		const IVec2 display_size = m_pipeline->getDisplaySize();
		if (m_resolution_idx == 0 && display_size.x > 0 && display_size.y > 0) {
			preview_label.append(" (", display_size.x, "x", display_size.y, ")");
		}
	}
	// wide enough for the current text and the arrow
	ImGui::SetNextItemWidth(ImGui::CalcTextSize(preview_label.data).x + style.FramePadding.x * 2 + ImGui::GetFrameHeight());
	ImGui::BeginDisabled(is_forced_by_script);
	if (ImGui::BeginCombo("##resolution", preview_label.data)) {
		for (int i = 0; i < (int)lengthOf(RESOLUTIONS); ++i) {
			if (ImGui::Selectable(RESOLUTIONS[i].label, i == m_resolution_idx)) m_resolution_idx = i;
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip(is_forced_by_script ? "Resolution is forced by a script" : "Game view resolution");
	}

	ImGui::SameLine();
	if (ImGui::Button("Debug"))ImGui::OpenPopup("Debug");
	if (ImGui::BeginPopup("Debug")) {
		auto option = [&](const char* label, Pipeline::DebugShow value) {
			if (ImGui::RadioButton(label, m_pipeline->m_debug_show == value)) {
				m_pipeline->m_debug_show = value;
				m_pipeline->m_debug_show_plugin = nullptr;
			}
		};
		option("No debug", Pipeline::DebugShow::NONE);
		option("Albedo", Pipeline::DebugShow::ALBEDO);
		option("Normal", Pipeline::DebugShow::NORMAL);
		option("Roughness", Pipeline::DebugShow::ROUGHNESS);
		option("Metallic", Pipeline::DebugShow::METALLIC);
		option("Velocity", Pipeline::DebugShow::VELOCITY);
		option("Light clusters", Pipeline::DebugShow::LIGHT_CLUSTERS);
		option("Probe clusters", Pipeline::DebugShow::PROBE_CLUSTERS);
		option("AO", Pipeline::DebugShow::AO);
		Renderer& renderer = m_pipeline->getRenderer();
		for (Lumix::RenderPlugin* plugin : renderer.getPlugins()) {
			plugin->debugUI(*m_pipeline);
		}
		ImGui::EndPopup();
	}

}


void GameView::onGUI() {
	m_was_rendered = false;
	if (!m_game_view_merged_with_scene_view) {
		const char* window_label = ICON_FA_CAMERA "Game View###game_view";
		if (m_is_mouse_captured) {
			window_label = ICON_FA_CAMERA "Game View (mouse captured)###game_view";
		}

		windowUI(window_label);
	}
	if (m_force_render && !m_was_rendered) renderForCapture();
}

void GameView::windowUI(const char* window_name) {
	PROFILE_FUNCTION();

	Engine& engine = m_app.getEngine();
	const UISystem* ui_system = (const UISystem*)engine.getSystemManager().getSystem("ui");
	const bool enable = ui_system && ui_system->isCursorEnabled();
	if (m_is_ingame_cursor != enable) {
		m_is_ingame_cursor = enable;
		if (m_is_mouse_captured) {
			os::showCursor(m_is_ingame_cursor);
		}
	}

	WorldEditor& editor = m_app.getWorldEditor();
	m_pipeline->setWorld(editor.getWorld());

	if (m_app.checkShortcut(m_toggle_ui, true)) m_is_open = !m_is_open;

	const bool is_game_mode = m_app.getWorldEditor().isGameMode();
	if (is_game_mode && !m_was_game_mode && m_focus_on_game_start) {
		ImGui::SetNextWindowFocus();
		m_is_open = true;
		if (m_capture_mouse_on_game_start) {
			captureMouse(true);
		}
	}
	m_was_game_mode = is_game_mode;

	if (m_is_mouse_captured && (ImGui::IsKeyDown(ImGuiKey_Escape) || !editor.isGameMode() || !m_app.isMouseCursorClipped())) {
		captureMouse(false);
	}

	if (m_is_fullscreen) {
		onFullscreenGUI(editor);
		return;
	}

	if (!m_is_open && !m_game_view_merged_with_scene_view) {
		captureMouse(false);
		return;
	}

	ImVec2 view_size;
	bool is_game_view_visible = false;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	if (ImGui::Begin(window_name, &m_is_open, ImGuiWindowFlags_NoNavInputs)) {
		if (m_app.checkShortcut(m_fullscreen_action)) toggleFullscreen();

		is_game_view_visible = true;

		const ImVec2 content_min = ImGui::GetCursorScreenPos();
		view_size = ImGui::GetContentRegionAvail();
		if (!m_game_view_merged_with_scene_view) view_size.y -= ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3;
		ImVec2 content_max(content_min.x + view_size.x, content_min.y + view_size.y);
		// size of the image on screen, differs from the render resolution when it is forced
		ImVec2 display_size = view_size;
		int forced_w, forced_h;
		if (getForcedSize(forced_w, forced_h)) {
			view_size = { (float)forced_w, (float)forced_h };
			// the render resolution is fixed, scale the preview to fit the window and keep the aspect ratio
			if (view_size.x > 0 && view_size.y > 0) {
				const float scale = maximum(0.f, minimum(display_size.x / view_size.x, display_size.y / view_size.y));
				display_size = ImVec2(view_size.x * scale, view_size.y * scale);
			}
		}
		if (view_size.x > 0 && view_size.y > 0) {
			setupViewport((int)view_size.x, (int)view_size.y);
			m_pipeline->render(false);
			const gpu::TextureHandle texture_handle = m_pipeline->getOutput();
			m_was_rendered = true;
			
			controlsGUI(editor);

			const ImVec2 view_pos = ImGui::GetCursorScreenPos();
			if (texture_handle) {
				if (gpu::isOriginBottomLeft()) {
					ImGui::Image(texture_handle, display_size, ImVec2(0, 1), ImVec2(1, 0));
				}
				else {
					ImGui::Image(texture_handle, display_size);
				}
			}
			else {
				ImGuiEx::Rect(display_size.x, display_size.y, 0xffFF00FF);
			}
			if (m_is_mouse_captured) {
				os::Rect rect;
				rect.left = (i32)view_pos.x;
				rect.top = (i32)view_pos.y;
				rect.width = (i32)display_size.x;
				rect.height = (i32)display_size.y;
				m_app.setMouseClipRect(ImGui::GetWindowViewport()->PlatformHandle, rect);
			}

			const bool is_hovered = ImGui::IsItemHovered();
			if (is_hovered && ImGui::IsMouseReleased(0) && editor.isGameMode()) captureMouse(true);
			m_pos = ImGui::GetItemRectMin();
			m_size = ImGui::GetItemRectSize();
			// the image can be displayed at a different size than it's rendered at
			m_input_scale.x = display_size.x > 0 ? view_size.x / display_size.x : 1.f;
			m_input_scale.y = display_size.y > 0 ? view_size.y / display_size.y : 1.f;

			processInputEvents();
		}

	}

	if (m_is_mouse_captured && !is_game_view_visible) captureMouse(false);

	ImGui::End();
	ImGui::PopStyleVar();
}


} // namespace Lumix
