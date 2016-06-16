#pragma once


#include "editor/world_editor.h"
#include <bgfx/bgfx.h>


struct PlatformData;
class StudioApp;


namespace Lumix
{
	class Pipeline;
	class Pipeline;
	class RenderScene;
}


class GameView
{
public:
	GameView(StudioApp& app);
	~GameView();

	void init(Lumix::WorldEditor& editor);
	void shutdown();
	void onGui();
	void setScene(Lumix::RenderScene* scene);
	bool isMouseCaptured() const { return m_is_mouse_captured; }
	void captureMouse(bool capture);

public:
	bool m_is_opened;

private:
	void onUniverseCreated();
	void onUniverseDestroyed();

private:
	bool m_is_mouse_captured;
	Lumix::Pipeline* m_pipeline;
	Lumix::WorldEditor* m_editor;
	bool m_is_mouse_hovering_window;
	float m_time_multiplier;
	bool m_paused;
	bool m_is_opengl;
	StudioApp& m_studio_app;
	bgfx::TextureHandle m_texture_handle;
};