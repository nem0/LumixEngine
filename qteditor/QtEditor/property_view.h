#pragma once


#include <QDockWidget>
#include "core/array.h"
#include "core/string.h"
#include "universe/entity.h"

namespace Lumix
{
	struct Component;
	class WorldEditor;
	struct Entity;
	class Event;
	class Path;
	struct PropertyListEvent;
}

namespace Ui
{
	class PropertyView;
}

class QTreeWidgetItem;
class ScriptCompiler;

class PropertyView : public QDockWidget
{
	Q_OBJECT

public:
	class Property
	{
		public:
			enum Type
			{
				FILE,
				STRING,
				DECIMAL,
				VEC3,
				BOOL
			};
				
			Type m_type;
			uint32_t m_component;
			Lumix::string m_name;
			Lumix::string m_file_type;
			Lumix::string m_component_name;
			uint32_t m_name_hash;
			QTreeWidgetItem* m_tree_item;
	};

public:
	explicit PropertyView(QWidget* parent = NULL);
	~PropertyView();
	void setWorldEditor(Lumix::WorldEditor& server);
	void setScriptCompiler(ScriptCompiler* compiler);

private slots:
	void on_addComponentButton_clicked();
	void on_checkboxStateChanged();
	void on_doubleSpinBoxValueChanged();
	void on_vec3ValueChanged();
	void on_lineEditEditingFinished();
	void on_browseFilesClicked();
	void on_compileScriptClicked();
	void on_editScriptClicked();
	void on_animablePlayPause();
	void on_animableTimeSet(int value);
	void on_terrainBrushSizeChanged(int value);
	void on_terrainBrushStrengthChanged(int value);
	void on_TerrainHeightTypeClicked();
	void on_TerrainTextureTypeClicked();
	void on_terrainBrushTextureChanged(int value);

private:
	void clear();
	void onUniverseCreated();
	void onUniverseDestroyed();
	void onEntitySelected(Lumix::Entity& e);
	void onEntityPosition(Lumix::Entity& e);
	void addProperty(const char* component, const char* name, const char* label, Property::Type type, const char* file_type);
	void onPropertyValue(Property* property, const void* data, int32_t data_size);
	void addScriptCustomProperties();
	void addAnimableCustomProperties(const Lumix::Component& cmp);
	void addTerrainCustomProperties(const Lumix::Component& terrain_component);
	void onScriptCompiled(const Lumix::Path& path, uint32_t status);
	void setScriptStatus(uint32_t status);
	void updateValues();

private:
	Ui::PropertyView* m_ui;
	Lumix::Array<Property*> m_properties;
	ScriptCompiler* m_compiler;
	Lumix::Entity m_selected_entity;
	Lumix::WorldEditor* m_world_editor;
	class TerrainEditor* m_terrain_editor;
};


