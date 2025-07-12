#pragma once


#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "core/allocator.h"
#include "renderer/gpu/gpu.h"
#include "renderer/pipeline.h"


namespace Lumix
{


template <typename T> struct Delegate;
struct LogUI;
struct Model;
struct StudioApp;
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
	void cameraPreviewGUI(Vec2 size);
	void manipulate();
	void captureMouse(bool capture);
	RayCastModelHit castRay(float x, float y);
	void handleDrop(const char* path, float x, float y);
	void onToolbar();
	void handleEvents();
	void insertModelUI();
	void toggleProjection();
	void rotate90Degrees();
	void moveEntity(Vec2 v);
	void snapDown();
	void toggleWireframe();

private:
	StudioApp& m_app;
	Local<Action> m_debug_show_actions[(u32)Pipeline::DebugShow::BUILTIN_COUNT];
	Action m_copy_move_action{"Duplicate move", "Duplicate entity when moving with gizmo", "duplicate_entity_move", ""};
	Action m_anisotropic_scale_action{"Enable/disable anisotropic scale", "Enable/disable anisotropic gizmo scale", "toggle_gizmo_anisotropic_scale", ""};
	Action m_toggle_gizmo_step_action{"Enable/disable gizmo step", "Enable/disable gizmo step", "toggle_gizmo_step", ""};
	Action m_set_pivot_action{"Set custom pivot", "Set custom pivot", "set_custom_pivot", ""};
	Action m_reset_pivot_action{"Reset pivot", "Reset pivot", "reset_pivot", ""};
	Action m_insert_model_action{"Insert model", "Insert model or prefab", "insert_model", ICON_FA_SEARCH};
	Action m_top_view_action{"Top", "Camera - top view", "view_top", ""};
	Action m_side_view_action{"Side", "Camera - side view", "view_side", ""};
	Action m_front_view_action{"Front", "Camera - front view", "view_front", ""};
	Action m_toggle_projection_action{"Ortho/perspective", "Camera - toggle ortho/perspective projection", "toggle_projection", ""};
	Action m_look_at_selected_action{"Look at selected", "Camera - look at selected entity", "look_at_selected", ""};
	Action m_copy_view_action{"Copy view transform", "Camera - copy transform to entity", "copy_view_transform", ""};
	Action m_rotate_entity_90_action{"Rotate 90 degrees", "Entity - Rotate by 90 degrees", "rotate_90_deg", ""};
	Action m_move_entity_E_action{"Move entity east", "Entity - move east", "move_entity_E", ""};
	Action m_move_entity_N_action{"Move entity north", "Entity - move north", "move_entity_N", ""};
	Action m_move_entity_S_action{"Move entity south", "Entity - move south", "move_entity_S", ""};
	Action m_move_entity_W_action{"Move entity west", "Entity - move west", "move_entity_W", ""};
	Action m_translate_gizmo_mode{"Translate", "Gizmo - translate mode", "gizmo_translate_mode", ICON_FA_ARROWS_ALT};
	Action m_rotate_gizmo_mode{"Rotate", "Gizmo - rotate mode", "gizmo_rotate_mode", ICON_FA_UNDO};
	Action m_scale_gizmo_mode{"Scale", "Gizmo - scale mode", "gizmo_scale_mode", ICON_FA_EXPAND_ALT};
	Action m_grab_action{"Grab", "Gizmo - grab mode", "gizmo_grab_mode", ICON_FA_HAND_PAPER};
	Action m_grab_x{"X", "Gizmo - grab X axis", "gizmo_grab_x", ""};
	Action m_grab_y{"Y", "Gizmo - grab Y axis", "gizmo_grab_y", ""};
	Action m_grab_z{"Z", "Gizmo - grab Z axis", "gizmo_grab_z", ""};
	Action m_local_coord_gizmo{"Local", "Gizmo - local transform system", "gizmo_local_coord", ICON_FA_HOME};
	Action m_global_coord_gizmo{"Global", "Gizmo - global transform system", "gizmo_global_coord", ICON_FA_GLOBE};
	Action m_create_entity{"Create empty", "Entity - create new", "entity_create", ICON_FA_PLUS_SQUARE};
	Action m_make_parent{"Make parent", "Entity - make parent", "entity_parent", ICON_FA_OBJECT_GROUP};
	Action m_unparent{"Unparent", "Entity - unparent", "entity_unparent", ICON_FA_OBJECT_UNGROUP};
	Action m_autosnap_down{"Autosnap down", "Entity - toggle autosnap down", "autosnap_down", ""};
	Action m_snap_down{"Snap down", "Entity - snap down", "entity_snap_down", ""};
	Action m_select_parent{"Select parent", "Entity - select parent", "entity_select_parent", ICON_FA_ARROW_UP};
	Action m_select_child{"Select first child", "Entity - select first child", "entity_select_first_child", ICON_FA_ARROW_DOWN};
	Action m_select_next_sibling{"Select next sibling", "Entity - select next sibling", "entity_select_next_sibling", ICON_FA_ARROW_RIGHT};
	Action m_select_prev_sibling{"Select previous sibling", "Entity - select previous sibling", "entity_select_prev_sibling", ICON_FA_ARROW_LEFT};
	Action m_wireframe_action{"Wireframe", "Tools - wireframe", "wireframe", "", Action::TOOL};

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
	UniquePtr<Pipeline> m_camera_preview_pipeline;
	LogUI& m_log_ui;
	bool m_show_camera_preview = true;
	bool m_mouse_wheel_changes_speed = true;
	
	WorldEditor& m_editor;
	struct WorldViewImpl* m_view;

	bool m_is_measure_active = false;
	bool m_is_measure_from_set = false;
	DVec3 m_measure_to = {0, 0, 0};
	DVec3 m_measure_from = {0, 0, 0};
	
	struct RenderPlugin;
	UniquePtr<RenderPlugin> m_render_plugin;
};


} // namespace Lumix