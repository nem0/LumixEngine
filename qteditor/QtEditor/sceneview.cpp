#include "sceneview.h"
#include "editor/world_editor.h"
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
			setMouseTracking(true);
		}

		virtual void mousePressEvent(QMouseEvent* event) override
		{
			m_world_editor->onMouseDown(event->x(), event->y(), event->button() == Qt::RightButton ? Lumix::MouseButton::RIGHT : Lumix::MouseButton::LEFT);
			m_last_x = event->x();
			m_last_y = event->y();
			setFocus();
		}

		virtual void mouseMoveEvent(QMouseEvent* event) override
		{
			int flags = 0;
			flags |= Qt::ControlModifier & QApplication::keyboardModifiers() ? (int)Lumix::WorldEditor::MouseFlags::CONTROL : 0;
			flags |= Qt::AltModifier & QApplication::keyboardModifiers() ? (int)Lumix::WorldEditor::MouseFlags::ALT : 0;
			m_world_editor->onMouseMove(event->x(), event->y(), event->x() - m_last_x, event->y() - m_last_y, flags);
			m_last_x = event->x();
			m_last_y = event->y();
		}

		virtual void mouseReleaseEvent(QMouseEvent* event) override
		{
			m_world_editor->onMouseUp(event->x(), event->y(), event->button() == Qt::RightButton ? Lumix::MouseButton::RIGHT : Lumix::MouseButton::LEFT);
		}

		Lumix::WorldEditor* m_world_editor;
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


void SceneView::setWorldEditor(Lumix::WorldEditor* world_editor)
{
	static_cast<ViewWidget*>(m_view)->m_world_editor = world_editor;
	m_world_editor = world_editor;
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
			m_world_editor->addEntityAt(event->pos().x(), event->pos().y());
			m_world_editor->addComponent(crc32("renderable"));
			char rel_path[LUMIX_MAX_PATH];
			m_world_editor->getRelativePath(rel_path, LUMIX_MAX_PATH, file.toLatin1().data());
			m_world_editor->setProperty("renderable", "source", rel_path, strlen(rel_path));
			m_world_editor->selectEntity(m_world_editor->getSelectedEntity());
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