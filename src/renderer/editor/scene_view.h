#pragma once


#include "editor/world_editor.h"
#include "editor/utils.h"
#include <bgfx/bgfx.h>


class LogUI;
class StudioApp;


namespace Lumix
{
	class Pipeline;
	class Pipeline;
	class RenderScene;
}


class SceneView
{
	public:
		SceneView(StudioApp& app);
		~SceneView();

		void update();
		void setScene(Lumix::RenderScene* scene);
		void shutdown();
		void onGUI();
		Lumix::Pipeline* getPipeline() { return m_pipeline; }

	private:
		void renderGizmos();
		void renderIcons();
		void onUniverseCreated();
		void onUniverseDestroyed();
		void captureMouse(bool capture);
		Lumix::RayCastModelHit castRay(float x, float y);
		void handleDrop(float x, float y);
		void onToolbar();
		void resetCameraSpeed();

	private:
		StudioApp& m_app;
		bool m_is_mouse_captured;
		Action* m_toggle_gizmo_step_action;
		Action* m_move_forward_action;
		Action* m_move_back_action;
		Action* m_move_left_action;
		Action* m_move_right_action;
		Action* m_move_up_action;
		Action* m_move_down_action;
		Action* m_camera_speed_action;
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
		bool m_is_opengl;
		LogUI* m_log_ui;
};