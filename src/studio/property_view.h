#pragma once


#include <QDockWidget>
#include "core/array.h"
#include "core/string.h"
#include "core/resource.h"
#include "universe/entity.h"

namespace Lumix
{
	struct Component;
	struct Entity;
	class Event;
	class Path;
	class Resource;
	class WorldEditor;
}

namespace Ui
{
	class PropertyView;
}

class AssetBrowser;
class MainWindow;
class QTreeWidgetItem;
class ScriptCompiler;


class PropertyView : public QDockWidget
{
		Q_OBJECT
	public:
		class IEntityComponentPlugin
		{
			public:
				virtual ~IEntityComponentPlugin() {}

				virtual uint32_t getType() = 0;
				virtual void createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component) = 0;
				virtual void onPropertyViewCleared() = 0;
		};

	public:
		explicit PropertyView(QWidget* parent = NULL);
		~PropertyView();
		void setWorldEditor(Lumix::WorldEditor& editor);
		Lumix::WorldEditor* getWorldEditor();
		void setAssetBrowser(AssetBrowser& asset_browser);
		Lumix::Resource* getSelectedResource() const { return m_selected_resource; }
		void setSelectedResourceFilename(const char* filename);
		Lumix::Resource* getResource(const char* name);
		void setSelectedResource(Lumix::Resource* resource);
		void refresh();
		void createCustomProperties(QTreeWidgetItem* item, const Lumix::Component& component);
		void addEntityComponentPlugin(IEntityComponentPlugin* plugin);
		MainWindow* getMainWindow() const { return (MainWindow*)parent(); }

	private slots:
		void on_positionX_valueChanged(double arg1);
		void on_positionY_valueChanged(double arg1);
		void on_positionZ_valueChanged(double arg1);
		void on_propertyList_customContextMenuRequested(const QPoint &pos);
		void on_nameEdit_editingFinished();
	
	private:
		void clear();
		void onUniverseCreated();
		void onUniverseDestroyed();
		void onEntitySelected(const Lumix::Array<Lumix::Entity>& e);
		void onEntityPosition(const Lumix::Entity& e);
		void updateSelectedEntityPosition();
		void onSelectedResourceLoaded(Lumix::Resource::State old_state, Lumix::Resource::State new_state);

	public:
		Ui::PropertyView* m_ui;

	private:
		Lumix::Entity m_selected_entity;
		Lumix::WorldEditor* m_world_editor;
		bool m_is_updating_values;
		AssetBrowser* m_asset_browser;
		Lumix::Resource* m_selected_resource;
		QList<IEntityComponentPlugin*> m_entity_component_plugins;
};



class GlobalLightComponentPlugin : public QObject, public PropertyView::IEntityComponentPlugin
{
		Q_OBJECT
	public:
		virtual uint32_t getType() override;
		virtual void createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component) override;
		virtual void onPropertyViewCleared() override {}
};

class TerrainComponentPlugin : public QObject, public PropertyView::IEntityComponentPlugin
{
		Q_OBJECT
	public:
		TerrainComponentPlugin(Lumix::WorldEditor& editor, class EntityTemplateList* template_list, class EntityList* entity_list);
		virtual ~TerrainComponentPlugin();

		virtual uint32_t getType() override;
		virtual void createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component) override;
		virtual void onPropertyViewCleared() override;

	private:
		void resetTools();

	private slots:
		void on_TerrainTextureTypeClicked();

	private:
		class TerrainEditor* m_terrain_editor;
		QTreeWidgetItem* m_tools_item;
		QTreeWidgetItem* m_texture_tool_item;
};



class ScriptComponentPlugin : public QObject, public PropertyView::IEntityComponentPlugin
{
		Q_OBJECT
	public:
		ScriptComponentPlugin(Lumix::WorldEditor& editor, ScriptCompiler& compiler);

		virtual uint32_t getType() override;
		virtual void createEditor(QTreeWidgetItem* component_item, const Lumix::Component& component) override;
		virtual void onPropertyViewCleared() override;

	private:
		void setScriptStatus(uint32_t status);

	private:
		Lumix::WorldEditor& m_world_editor;
		ScriptCompiler& m_compiler;
		QTreeWidgetItem* m_status_item;
};
