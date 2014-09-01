#pragma once

#include <QDockWidget>
#include <qsortfilterproxymodel.h>


namespace Ui 
{
	class EntityList;
}

namespace Lumix
{
	struct Entity;
	class Universe;
	class WorldEditor;
}

class EntityListModel;
class EntityListFilter;


class EntityList : public QDockWidget
{
	Q_OBJECT

	public:
		explicit EntityList(QWidget* parent);
		~EntityList();
		void setWorldEditor(Lumix::WorldEditor& editor);

	private slots:
		void on_entityList_clicked(const QModelIndex& index);
		void on_comboBox_activated(const QString& arg1);

	private:
		void onUniverseCreated();
		void onUniverseDestroyed();
		void onUniverseLoaded();

	private:
		Ui::EntityList* m_ui;
		Lumix::WorldEditor* m_editor;
		Lumix::Universe* m_universe;
		EntityListModel* m_model;
		EntityListFilter* m_filter;
};

