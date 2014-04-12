#include "sceneview.h"
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include <QMouseEvent>
#include "graphics/pipeline.h"

SceneView::SceneView(QWidget* parent) :
	QDockWidget(parent)
{
	m_pipeline = NULL;
	setWidget(new QWidget());
	setWindowTitle("Scene");
	setObjectName("sceneView");
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
	m_client->mouseMove(event->x(), event->y(), event->x() - m_last_x, event->y() - m_last_y);
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