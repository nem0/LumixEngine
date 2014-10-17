#pragma once

#include <QDockWidget>
#include <qsortfilterproxymodel.h>
#include "core/array.h"
#include "universe/entity.h"


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
		void enableUpdate(bool enable);

	private slots:
		void on_entityList_clicked(const QModelIndex& index);
		void on_comboBox_activated(const QString& arg1);
		void on_nameFilterEdit_textChanged(const QString &arg1);

	private:
		void onUniverseCreated();
		void onUniverseDestroyed();
		void onUniverseLoaded();
		void onEntitySelected(const Lumix::Array<Lumix::Entity>& entity);

	private:
		Ui::EntityList* m_ui;
		Lumix::WorldEditor* m_editor;
		Lumix::Universe* m_universe;
		EntityListModel* m_model;
		EntityListFilter* m_filter;
		bool m_is_update_enabled;
};

