#pragma once

#include "../property_view.h"
#include <QDragEnterEvent>
#include <qmimedata.h>
#include <qlineedit.h>


class FileEdit : public QLineEdit
{
public:
	FileEdit(QWidget* parent, PropertyView* property_view)
		: QLineEdit(parent)
		, m_property_view(property_view)
		, m_world_editor(NULL)
	{
		setAcceptDrops(true);
	}

	virtual void dragEnterEvent(QDragEnterEvent* event) override
	{
		if (event->mimeData()->hasUrls())
		{
			event->acceptProposedAction();
		}
	}

	virtual void dropEvent(QDropEvent* event)
	{
		ASSERT(m_world_editor);
		const QList<QUrl>& list = event->mimeData()->urls();
		if (!list.empty())
		{
			QString file = list[0].toLocalFile();
			if (file.toLower().startsWith(m_world_editor->getBasePath()))
			{
				file.remove(0, QString(m_world_editor->getBasePath()).length());
			}
			if (file.startsWith("/"))
			{
				file.remove(0, 1);
			}
			setText(file);
			emit editingFinished();
		}
	}

	void setServer(Lumix::WorldEditor* server)
	{
		m_world_editor = server;
	}

private:
	PropertyView* m_property_view;
	Lumix::WorldEditor* m_world_editor;
};