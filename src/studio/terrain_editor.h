#pragma once


#include "core/binary_array.h"
#include "editor/world_editor.h"
#include "utils.h"


namespace Lumix
{
class Material;
class Model;
class RenderScene;
class Texture;
}


class TerrainEditor : public Lumix::WorldEditor::Plugin
{
public:
	enum Type
	{
		RAISE_HEIGHT,
		LOWER_HEIGHT,
		SMOOTH_HEIGHT,
		FLAT_HEIGHT,
		LAYER,
		ENTITY,
		REMOVE_ENTITY,
		COLOR,
		NOT_SET
	};

	TerrainEditor(Lumix::WorldEditor& editor, Lumix::Array<Action*>& actions);
	~TerrainEditor();

	virtual void tick() override;
	virtual bool onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int) override;
	virtual void onMouseMove(int x, int y, int /*rel_x*/, int /*rel_y*/) override;
	virtual void onMouseUp(int, int, Lumix::MouseButton::Value) override;
	void onGUI();
	void setComponent(Lumix::ComponentUID cmp) { m_component = cmp; }

private:
	void detectModifiers();
	void drawCursor(Lumix::RenderScene& scene,
		const Lumix::ComponentUID& cmp,
		const Lumix::Vec3& center);
	Lumix::Material* getMaterial();
	void paint(const Lumix::RayCastModelHit& hit, TerrainEditor::Type type, bool new_stroke);

	static void getProjections(const Lumix::Vec3& axis,
		const Lumix::Vec3 vertices[8],
		float& min,
		float& max);
	void removeEntities(const Lumix::RayCastModelHit& hit);
	void paintEntities(const Lumix::RayCastModelHit& hit);
	void increaseBrushSize();
	void decreaseBrushSize();
	void nextTerrainTexture();
	void prevTerrainTexture();
	Lumix::uint16 getHeight(const Lumix::Vec3& world_pos);
	Lumix::Texture* getHeightmap();
	Lumix::Vec3 getRelativePosition(const Lumix::Vec3& world_pos) const;

private:
	Lumix::WorldEditor& m_world_editor;
	Type m_type;
	Lumix::ComponentUID m_component;
	float m_terrain_brush_strength;
	float m_terrain_brush_size;
	int m_texture_idx;
	Lumix::uint16 m_flat_height;
	Lumix::Vec3 m_color;
	int m_current_brush;
	int m_selected_entity_template;
	Action* m_increase_brush_size;
	Action* m_decrease_brush_size;
	Action* m_increase_texture_idx;
	Action* m_decrease_texture_idx;
	Action* m_lower_terrain_action;
	Action* m_smooth_terrain_action;
	Action* m_remove_entity_action;
	Lumix::BinaryArray m_brush_mask;
	Lumix::Texture* m_brush_texture;
	bool m_is_align_with_normal;
	bool m_is_rotate_x;
	bool m_is_rotate_z;
	bool m_is_enabled;
};
