#pragma once


#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "renderer/gpu/gpu.h"


namespace Lumix
{


template <typename T> struct Delegate;
struct LogUI;
struct Model;
struct StudioApp;
struct Pipeline;
struct RayCastModelHit;
struct Shader;
struct Universe;


struct RenderInterfaceBase : RenderInterface {
	virtual Model* getModel(RenderInterface::ModelHandle handle) = 0;
};


struct SceneView : StudioApp::GUIPlugin
{
	public:
		using DropHandler = Delegate<bool(StudioApp&, float, float, const RayCastModelHit&)>;

	public:
		explicit SceneView(StudioApp& app);
		~SceneView();

		void update(float time_delta) override;
		void setUniverse(Universe* universe);
		void onWindowGUI() override;
		Pipeline* getPipeline() { return m_pipeline; }
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
		void handleEvents();
		void statsUI(float x, float y);

	private:
		StudioApp& m_app;
		Action* m_toggle_gizmo_step_action;
		Action* m_move_forward_action;
		Action* m_move_back_action;
		Action* m_move_left_action;
		Action* m_move_right_action;
		Action* m_move_up_action;
		Action* m_move_down_action;
		Action* m_camera_speed_action;
		bool m_is_mouse_captured;
		bool m_show_stats;
		int m_screen_x;
		int m_screen_y;
		int m_width;
		int m_height;
		int m_captured_mouse_x;
		int m_captured_mouse_y;
		float m_camera_speed;
		WorldEditor& m_editor;
		Pipeline* m_pipeline;
		LogUI& m_log_ui;
		Array<DropHandler> m_drop_handlers;
		Shader* m_debug_shape_shader;
};


} // namespace Lumix