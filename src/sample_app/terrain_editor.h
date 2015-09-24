#pragma once


#include "editor/world_editor.h"


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
		LAYER,
		ENTITY,
		COLOR,
		NOT_SET
	};

	TerrainEditor(Lumix::WorldEditor& editor);
	~TerrainEditor();

	virtual void tick() override;
	virtual bool onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int) override;
	virtual void onMouseMove(int x,
		int y,
		int /*rel_x*/,
		int /*rel_y*/,
		int /*mouse_flags*/) override;
	virtual void onMouseUp(int, int, Lumix::MouseButton::Value) override;
	void onGui();
	void setComponent(Lumix::ComponentUID cmp) { m_component = cmp; }

private:
	void detectModifiers();
	void drawCursor(Lumix::RenderScene& scene,
		const Lumix::ComponentUID& cmp,
		const Lumix::Vec3& center);
	Lumix::Material* getMaterial();
	bool overlaps(float min1, float max1, float min2, float max2);
	bool testOBBCollision(const Lumix::Matrix& matrix_a,
		const Lumix::Model* model_a,
		const Lumix::Matrix& matrix_b,
		const Lumix::Model* model_b,
		float scale);
	bool isOBBCollision(Lumix::RenderScene* scene,
		const Lumix::Matrix& matrix,
		Lumix::Model* model,
		float scale);
	void paint(const Lumix::RayCastModelHit& hit, TerrainEditor::Type type, bool new_stroke);

	static void getProjections(const Lumix::Vec3& axis,
		const Lumix::Vec3 vertices[8],
		float& min,
		float& max);
	void paintEntities(const Lumix::RayCastModelHit& hit);

private:
	Lumix::WorldEditor& m_world_editor;
	Type m_type;
	Lumix::ComponentUID m_component;
	float m_terrain_brush_strength;
	float m_terrain_brush_size;
	int m_texture_idx;
	Lumix::Vec3 m_color;
	int m_current_brush;
	int m_selected_entity_template;
};
