#pragma once

#include "../property_view.h"
#include <QDragEnterEvent>
#include <qmimedata.h>
#include <qlineedit.h>


class FileEdit : public QLineEdit
{
	public:
		FileEdit(QWidget* parent);

		virtual void dragEnterEvent(QDragEnterEvent* event) override;
		virtual void dropEvent(QDropEvent* event) override;
		void setServer(Lumix::WorldEditor* server);

	private:
		Lumix::WorldEditor* m_world_editor;
};