#pragma once

#include <QDockWidget>

namespace Ui
{
	class EntityTemplateList;
}

namespace Lumix
{
	class WorldEditor;
}

class EntityTemplateList : public QDockWidget
{
		Q_OBJECT

	public:
		explicit EntityTemplateList();
		~EntityTemplateList();

		void setWorldEditor(Lumix::WorldEditor& editor);

	private:
		void onSystemUpdated();

	private slots:
		void on_templateList_doubleClicked(const QModelIndex& index);

private:
		Ui::EntityTemplateList* m_ui;
		Lumix::WorldEditor* m_editor;
};

