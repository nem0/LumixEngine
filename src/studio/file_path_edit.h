#pragma once


#include <QDragEnterEvent>
#include <qlineedit.h>
#include <qmimedata.h>


class FilePathEdit : public QLineEdit
{
	public:
		explicit FilePathEdit(QWidget* parent)
			: QLineEdit(parent)
		{}

		void dragEnterEvent(QDragEnterEvent* e) override
		{
			if (e->mimeData()->hasUrls())
			{
				e->acceptProposedAction();
			}
		}

		void dropEvent(QDropEvent* e) override
		{
			auto urls = e->mimeData()->urls();
			setText(urls[0].toLocalFile());
		}
};