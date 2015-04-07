#include "file_edit.h"
#include "editor/world_editor.h"


FileEdit::FileEdit(QWidget* parent)
	: QLineEdit(parent)
	, m_world_editor(NULL)
{
	setAcceptDrops(true);
}


void FileEdit::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasUrls())
	{
		event->acceptProposedAction();
	}
}


void FileEdit::dropEvent(QDropEvent* event)
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


void FileEdit::setServer(Lumix::WorldEditor* server)
{
	m_world_editor = server;
}
