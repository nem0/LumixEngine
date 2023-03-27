#pragma once


#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/allocator.h"
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
struct World;

struct SceneView : StudioApp::GUIPlugin
{
	friend struct WorldViewImpl;
	explicit SceneView(StudioApp& app);
	~SceneView();

	void update(float time_delta) override;
	void setWorld(World* world);
	void onWindowGUI() override;
	Pipeline* getPipeline() { return m_pipeline.get(); }
	const char* getName() const override { return "scene_view"; }
	void init();

private:
	void manipulate();
	void renderSelection();
	void renderGizmos();
	void renderIcons();
	void captureMouse(bool capture);
	RayCastModelHit castRay(float x, float y);
	void handleDrop(const char* path, float x, float y);
	void onToolbar();
	void handleEvents();
	void statsUI(float x, float y);
	void toggleSearch() { m_search_request = true; }
	void searchUI();
	void onSettingsLoaded() override;
	void onBeforeSettingsSaved() override;
	void setTopView();
	void setSideView();
	void setFrontView();
	void toggleProjection();
	void lookAtSelected();
	void copyViewTransform();

private:
	StudioApp& m_app;
	Action m_search_action;
	Action m_orbit_action;
	Action m_toggle_gizmo_step_action;
	Action m_copy_move_action;
	Action m_move_forward_action;
	Action m_move_back_action;
	Action m_move_left_action;
	Action m_move_right_action;
	Action m_move_up_action;
	Action m_move_down_action;
	Action m_set_pivot_action;
	Action m_reset_pivot_action;
	Action m_top_view_action;
	Action m_side_view_action;
	Action m_front_view_action;
	Action m_toggle_projection_action;
	Action m_look_at_selected_action;
	Action m_copy_view_action;
	bool m_is_mouse_captured = false;
	bool m_copy_moved = false;
	bool m_show_stats = false;
	bool m_search_request = false;
	bool m_search_preview = false;
	bool m_search_actions = true;
	bool m_search_models = true;
	char m_search_buf[128] = "";
	i32 m_search_selected = 0;
	int m_screen_x;
	int m_screen_y;
	int m_width;
	int m_height;
	int m_captured_mouse_x;
	int m_captured_mouse_y;
	float m_camera_speed = 0.1f;
	UniquePtr<Pipeline> m_pipeline;
	LogUI& m_log_ui;
	Shader* m_debug_shape_shader;
	
	WorldEditor& m_editor;
	struct WorldViewImpl* m_view;

	bool m_is_measure_active = false;
	bool m_is_measure_from_set = false;
	DVec3 m_measure_to = {0, 0, 0};
	DVec3 m_measure_from = {0, 0, 0};
};


} // namespace Lumix