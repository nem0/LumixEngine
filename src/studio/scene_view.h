#pragma once


#include "editor/world_editor.h"
#include <bgfx/bgfx.h>


namespace Lumix
{
	class Pipeline;
	class PipelineInstance;
	class RenderScene;
}


class SceneView
{
	public:
		SceneView();
		~SceneView();

		void update();
		bool init(Lumix::WorldEditor& editor);
		void setScene(Lumix::RenderScene* scene);
		void shutdown();
		void onGUI();
		void onMouseUp(Lumix::MouseButton::Value button);
		bool onMouseDown(int screen_x, int screen_y, Lumix::MouseButton::Value button);
		void onMouseMove(int screen_x, int screen_y, int rel_x, int rel_y);
		void setWireframe(bool wireframe);

	private:
		void renderGizmos();

	private:
		bool m_is_mouse_hovering_window;
		bool m_is_opened;
		int m_screen_x;
		int m_screen_y;
		int m_width;
		int m_height;
		float m_camera_speed;
		Lumix::WorldEditor* m_editor;
		Lumix::PipelineInstance* m_pipeline;
		Lumix::Pipeline* m_pipeline_source;
		bgfx::TextureHandle m_texture_handle;
};