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


class PropertyView : public QDockWidget
{
	Q_OBJECT
	public:
		explicit PropertyView(QWidget* parent = NULL);
		~PropertyView();
		void setWorldEditor(Lumix::WorldEditor& editor);
		void setAssetBrowser(AssetBrowser& asset_browser);
		void setModel(class QAbstractItemModel* model, class QAbstractItemDelegate* delegate);
	
	private:
		void setSelectedResourceFilename(const char* filename);
		void onEntitySelected(const Lumix::Array<Lumix::Entity>& e);

	public:
		Ui::PropertyView* m_ui;

	private:
		Lumix::Entity m_selected_entity;
		Lumix::WorldEditor* m_world_editor;
		bool m_is_updating_values;
		AssetBrowser* m_asset_browser;
};
