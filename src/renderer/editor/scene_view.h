#pragma once

#include "editor/action.h"
#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "editor/text_filter.h"
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

//@ object
struct LUMIX_RENDERER_API SceneView : StudioApp::GUIPlugin
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

	//@ function
	const Viewport& getViewport();
	//@ function
	void setViewport(const Viewport& vp);
	//@ function
	void makeScreenshot(StringView path);
	void setGameView(struct GameView& game_view) { m_game_view = &game_view; }

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
	Action m_insert_model_action;
	Action m_wireframe_action{"Studio", "Wireframe", "wireframe", "wireframe", "", Action::TOOL};
	
	Action m_top_view_action{"Camera", "Top", "Top view", "view_top", ""};
	Action m_side_view_action{"Camera", "Side", "Side view", "view_side", ""};
	Action m_front_view_action{"Camera", "Front", "Front view", "view_front", ""};
	Action m_toggle_projection_action{"Camera", "Ortho/perspective", "Toggle ortho/perspective projection", "toggle_projection", ""};
	Action m_look_at_selected_action{"Camera", "Look at selected", "Look at selected entity", "look_at_selected", ""};
	Action m_copy_view_action{"Camera", "Copy view transform", "Copy transform to entity", "copy_view_transform", ""};

	Action m_set_pivot_action{"Gizmo", "Set custom pivot", "Set custom pivot", "set_custom_pivot", ""};
	Action m_reset_pivot_action{"Gizmo", "Reset pivot", "Reset pivot", "reset_pivot", ""};
	Action m_anisotropic_scale_action{"Gizmo", "Enable/disable anisotropic scale", "Enable/disable anisotropic gizmo scale", "toggle_gizmo_anisotropic_scale", ""};
	Action m_use_grid_snapping_action;
	Action m_copy_move_action{"Gizmo", "Duplicate move", "Duplicate entity when moving with gizmo", "duplicate_entity_move", ""};
	Action m_translate_gizmo_mode;
	Action m_rotate_gizmo_mode;
	Action m_scale_gizmo_mode;
	Action m_grab_action;
	Action m_grab_x{"Gizmo", "X", "Grab X axis", "gizmo_grab_x", ""};
	Action m_grab_y{"Gizmo", "Y", "Grab Y axis", "gizmo_grab_y", ""};
	Action m_grab_z{"Gizmo", "Z", "Grab Z axis", "gizmo_grab_z", ""};
	Action m_local_coord_gizmo;
	Action m_global_coord_gizmo;
	
	Action m_rotate_entity_90_action{"Entity", "Rotate 90 degrees", "Rotate by 90 degrees", "rotate_90_deg", ""};
	Action m_move_entity_E_action{"Entity", "Move entity east", "Move east", "move_entity_E", ""};
	Action m_move_entity_N_action{"Entity", "Move entity north", "Move north", "move_entity_N", ""};
	Action m_move_entity_S_action{"Entity", "Move entity south", "Move south", "move_entity_S", ""};
	Action m_move_entity_W_action{"Entity", "Move entity west", "Move west", "move_entity_W", ""};
	Action m_create_entity;
	Action m_make_parent;
	Action m_unparent;
	Action m_autosnap_down{"Entity", "Autosnap down", "Toggle autosnap down", "autosnap_down", ""};
	Action m_snap_down{"Entity", "Snap down", "Snap down", "entity_snap_down", ""};
	Action m_select_parent;
	Action m_select_child;
	Action m_select_next_sibling;
	Action m_select_prev_sibling;

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
	bool m_was_game_mode = false;
	bool m_use_grid_snapping = false;

	WorldEditor& m_editor;
	struct WorldViewImpl* m_view;
	GameView* m_game_view = nullptr;

	bool m_is_measure_active = false;
	bool m_is_measure_from_set = false;
	DVec3 m_measure_to = {0, 0, 0};
	DVec3 m_measure_from = {0, 0, 0};
	
	struct RenderPlugin;
	UniquePtr<RenderPlugin> m_render_plugin;
};


} // namespace Lumix