#pragma once


#include "editor/world_editor.h"
#include <bgfx/bgfx.h>


struct PlatformData;
class StudioApp;


namespace Lumix
{
	class Pipeline;
	class RenderScene;
}


class GameView
{
friend struct GUIInterface;
public:
	GameView(StudioApp& app);
	~GameView();

	void init(Lumix::WorldEditor& editor);
	void shutdown();
	void onGui();
	void setScene(Lumix::RenderScene* scene);
	bool isMouseCaptured() const { return m_is_mouse_captured; }
	void captureMouse(bool capture);
	void enableIngameCursor(bool enable);

public:
	bool m_is_opened;

private:
	void onUniverseCreated();
	void onUniverseDestroyed();

private:
	Lumix::Pipeline* m_pipeline;
	Lumix::WorldEditor* m_editor;
	float m_time_multiplier;
	StudioApp& m_studio_app;
	Lumix::Vec2 m_pos;
	Lumix::Vec2 m_size;
	bgfx::TextureHandle m_texture_handle;
	struct GUIInterface* m_gui_interface;
	bool m_is_mouse_captured;
	bool m_is_mouse_hovering_window;
	bool m_is_ingame_cursor;
	bool m_paused;
	bool m_is_opengl;
	int m_captured_mouse_x, m_captured_mouse_y;
};