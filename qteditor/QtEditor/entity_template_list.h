#pragma once

#include <QDockWidget>
#include "core/vec3.h"

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
		void instantiateTemplate();
		void instantiateTemplateAt(const Lumix::Vec3& position);

	private:
		void onSystemUpdated();

	private slots:
		void on_templateList_doubleClicked(const QModelIndex& index);

private:
		Ui::EntityTemplateList* m_ui;
		Lumix::WorldEditor* m_editor;
};

