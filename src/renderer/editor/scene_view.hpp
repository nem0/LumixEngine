#pragma once


#include "editor/render_interface.hpp"
#include "editor/studio_app.hpp"
#include "editor/utils.hpp"
#include "core/allocator.hpp"
#include "renderer/gpu/gpu.hpp"


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
	void onGUI() override;
	Pipeline* getPipeline() { return m_pipeline.get(); }
	const char* getName() const override { return "scene_view"; }
	void init();

	DVec3 getViewportPosition();
	void setViewportPosition(const DVec3& pos);
	Quat getViewportRotation();
	void setViewportRotation(const Quat& rot);

private:
	bool onAction(const Action& action) override;
	void manipulate();
	void renderSelection();
	void renderGizmos();
	void renderIcons();
	void captureMouse(bool capture);
	RayCastModelHit castRay(float x, float y);
	void handleDrop(const char* path, float x, float y);
	void onToolbar();
	void handleEvents();
	void insertModelUI();
	void onSettingsLoaded() override;
	void onBeforeSettingsSaved() override;
	void toggleProjection();
	bool hasFocus() const override { return m_has_focus; }
	void rotate90Degrees();
	void moveEntity(Vec2 v);

private:
	StudioApp& m_app;
	Action m_insert_model_action;
	Action m_set_pivot_action;
	Action m_toggle_gizmo_step_action;
	Action m_copy_move_action;
	Action m_reset_pivot_action;
	Action m_top_view_action;
	Action m_side_view_action;
	Action m_front_view_action;
	Action m_toggle_projection_action;
	Action m_look_at_selected_action;
	Action m_copy_view_action;
	Action m_rotate_entity_90_action;
	Action m_move_entity_N_action;
	Action m_move_entity_S_action;
	Action m_move_entity_E_action;
	Action m_move_entity_W_action;
	bool m_has_focus = false;
	bool m_is_mouse_captured = false;
	bool m_copy_moved = false;
	bool m_insert_model_request = false;
	bool m_search_preview = false;
	TextFilter m_filter;
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