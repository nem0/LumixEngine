#pragma once


#include "engine/binary_array.h"
#include "engine/vec.h"
#include "editor/world_editor.h"
#include "editor/utils.h"


namespace Lumix
{
class Material;
class Model;
class RenderScene;
class Texture;
}


class TerrainEditor LUMIX_FINAL : public Lumix::WorldEditor::Plugin
{
public:
	enum ActionType
	{
		RAISE_HEIGHT,
		LOWER_HEIGHT,
		SMOOTH_HEIGHT,
		FLAT_HEIGHT,
		LAYER,
		ENTITY,
		REMOVE_ENTITY,
		COLOR,
		ADD_GRASS,
		REMOVE_GRASS,
		NOT_SET
	};

	TerrainEditor(Lumix::WorldEditor& editor, class StudioApp& app);
	~TerrainEditor();

	bool onEntityMouseDown(const Lumix::WorldEditor::RayHit& hit, int, int) override;
	void onMouseMove(int x, int y, int /*rel_x*/, int /*rel_y*/) override;
	void onMouseUp(int, int, Lumix::MouseButton::Value) override;
	void onGUI();
	void setComponent(Lumix::ComponentUID cmp) { m_component = cmp; }

private:
	void onUniverseDestroyed();
	void detectModifiers();
	void drawCursor(Lumix::RenderScene& scene, Lumix::ComponentHandle cmp, const Lumix::Vec3& center);
	Lumix::Material* getMaterial();
	void paint(const Lumix::Vec3& hit, TerrainEditor::ActionType action_type, bool new_stroke);

	static void getProjections(const Lumix::Vec3& axis,
		const Lumix::Vec3 vertices[8],
		float& min,
		float& max);
	void removeEntities(const Lumix::Vec3& hit);
	void paintEntities(const Lumix::Vec3& hit);
	void increaseBrushSize();
	void decreaseBrushSize();
	void nextTerrainTexture();
	void prevTerrainTexture();
	Lumix::uint16 getHeight(const Lumix::Vec3& world_pos);
	Lumix::Texture* getHeightmap();
	Lumix::Vec3 getRelativePosition(const Lumix::Vec3& world_pos) const;

private:
	Lumix::WorldEditor& m_world_editor;
	ActionType m_action_type;
	Lumix::ComponentUID m_component;
	float m_terrain_brush_strength;
	float m_terrain_brush_size;
	int m_texture_idx;
	int m_grass_idx;
	Lumix::uint16 m_flat_height;
	Lumix::Vec3 m_color;
	int m_current_brush;
	Lumix::Array<int> m_selected_entity_templates;
	Action* m_increase_brush_size;
	Action* m_decrease_brush_size;
	Action* m_increase_texture_idx;
	Action* m_decrease_texture_idx;
	Action* m_lower_terrain_action;
	Action* m_smooth_terrain_action;
	Action* m_remove_entity_action;
	Action* m_remove_grass_action;
	Lumix::BinaryArray m_brush_mask;
	Lumix::Texture* m_brush_texture;
	Lumix::Vec2 m_size_spread;
	Lumix::Vec2 m_y_spread;
	bool m_is_align_with_normal;
	bool m_is_rotate_x;
	bool m_is_rotate_y;
	bool m_is_rotate_z;
	bool m_is_enabled;
	Lumix::Vec2 m_rotate_x_spread;
	Lumix::Vec2 m_rotate_y_spread;
	Lumix::Vec2 m_rotate_z_spread;
};
