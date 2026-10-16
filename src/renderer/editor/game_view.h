#pragma once

#include "editor/studio_app.h"
#include "editor/action.h"
#include "core/allocator.h"
#include "core/math.h"
#include "core/os.h"
#include "renderer/gpu/gpu.h"

struct ImVec2;

namespace Lumix {

struct Path;
struct Pipeline;
struct PlatformData;
struct RenderModule;
struct StringView;
struct StudioApp;

//@ object
struct LUMIX_RENDERER_API GameView : StudioApp::GUIPlugin {
	explicit GameView(StudioApp& app);

	bool isMouseCaptured() const { return m_is_mouse_captured; }
	void captureMouse(bool capture);
	void setCursor(os::CursorType type);
	//@ function
	void forceViewport(bool enable, int w, int h);
	// Queues a TGA screenshot of the game view (including the in-game UI) to a project-relative path. Returns false if the game view has no size yet
	// (e.g. its window has never been visible). The file is written asynchronously after the GPU read-back.
	// If width and height are both > 0, this capture is rendered at that size; the window size applies again on the next GUI frame.
	bool makeScreenshot(StringView path, int width = 0, int height = 0);
	// If enabled, the game view is rendered every frame even if its window is closed or hidden (e.g. for video recording).
	//@ function
	void setForceRender(bool enable) { m_force_render = enable; }
	Pipeline* getPipeline() { return m_pipeline.get(); }
	const char* getName() const override { return "game_view"; }
	bool isOpen() const { return m_is_open; }
	void onToggleOpen() { m_is_open = !m_is_open; }
	void onGUI() override;
	void windowUI(const char* window_name);
	void init();

	bool m_is_open;
	bool m_game_view_merged_with_scene_view = false;
	bool m_capture_mouse_on_game_start = false;
	Action m_posses_game_camera{"Game View", "Posses", "Posses game camera", "game_view_posses", ICON_FA_MALE};

private:
	void toggleFullscreen();
	void setupViewport(int w, int h);
	void renderForCapture();
	void processInputEvents();
	void onFullscreenGUI(WorldEditor& editor);
	void setFullscreen(bool fullscreen);
	void controlsGUI(WorldEditor& editor);
	bool getForcedSize(int& w, int& h) const;

	UniquePtr<Pipeline> m_pipeline;
	StudioApp& m_app;
	float m_time_multiplier;
	int m_resolution_idx = 0;	// 0 = match window size
	Vec2 m_pos = Vec2(0);
	Vec2 m_size = Vec2(0);
	Vec2 m_input_scale = Vec2(1.f);	// render size / displayed size, used to remap mouse position
	bool m_is_mouse_captured;
	bool m_is_ingame_cursor;
	bool m_is_fullscreen;
	bool m_was_game_mode = false;
	bool m_focus_on_game_start = false;
	os::CursorType m_cursor_type = os::CursorType::DEFAULT;
	bool m_force_render = false;
	bool m_was_rendered = false;	// the pipeline was rendered since the beginning of `onGUI`
	struct
	{
		bool enabled = false;
		int width;
		int height;
	} m_forced_viewport;
	Action m_toggle_ui{"Game View", "Game View", "Toggle UI", "game_view_toggle_ui", "", Action::WINDOW};
	Action m_fullscreen_action{"Game View", "Fullscreen", "Fullscreen", "game_view_fullscreen", ""};
};


} // namespace Lumix