#pragma once


#include "editor/studio_app.h"
#include "engine/math.h"
#include "renderer/gpu/gpu.h"


struct ImVec2;


namespace Lumix
{


struct Path;
struct Pipeline;
struct PlatformData;
struct RenderScene;
struct StudioApp;


struct GameView : StudioApp::GUIPlugin
{
friend struct GUIInterface;
public:
	explicit GameView(StudioApp& app);
	~GameView();

	bool isMouseCaptured() const { return m_is_mouse_captured; }
	void captureMouse(bool capture);
	void enableIngameCursor(bool enable);
	void forceViewport(bool enable, int w, int h);
	const char* getName() const override { return "game_view"; }
	bool isOpen() const { return m_is_open; }
	void onAction() { m_is_open = !m_is_open; }
	void onWindowGUI() override;

public:
	bool m_is_open;

private:
	void onSettingsLoaded() override;
	void onBeforeSettingsSaved() override;
	void toggleFullscreen();
	void processInputEvents();
	void onFullscreenGUI();
	void setFullscreen(bool fullscreen);
	void onStatsGUI(const ImVec2& view_pos);
	void controlsGUI();

private:
	Pipeline* m_pipeline;
	WorldEditor& m_editor;
	StudioApp& m_app;
	float m_time_multiplier;
	Vec2 m_pos;
	Vec2 m_size;
	struct GUIInterface* m_gui_interface;
	bool m_is_mouse_captured;
	bool m_is_ingame_cursor;
	bool m_paused;
	bool m_is_fullscreen;
	bool m_show_stats;
	struct
	{
		bool enabled = false;
		int width;
		int height;
	} m_forced_viewport;
	int m_captured_mouse_x, m_captured_mouse_y;
};


} // namespace Lumix