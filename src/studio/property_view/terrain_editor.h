#pragma once


#include "dynamic_object_model.h"
#include "editor/world_editor.h"
#include <qobject.h>


class EntityTemplateList;
class EntityList;
class MainWindow;
class QTreeWidgetItem;


namespace Lumix
{
class Material;
class Model;
class RenderScene;
class Texture;
}

class TerrainEditor : public Lumix::WorldEditor::Plugin
{
	friend class TerrainComponentPlugin;

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

	TerrainEditor(Lumix::WorldEditor& editor,
				  MainWindow& main_window,
				  TerrainComponentPlugin& plugin);
	~TerrainEditor();

	virtual void tick() override;
	virtual bool
	onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int) override;
	virtual void onMouseMove(int x,
							 int y,
							 int /*rel_x*/,
							 int /*rel_y*/,
							 int /*mouse_flags*/) override;
	virtual void onMouseUp(int, int, Lumix::MouseButton::Value) override;

private:
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
	void paintEntities(const Lumix::RayCastModelHit& hit);
	void paint(const Lumix::RayCastModelHit& hit,
			   TerrainEditor::Type type,
			   bool new_stroke);

	static void getProjections(const Lumix::Vec3& axis,
							   const Lumix::Vec3 vertices[8],
							   float& min,
							   float& max);

private:
	TerrainComponentPlugin& m_plugin;
	Lumix::WorldEditor& m_world_editor;
	MainWindow& m_main_window;
	Type m_type;
	Lumix::ComponentUID m_component;
	QTreeWidgetItem* m_texture_tree_item;
	float m_terrain_brush_strength;
	int m_terrain_brush_size;
	int m_texture_idx;
	QString m_selected_entity_template;
};


class TerrainComponentPlugin : public QObject
{
	Q_OBJECT
public:
	TerrainComponentPlugin(MainWindow& main_window);
	~TerrainComponentPlugin();

	void createEditor(DynamicObjectModel::Node& parent_widget,
					  const Lumix::ComponentUID& component);
	QColor getSelectedColor() const { return m_selected_color; }

private:
	void addEntityTemplateNode(DynamicObjectModel::Node& node);
	void addTextureNode(DynamicObjectModel::Node& node);
	void addColorNode(DynamicObjectModel::Node& node);

private:
	MainWindow& m_main_window;
	TerrainEditor* m_terrain_editor;
	QTreeWidgetItem* m_tools_item;
	QTreeWidgetItem* m_texture_tool_item;
	QColor m_selected_color;
};
