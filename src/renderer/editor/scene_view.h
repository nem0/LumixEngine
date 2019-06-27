#pragma once


#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "renderer/ffr/ffr.h"


namespace Lumix
{


class LogUI;
class StudioApp;
class Pipeline;
class RenderScene;
class Shader;


class SceneView : public StudioApp::GUIPlugin
{
	public:
		typedef Delegate<bool(StudioApp&, float, float, const RayCastModelHit&)> DropHandler;

	public:
		explicit SceneView(StudioApp& app);
		~SceneView();

		void update(float time_delta) override;
		void setScene(RenderScene* scene);
		void onWindowGUI() override;
		Pipeline* getPipeline() { return m_pipeline; }
		const ffr::TextureHandle& getTextureHandle() const { return m_texture_handle; }
		void addDropHandler(DropHandler handler);
		void removeDropHandler(DropHandler handler);
		const char* getName() const override { return "scene_view"; }

	private:
		void renderSelection();
		void renderGizmos();
		void renderIcons();
		void onUniverseCreated();
		void onUniverseDestroyed();
		void captureMouse(bool capture);
		RayCastModelHit castRay(float x, float y);
		void handleDrop(const char* path, float x, float y);
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
		bool m_is_open;
		int m_screen_x;
		int m_screen_y;
		int m_width;
		int m_height;
		int m_captured_mouse_x;
		int m_captured_mouse_y;
		float m_camera_speed;
		WorldEditor& m_editor;
		Pipeline* m_pipeline;
		ffr::TextureHandle m_texture_handle;
		bool m_show_stats;
		LogUI& m_log_ui;
		Array<DropHandler> m_drop_handlers;
		Shader* m_debug_shape_shader;
};


} // namespace Lumix