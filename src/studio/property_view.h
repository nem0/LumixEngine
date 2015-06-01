#pragma once


#include <QDockWidget>
#include "core/array.h"
#include "core/delegate_list.h"
#include "core/string.h"
#include "core/resource.h"
#include "property_view/dynamic_object_model.h"
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


class QAbstractItemModel;
class AssetBrowser;


class PropertyView : public QDockWidget
{
	Q_OBJECT
	public:

	public:
		explicit PropertyView(QWidget* parent = NULL);
		~PropertyView();
		void setWorldEditor(Lumix::WorldEditor& editor);
		void setAssetBrowser(AssetBrowser& asset_browser);
		void setModel(QAbstractItemModel* model, class QAbstractItemDelegate* delegate);
		void setSelectedResourceFilename(const char* filename);
		QAbstractItemModel* getModel() const;

	signals:
		void componentNodeCreated(DynamicObjectModel::Node&, const Lumix::Component&);

	public:
		Ui::PropertyView* m_ui;

	private:
		void onEntitySelected(const Lumix::Array<Lumix::Entity>& e);
		void openPersistentEditors(QAbstractItemModel* model, const QModelIndex& parent);

	private:
		Lumix::Entity m_selected_entity;
		Lumix::WorldEditor* m_world_editor;
		bool m_is_updating_values;
		AssetBrowser* m_asset_browser;
};
