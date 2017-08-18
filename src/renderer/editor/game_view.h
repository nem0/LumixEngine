#pragma once


#include "editor/studio_app.h"
#include "engine/vec.h"
#include <bgfx/bgfx.h>


struct ImVec2;


namespace Lumix
{


struct PlatformData;
class StudioApp;
class Pipeline;
class RenderScene;


class GameView : public StudioApp::IPlugin
{
friend struct GUIInterface;
public:
	GameView(StudioApp& app);
	~GameView();

	void setScene(RenderScene* scene);
	bool isMouseCaptured() const { return m_is_mouse_captured; }
	void captureMouse(bool capture);
	void enableIngameCursor(bool enable);
	void forceViewport(bool enable, int w, int h);
	const char* getName() const override { return "game_view"; }
	bool isOpened() const { return m_is_open; }
	void onAction() { m_is_open = !m_is_open; }
	void onWindowGUI() override;
	const bgfx::TextureHandle& getTextureHandle() const { return m_texture_handle; }

public:
	bool m_is_open;

private:
	void onUniverseCreated();
	void onUniverseDestroyed();
	void onFullscreenGUI();
	void setFullscreen(bool fullscreen);
	void onStatsGUI(const ImVec2& view_pos);
	

private:
	Pipeline* m_pipeline;
	WorldEditor& m_editor;
	float m_time_multiplier;
	StudioApp& m_studio_app;
	Vec2 m_pos;
	Vec2 m_size;
	bgfx::TextureHandle m_texture_handle;
	struct GUIInterface* m_gui_interface;
	bool m_is_mouse_captured;
	bool m_is_mouse_hovering_window;
	bool m_is_ingame_cursor;
	bool m_paused;
	bool m_is_opengl;
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