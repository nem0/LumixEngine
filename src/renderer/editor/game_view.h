#pragma once

#include "editor/studio_app.h"
#include "editor/utils.h"
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
struct StudioApp;

struct LUMIX_RENDERER_API GameView : StudioApp::GUIPlugin {
	friend struct GUIInterface;

	explicit GameView(StudioApp& app);
	~GameView();

	bool isMouseCaptured() const { return m_is_mouse_captured; }
	void captureMouse(bool capture);
	void enableIngameCursor(bool enable);
	void setCursor(os::CursorType type);
	void forceViewport(bool enable, int w, int h);
	const char* getName() const override { return "game_view"; }
	bool isOpen() const { return m_is_open; }
	void onToggleOpen() { m_is_open = !m_is_open; }
	void onGUI() override;
	void init();

	bool m_is_open;

private:
	void toggleFullscreen();
	void processInputEvents();
	void onFullscreenGUI(WorldEditor& editor);
	void setFullscreen(bool fullscreen);
	void controlsGUI(WorldEditor& editor);

	UniquePtr<Pipeline> m_pipeline;
	StudioApp& m_app;
	float m_time_multiplier;
	Vec2 m_pos = Vec2(0);
	Vec2 m_size = Vec2(0);
	UniquePtr<struct GUIInterface> m_gui_interface;
	bool m_is_mouse_captured;
	bool m_is_ingame_cursor;
	bool m_is_fullscreen;
	bool m_was_game_mode = false;
	bool m_focus_on_game_start = false;
	os::CursorType m_cursor_type = os::CursorType::DEFAULT;
	struct
	{
		bool enabled = false;
		int width;
		int height;
	} m_forced_viewport;
	Action m_toggle_ui{"Game View", "Game view - toggle UI", "game_view_toggle_ui", "", Action::WINDOW};
	Action m_fullscreen_action{"Fullscreen", "Game view - fullscreen", "game_view_fullscreen", ""};
};


} // namespace Lumix