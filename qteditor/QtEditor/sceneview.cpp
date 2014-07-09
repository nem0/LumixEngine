#include "sceneview.h"
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include <qapplication.h>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include "core/crc32.h"
#include "graphics/pipeline.h"

SceneView::SceneView(QWidget* parent) :
	QDockWidget(parent)
{
	m_pipeline = NULL;
	setWidget(new QWidget());
	setWindowTitle("Scene");
	setObjectName("sceneView");
	setAcceptDrops(true);
}


void SceneView::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
	{
		event->acceptProposedAction();
	}
}


void SceneView::dropEvent(QDropEvent *event)
{
	const QList<QUrl>& list = event->mimeData()->urls();
	if(!list.empty())
	{
		QString file = list[0].toLocalFile();
		if(file.endsWith(".msh"))
		{
			m_client->addEntity();
			m_client->addComponent(crc32("renderable"));
			QString base_path = m_client->getBasePath();
			if(file.startsWith(base_path))
			{
				file.remove(0, base_path.length());
			}
			m_client->setComponentProperty("renderable", "source", file.toLatin1().data(), file.length());
		}
	}
}


void SceneView::mousePressEvent(QMouseEvent* event)
{
	m_client->mouseDown(event->x(), event->y(), event->button() == Qt::LeftButton ? 0 : 2);
	m_last_x = event->x();
	m_last_y = event->y();
	setFocus();
}

void SceneView::mouseMoveEvent(QMouseEvent* event)
{
	int flags = 0;
	flags |= Qt::ControlModifier & QApplication::keyboardModifiers() ? (int)Lumix::EditorServer::MouseFlags::CONTROL : 0;
	flags |= Qt::AltModifier & QApplication::keyboardModifiers() ? (int)Lumix::EditorServer::MouseFlags::ALT : 0;
	m_client->mouseMove(event->x(), event->y(), event->x() - m_last_x, event->y() - m_last_y, flags);
	m_last_x = event->x();
	m_last_y = event->y();
}

void SceneView::mouseReleaseEvent(QMouseEvent* event)
{
	m_client->mouseUp(event->x(), event->y(), event->button() == Qt::LeftButton ? 0 : 2);
}


void SceneView::resizeEvent(QResizeEvent* event)
{
	int w = event->size().width();
	int h = event->size().height();
	if (m_pipeline)
	{
		m_pipeline->resize(w, h);
	}
}