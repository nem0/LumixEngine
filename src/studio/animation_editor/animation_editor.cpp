#include "animation_editor.h"
#include "animator.h"
#include "property_view/property_editor.h"
#include "property_view.h"
#include <qaction.h>
#include <qevent.h>
#include <qlayout.h>
#include <qpainter.h>
#include <qtoolbar.h>


static const QColor GRID_COLOR(60, 60, 60);
static const int GRID_CELL_SIZE = 32;
static QLinearGradient ANIMATION_NODE_GRADIENT(0, 0, 0, 100);

AnimationEditor::AnimationEditor(PropertyView& property_view)
	: m_property_view(property_view)
{
	m_animator = new Animator;

	setWindowTitle("Animation editor");
	setObjectName("animationEditor");
	QWidget* widget = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	m_animation_graph_view = new AnimationGraphView(*this);
	setWidget(widget);
	QToolBar* toolbar = new QToolBar(widget);
	toolbar->addAction("compile");
	toolbar->addAction("run");
	toolbar->connect(toolbar, &QToolBar::actionTriggered, [this](QAction* action){
		if (action->text() == "compile")
		{
			m_animator->compile("d:/projects/lumixengine_data");
		}
		else if (action->text() == "run")
		{
			m_animator->run();
		}

	});

	layout->addWidget(toolbar);
	layout->addWidget(m_animation_graph_view);
	

	ANIMATION_NODE_GRADIENT.setColorAt(0.0, QColor(0, 255, 0, 128));
	ANIMATION_NODE_GRADIENT.setColorAt(1.0, QColor(0, 64, 0, 128));
	ANIMATION_NODE_GRADIENT.setSpread(QGradient::Spread::ReflectSpread);
}


void AnimationEditor::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_animator->setWorldEditor(editor);
}


void AnimationEditor::setComponent(const Lumix::Component& component)
{

}


void AnimationEditor::executeCommand(QUndoCommand* command)
{
	m_undo_stack.push(command);
}


void AnimationEditor::update(float time_delta)
{
	m_animator->update(time_delta);
}


AnimationGraphView::AnimationGraphView(AnimationEditor& editor)
	: QWidget(&editor)
	, m_editor(editor)
{
	m_node = editor.getAnimator()->getRoot();
	m_mouse_mode = MouseMode::NONE;
	m_dragged_node = NULL;
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this, &AnimationGraphView::showContextMenu);
}


void AnimationGraphView::showContextMenu(const QPoint& pos)
{
	m_node->showContextMenu(m_editor, this, pos);
}


void AnimationGraphView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	drawGrid(painter);
	drawNodes(painter);
}


void AnimationGraphView::mousePressEvent(QMouseEvent* event)
{
	AnimatorNode* node = m_node->getContentNodeAt(event->x(), event->y());
	if (node)
	{
		m_mouse_mode = MouseMode::DRAGGING;
		m_dragged_node = node;
		m_last_mouse_position.setX(event->x());
		m_last_mouse_position.setY(event->y());
		node->getContent()->fillPropertyView(m_editor.getPropertyView());
	}
}


void AnimationGraphView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_mouse_mode == MouseMode::DRAGGING)
	{
		QPoint new_position = m_dragged_node->getPosition();
		new_position.setX(new_position.x() + event->x() - m_last_mouse_position.x());
		new_position.setY(new_position.y() + event->y() - m_last_mouse_position.y());
		m_dragged_node->setPosition(new_position);
		update();
	}
	m_last_mouse_position.setX(event->x());
	m_last_mouse_position.setY(event->y());
}


void AnimationGraphView::drawGrid(QPainter& painter)
{
	painter.setPen(GRID_COLOR);
	
	for (int i = 0; i < height() / GRID_CELL_SIZE; ++i)
	{
		painter.drawLine(0, i * GRID_CELL_SIZE, width(), i * GRID_CELL_SIZE);
	}

	for (int i = 0; i < width() / GRID_CELL_SIZE; ++i)
	{
		painter.drawLine(i * GRID_CELL_SIZE, 0, i * GRID_CELL_SIZE, height());
	}
}


void AnimationGraphView::drawNodes(QPainter& painter)
{
	if (m_node)
	{
		m_node->paintContent(painter);
	}
}
