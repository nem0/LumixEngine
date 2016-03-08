#pragma once


#include "editor/world_editor.h"
#include "editor/utils.h"
#include <bgfx/bgfx.h>


class LogUI;


namespace Lumix
{
	class Pipeline;
	class Pipeline;
	class RenderScene;
}


class SceneView
{
	public:
		SceneView();
		~SceneView();

		void update();
		bool init(LogUI& log_ui, Lumix::WorldEditor& editor, Lumix::Array<Action*>& actions);
		void setScene(Lumix::RenderScene* scene);
		void shutdown();
		void onGUI();
		void setWireframe(bool wireframe);
		Lumix::Pipeline* getPipeline() { return m_pipeline; }

	private:
		void renderGizmos();
		void renderIcons();
		void onUniverseCreated();
		void onUniverseDestroyed();
		void captureMouse(bool capture);

	private:
		bool m_is_mouse_captured;
		Action* m_toggle_gizmo_step_action;
		bool m_is_mouse_hovering_window;
		bool m_is_opened;
		int m_screen_x;
		int m_screen_y;
		int m_width;
		int m_height;
		float m_camera_speed;
		Lumix::WorldEditor* m_editor;
		Lumix::Pipeline* m_pipeline;
		bgfx::TextureHandle m_texture_handle;
		bool m_show_stats;
		LogUI* m_log_ui;
};