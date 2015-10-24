#pragma once


#include "editor/world_editor.h"
#include <bgfx/bgfx.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Lumix
{
	class Pipeline;
	class PipelineInstance;
	class RenderScene;
}


class GameView
{
public:
	GameView();
	~GameView();

	void init(HWND hwnd, Lumix::WorldEditor& editor);
	void shutdown();
	void onGui();
	void setScene(Lumix::RenderScene* scene);
	bool isMouseCaptured() const { return m_is_mouse_captured; }

public:
	bool m_is_opened;

private:
	void captureMouse(bool capture);
	void onUniverseCreated();
	void onUniverseDestroyed();

private:
	bool m_is_mouse_captured;
	Lumix::Pipeline* m_pipeline_source;
	Lumix::PipelineInstance* m_pipeline;
	bgfx::TextureHandle m_texture_handle;
	Lumix::WorldEditor* m_editor;
	bool m_is_mouse_hovering_window;
	HWND m_hwnd;
};