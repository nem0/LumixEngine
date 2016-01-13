#pragma once


#include "editor/world_editor.h"
#include "studio_lib/utils.h"
#include <bgfx/bgfx.h>


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
		bool init(Lumix::WorldEditor& editor, Lumix::Array<Action*>& actions);
		void setScene(Lumix::RenderScene* scene);
		void shutdown();
		void onGUI();
		void setWireframe(bool wireframe);

	private:
		void renderGizmos();
		void onUniverseCreated();
		void onUniverseDestroyed();

	private:
		Action* m_toggle_gizmo_step_action;
		bool m_is_mouse_hovering_window;
		bool m_is_opened;
		int m_screen_x;
		int m_screen_y;
		int m_width;
		int m_height;
		Lumix::Vec2 m_last_mouse_pos;
		float m_camera_speed;
		Lumix::WorldEditor* m_editor;
		Lumix::Pipeline* m_pipeline;
		bgfx::TextureHandle m_texture_handle;
};