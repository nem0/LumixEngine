#pragma once


#include "engine/array.h"
#include "engine/math.h"
#include "editor/studio_app.h"
#include "engine/file_system.h"
#include "engine/path.h"
#include "engine/world.h"
#include "editor/world_editor.h"
#include "editor/utils.h"
#include "renderer/gpu/gpu.h"


namespace Lumix
{


struct Material;
struct Model;
struct PrefabResource;
struct RenderModule;
struct Texture;
struct Renderer;

struct TerrainEditor final : StudioApp::MousePlugin {
	enum class Mode {
		LAYER,
		HEIGHT,
		ENTITY
	};

	enum ActionType
	{
		RAISE_HEIGHT,
		LOWER_HEIGHT,
		SMOOTH_HEIGHT,
		FLAT_HEIGHT,
		LAYER,
		REMOVE_GRASS
	};

	TerrainEditor(struct StudioApp& app);
	~TerrainEditor();

	void onGUI(ComponentUID cmp, WorldEditor& editor);
	const char* getName() const override { return "terrain_editor"; }

private:
	struct Terrain* getTerrain() const;
	void fillGrass(u32 idx, EntityRef terrain, WorldEditor& editor);
	void clearGrass(u32 idx, EntityRef terrain, WorldEditor& editor);
	void exportGrass(u32 idx, EntityRef terrain, WorldEditor& editor);
	void importGrass(u32 idx, EntityRef terrain, WorldEditor& editor);
	void onMouseWheel(float value) override;
	bool onMouseDown(WorldView& view, int, int) override;
	void onMouseMove(WorldView& view, int x, int y, int /*rel_x*/, int /*rel_y*/) override;
	void onMouseUp(WorldView& view, int, int, os::MouseButton) override {};
	void compositeTextureRemoveLayer(const Path& path, i32 layer) const;
	void saveCompositeTexture(const Path& path, const char* channel) const;
	void layerGUI(ComponentUID cmp);
	void entityGUI();
	void drawCursor(RenderModule& module, EntityRef terrain, const DVec3& center) const;
	Material* getMaterial(ComponentUID cmp) const;
	void paint(const DVec3& hit, TerrainEditor::ActionType action_type, bool new_stroke, EntityRef terrain, WorldEditor& editor) const;

	void removeEntities(const DVec3& hit, WorldEditor& editor) const;
	void paintEntities(const DVec3& hit, WorldEditor& editor, EntityRef entity) const;
	void increaseBrushSize();
	void decreaseBrushSize();
	u16 getHeight(const DVec3& world_pos, RenderModule* module, EntityRef terrain) const;
	DVec3 getRelativePosition(const DVec3& world_pos, EntityRef terrain, World& world) const;
	void exportToOBJ(ComponentUID cmp) const;
	Renderer& getRenderer();

private:
	StudioApp& m_app;
	Mode m_mode = Mode::HEIGHT;
	bool m_is_flat_height = false;
	float m_terrain_brush_strength;
	float m_terrain_brush_size;
	u64 m_textures_mask = 0b1;
	u32 m_layers_mask = 0b1;
	Vec2 m_fixed_value{-1,1};
	u16 m_grass_mask;
	u16 m_flat_height;
	Vec3 m_color;
	int m_current_brush;
	Array<PrefabResource*> m_selected_prefabs;
	Action m_lower_terrain_action;
	Action m_smooth_terrain_action;
	Action m_remove_entity_action;
	Action m_remove_grass_action;
	Array<bool> m_brush_mask;
	Texture* m_brush_texture;
	Vec2 m_size_spread;
	Vec2 m_y_spread;
	bool m_is_align_with_normal;
	bool m_is_rotate_x;
	bool m_is_rotate_y;
	bool m_is_rotate_z;
	bool m_ignore_entities_not_in_folder = false;
	bool m_is_enabled;
	Vec2 m_rotate_x_spread;
	Vec2 m_rotate_y_spread;
	Vec2 m_rotate_z_spread;
	Array<gpu::TextureHandle> m_layer_views;
	Path m_albedo_composite_path;

	struct {
		char albedo[LUMIX_MAX_PATH] = "";
		char normal[LUMIX_MAX_PATH] = "";
	} m_add_layer_popup;
};


} // namespace Lumix
