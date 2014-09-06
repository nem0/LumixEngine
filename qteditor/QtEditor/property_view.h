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
class QTreeWidgetItem;
class ScriptCompiler;


class PropertyViewObject : public QObject
{
	Q_OBJECT
	public:
		typedef PropertyViewObject* (*Creator)(PropertyViewObject*, Lumix::Resource*);

	public:
		PropertyViewObject(PropertyViewObject* parent, const char* name)
			: m_name(name)
			, m_parent(parent)
		{ }
		virtual ~PropertyViewObject();
		const char* getName() const { return m_name.c_str(); }
		PropertyViewObject** getMembers() { return m_members.empty() ? NULL : &m_members[0]; }
		int getMemberCount() const { return m_members.size(); }
		void addMember(PropertyViewObject* member) { m_members.push(member); }
		void removeMember(PropertyViewObject* member) { m_members.eraseItem(member); }
		PropertyViewObject* getParent() const { return m_parent; }

		virtual void createEditor(class PropertyView& view, QTreeWidgetItem* item) = 0;
		virtual bool isEditable() const = 0;

	private:
		Lumix::string m_name;
		Lumix::Array<PropertyViewObject*> m_members;
		PropertyViewObject* m_parent;
};


class PropertyView : public QDockWidget
{
	Q_OBJECT

public:
	explicit PropertyView(QWidget* parent = NULL);
	~PropertyView();
	void setWorldEditor(Lumix::WorldEditor& editor);
	Lumix::WorldEditor* getWorldEditor();
	void setScriptCompiler(ScriptCompiler* compiler);
	void setAssetBrowser(AssetBrowser& asset_browser);
	void addResourcePlugin(PropertyViewObject::Creator plugin);
	Lumix::Resource* getSelectedResource() const { return m_selected_resource; }
	void setSelectedResourceFilename(const char* filename);
	void setSelectedResource(Lumix::Resource* resource);
	void setObject(PropertyViewObject* object);
	PropertyViewObject* getObject();
	void addTerrainCustomProperties(QTreeWidgetItem& item, const Lumix::Component& terrain_component);
	void refresh();

private slots:
	void on_addComponentButton_clicked();
	void on_compileScriptClicked();
	void on_editScriptClicked();
	void on_TerrainHeightTypeClicked();
	void on_TerrainTextureTypeClicked();
	void on_terrainBrushTextureChanged(int value);
	void on_TerrainHeightSaveClicked();
	void on_TerrainSplatSaveClicked();
	void on_positionX_valueChanged(double arg1);
	void on_positionY_valueChanged(double arg1);
	void on_positionZ_valueChanged(double arg1);
	void on_propertyList_customContextMenuRequested(const QPoint &pos);
	void on_nameEdit_editingFinished();

private:
	void createObjectEditor(QTreeWidgetItem* item, PropertyViewObject* object);
	void clear();
	void onUniverseCreated();
	void onUniverseDestroyed();
	void onEntitySelected(const Lumix::Array<Lumix::Entity>& e);
	void onEntityPosition(const Lumix::Entity& e);
	void addScriptCustomProperties();
	void onScriptCompiled(const Lumix::Path& path, uint32_t status);
	void setScriptStatus(uint32_t status);
	void updateSelectedEntityPosition();
	void onSelectedResourceLoaded(Lumix::Resource::State old_state, Lumix::Resource::State new_state);

private:
	Ui::PropertyView* m_ui;
	ScriptCompiler* m_compiler;
	Lumix::Entity m_selected_entity;
	Lumix::WorldEditor* m_world_editor;
	bool m_is_updating_values;
	class TerrainEditor* m_terrain_editor;
	AssetBrowser* m_asset_browser;
	Lumix::Resource* m_selected_resource;
	Lumix::Array<PropertyViewObject::Creator> m_resource_plugins;
	PropertyViewObject* m_object;
};


