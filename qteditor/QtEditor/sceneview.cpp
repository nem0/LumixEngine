#include "sceneview.h"
#include "editor/editor_client.h"
#include "editor/editor_server.h"
#include <qapplication.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QVBoxLayout>
#include "core/crc32.h"
#include "graphics/pipeline.h"

class ViewWidget : public QWidget
{
	public:
		ViewWidget(QWidget* parent)
			: QWidget(parent)
		{
		}

		virtual void mousePressEvent(QMouseEvent* event) override
		{
			m_client->mouseDown(event->x(), event->y(), event->button() == Qt::LeftButton ? 0 : 2);
			m_last_x = event->x();
			m_last_y = event->y();
			setFocus();
		}

		virtual void mouseMoveEvent(QMouseEvent* event) override
		{
			int flags = 0;
			flags |= Qt::ControlModifier & QApplication::keyboardModifiers() ? (int)Lumix::EditorServer::MouseFlags::CONTROL : 0;
			flags |= Qt::AltModifier & QApplication::keyboardModifiers() ? (int)Lumix::EditorServer::MouseFlags::ALT : 0;
			m_client->mouseMove(event->x(), event->y(), event->x() - m_last_x, event->y() - m_last_y, flags);
			m_last_x = event->x();
			m_last_y = event->y();
		}

		virtual void mouseReleaseEvent(QMouseEvent* event) override
		{
			m_client->mouseUp(event->x(), event->y(), event->button() == Qt::LeftButton ? 0 : 2);
		}

		Lumix::EditorClient* m_client;
		int m_last_x;
		int m_last_y;
};

SceneView::SceneView(QWidget* parent) :
	QDockWidget(parent)
{
	m_pipeline = NULL;
	QWidget* root = new QWidget();
	QVBoxLayout* vertical_layout = new QVBoxLayout(root);
	QHBoxLayout* horizontal_layout = new QHBoxLayout(root);
	m_view = new ViewWidget(root);
	m_speed_input = new QDoubleSpinBox(root);
	m_speed_input->setSingleStep(0.1f);
	m_speed_input->setValue(0.1f);
	horizontal_layout->addWidget(m_speed_input);
	horizontal_layout->addStretch();
	vertical_layout->addWidget(m_view);
	vertical_layout->addLayout(horizontal_layout);
	vertical_layout->setContentsMargins(0, 0, 0, 0);
	setWidget(root);
	setWindowTitle("Scene");
	setObjectName("sceneView");
	setAcceptDrops(true);
}


void SceneView::setEditorClient(Lumix::EditorClient& client)
{
	m_client = &client;
	static_cast<ViewWidget*>(m_view)->m_client = &client;
}


float SceneView::getNavivationSpeed() const
{
	return m_speed_input->value();
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



void SceneView::resizeEvent(QResizeEvent*)
{
	if (m_pipeline)
	{
		m_pipeline->resize(m_view->width(), m_view->height());
	}
}