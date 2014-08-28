#pragma once

#include <QDockWidget>

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

class EntityList : public QDockWidget
{
	Q_OBJECT

	public:
		explicit EntityList(QWidget* parent);
		~EntityList();

		void setWorldEditor(Lumix::WorldEditor& editor);

private slots:
        void on_entityList_clicked(const QModelIndex &index);

private:
		void onUniverseCreated();
		void onUniverseDestroyed();
		void onUniverseLoaded();

	private:
		Ui::EntityList* m_ui;
		Lumix::WorldEditor* m_editor;
		Lumix::Universe* m_universe;
		EntityListModel* m_model;
};

