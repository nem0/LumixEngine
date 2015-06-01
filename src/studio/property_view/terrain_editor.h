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
			HEIGHT,
			TEXTURE,
			ENTITY,
			NOT_SET
		};

		TerrainEditor(Lumix::WorldEditor& editor, MainWindow& main_window);
		~TerrainEditor();

		virtual void tick() override;
		virtual bool onEntityMouseDown(const Lumix::RayCastModelHit& hit, int, int) override;
		virtual void onMouseMove(int x, int y, int /*rel_x*/, int /*rel_y*/, int /*mouse_flags*/) override;
		virtual void onMouseUp(int, int, Lumix::MouseButton::Value) override;
		Lumix::Material* getMaterial();

		static void getProjections(const Lumix::Vec3& axis, const Lumix::Vec3 vertices[8], float& min, float& max);
		bool overlaps(float min1, float max1, float min2, float max2);
		bool testOBBCollision(const Lumix::Matrix& matrix_a, const Lumix::Model* model_a, const Lumix::Matrix& matrix_b, const Lumix::Model* model_b, float scale);
		bool isOBBCollision(Lumix::RenderScene* scene, const Lumix::Matrix& matrix, Lumix::Model* model, float scale);
		void paintEntities(Lumix::Component terrain, const Lumix::RayCastModelHit& hit);
		void addSplatWeight(Lumix::Component terrain, const Lumix::RayCastModelHit& hit);
		void addTexelSplatWeight(uint8_t& w1, uint8_t& w2, uint8_t& w3, uint8_t& w4, int value);
		void addTerrainLevel(Lumix::Component terrain, const Lumix::RayCastModelHit& hit, bool new_stroke);


	private:
		Lumix::WorldEditor& m_world_editor;
		MainWindow& m_main_window;
		Type m_type;
		Lumix::Component m_component;
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

		void createEditor(DynamicObjectModel::Node& parent_widget, const Lumix::Component& component);

	private:
		void addEntityTemplateNode(DynamicObjectModel::Node& node);
		void addTextureNode(DynamicObjectModel::Node& node);

	private:
		MainWindow& m_main_window;
		TerrainEditor* m_terrain_editor;
		QTreeWidgetItem* m_tools_item;
		QTreeWidgetItem* m_texture_tool_item;
};
