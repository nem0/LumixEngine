#include "animation_editor.h"
#include "animation_inputs.h"
#include "animator.h"
#include "mainwindow.h"
#include "property_view/property_editor.h"
#include "property_view.h"
#include "scripts/scriptcompiler.h"
#include "skeleton_view.h"
#include <qaction.h>
#include <qevent.h>
#include <qlayout.h>
#include <qmenu.h>
#include <qmenubar.h>
#include <qmessagebox.h>
#include <qpainter.h>
#include <qtoolbar.h>


static const QColor GRID_COLOR(60, 60, 60);
static const QColor EDGE_COLOR(255, 255, 255);
static const int GRID_CELL_SIZE = 32;
static QLinearGradient ANIMATION_NODE_GRADIENT(0, 0, 0, 100);

AnimationEditor::AnimationEditor(MainWindow& main_window)
	: m_property_view(*main_window.getPropertyView())
	, m_compiler(*main_window.getScriptCompiler())
	, m_main_window(main_window)
{
	m_animator = new Animator(m_compiler);
	emit animatorCreated();

	setWindowTitle("Animation editor");
	setObjectName("animationEditor");
	QWidget* widget = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	m_animation_graph_view = new AnimationGraphView(*this);
	setWidget(widget);
	QToolBar* toolbar = new QToolBar(widget);
	m_compile_action = toolbar->addAction("Compile");
	m_run_action = toolbar->addAction("Run");
	m_save_action = toolbar->addAction("Save");
	m_save_as_action = toolbar->addAction("Save As");
	m_load_action = toolbar->addAction("Load");
	connect(m_compile_action, &QAction::triggered, this, &AnimationEditor::onCompileAction);
	connect(m_run_action, &QAction::triggered, this, &AnimationEditor::onRunAction);
	connect(m_save_action, &QAction::triggered, this, &AnimationEditor::onSaveAction);
	connect(m_save_as_action, &QAction::triggered, this, &AnimationEditor::onSaveAsAction);
	connect(m_load_action, &QAction::triggered, this, &AnimationEditor::onLoadAction);
	layout->addWidget(toolbar);
	layout->addWidget(m_animation_graph_view);

	m_inputs = new AnimationInputs(*this);
	m_skeleton_view = new SkeletonView();
	
	addMenu(main_window);

	ANIMATION_NODE_GRADIENT.setColorAt(0.0, QColor(0, 255, 0, 128));
	ANIMATION_NODE_GRADIENT.setColorAt(1.0, QColor(0, 64, 0, 128));
	ANIMATION_NODE_GRADIENT.setSpread(QGradient::Spread::ReflectSpread);
}


void AnimationEditor::addMenu(MainWindow& main_window)
{
	QMenuBar* menu_bar = main_window.getMenuBar();
	QMenu* menu = menu_bar->addMenu("Animation Editor");
	menu->addAction(m_compile_action);
	menu->addAction(m_run_action);
	menu->addAction(m_load_action);
	menu->addAction(m_save_action);
	menu->addAction(m_save_as_action);
	m_view_menu = menu->addMenu("View");
	addEditorDock(Qt::DockWidgetArea::BottomDockWidgetArea, this);
	addEditorDock(Qt::DockWidgetArea::BottomDockWidgetArea, m_inputs);
	addEditorDock(Qt::DockWidgetArea::BottomDockWidgetArea, m_skeleton_view);
	m_view_menu->connect(m_view_menu, &QMenu::aboutToShow, [this]() {
		for (auto info : m_dock_infos)
		{
			info.m_action->setChecked(info.m_widget->isVisible());
		}
	});
}


void AnimationEditor::addEditorDock(Qt::DockWidgetArea area, QDockWidget* widget)
{
	DockInfo info;
	info.m_widget = widget;
	QAction* action = widget->toggleViewAction();
	action->setCheckable(true);
	m_view_menu->addAction(action);
	info.m_action = action;
	action->connect(action, &QAction::triggered, this, [widget](){
		widget->show();
	});
	m_dock_infos.push_back(info);
	m_main_window.addDockWidget(area, widget);
}


void AnimationEditor::onCompileAction()
{
	if (!m_animator->isValidPath())
	{
		onSaveAsAction();
		if (!m_animator->isValidPath())
		{
			return;
		}
	}
	m_animator->compile();
}


void AnimationEditor::onRunAction()
{
	m_animator->run();
}


void AnimationEditor::onSaveAsAction()
{
	QString path = QFileDialog::getSaveFileName(NULL, QString(), QString(), "All files (*.grf)");
	if (!path.isEmpty())
	{
		m_animator->setPath(path);
		onSaveAction();
	}
}


void AnimationEditor::onSaveAction()
{
	if (!m_animator->isValidPath())
	{
		QString path = QFileDialog::getSaveFileName(NULL, QString(), QString(), "All files (*.grf)");
		if (!path.isEmpty())
		{
			m_animator->setPath(path);
		}
		else
		{
			return;
		}
	}

	Lumix::OutputBlob blob(m_editor->getEngine().getAllocator());
	m_animator->serialize(blob);
	QFile file(m_animator->getPath());
	file.open(QIODevice::WriteOnly);
	file.write((const char*)blob.getData(), blob.getSize());
	file.close();
}


void AnimationEditor::onLoadAction()
{
	QString path = QFileDialog::getOpenFileName(NULL, QString(), QString(), "All files (*.grf)");
	if (!path.isEmpty())
	{
		delete m_animator;
		m_animator = new Animator(m_compiler);
		m_animator->setPath(path);
		m_animator->setWorldEditor(*m_editor);
		QFile file(path);
		file.open(QIODevice::ReadOnly);
		QByteArray data = file.readAll();

		Lumix::InputBlob blob(data.data(), data.size());
		m_animator->deserialize(*this, blob);
		m_animation_graph_view->setNode(m_animator->getRoot());
		emit animatorCreated();
	}
}


AnimatorNodeContent* AnimationEditor::createContent(AnimatorNode& node, uint32_t content_type)
{
	TODO("todo");
	if (content_type == crc32("animation"))
	{
		return new AnimationNodeContent(&node);
	}
	else if (content_type == crc32("state_machine"))
	{
		return new StateMachineNodeContent(&node);
	}
	return NULL;
}


void AnimationEditor::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_editor = &editor;
	m_animator->setWorldEditor(editor);
	m_skeleton_view->setWorldEditor(editor);
}


void AnimationEditor::setComponent(const Lumix::Component&)
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
	m_mouse_node = NULL;
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this, &AnimationGraphView::showContextMenu);
}


void AnimationGraphView::showContextMenu(const QPoint& pos)
{
	if (m_mouse_mode != MouseMode::EDGE)
	{
		m_node->showContextMenu(m_editor, this, pos);
	}
	m_mouse_mode = MouseMode::NONE;
	update();
}


void AnimationGraphView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	drawGrid(painter);
	drawNewEdge(painter);
	drawNodes(painter);
}


void AnimationGraphView::selectEdge(AnimatorEdge* edge)
{
	edge->fillPropertyView(m_editor.getPropertyView());
}


void AnimationGraphView::selectNode(AnimatorNode* node)
{
	node->getContent()->fillPropertyView(m_editor.getPropertyView());
}


void AnimationGraphView::mouseReleaseEvent(QMouseEvent* event)
{
	AnimatorNode* node = m_node->getContentNodeAt(event->x(), event->y());
	if (m_mouse_mode == MouseMode::EDGE && node && m_node != node && node != m_mouse_node)
	{
		if (m_node->getContent()->getType() == crc32("state_machine"))
		{
			static_cast<StateMachineNodeContent*>(m_node->getContent())->createEdge(*m_editor.getAnimator(), m_mouse_node, node);
		}
	}
	else
	{
		m_mouse_mode = MouseMode::NONE;
	}

	if (event->button() != Qt::RightButton)
	{
		m_mouse_mode = MouseMode::NONE;
	}
}


void AnimationGraphView::mousePressEvent(QMouseEvent* event)
{
	AnimatorNode* node = m_node->getContentNodeAt(event->x(), event->y());
	if (node && m_node != node)
	{
		m_mouse_mode = event->button() == Qt::RightButton ? MouseMode::EDGE : MouseMode::DRAGGING;
		m_mouse_node = node;
		m_last_mouse_position.setX(event->x());
		m_last_mouse_position.setY(event->y());
		selectNode(node);
	}
	else if (m_node == node)
	{
		AnimatorEdge* edge = m_node->getContent()->getEdgeAt(event->x(), event->y());
		if (edge)
		{
			selectEdge(edge);
		}

	}
}


void AnimationGraphView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_mouse_mode == MouseMode::DRAGGING)
	{
		QPoint new_position = m_mouse_node->getPosition();
		new_position.setX(new_position.x() + event->x() - m_last_mouse_position.x());
		new_position.setY(new_position.y() + event->y() - m_last_mouse_position.y());
		m_mouse_node->setPosition(new_position);
		update();
	}
	else if (m_mouse_mode == MouseMode::EDGE)
	{
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


void AnimationGraphView::drawNewEdge(QPainter& painter)
{
	if (m_mouse_mode == MouseMode::EDGE)
	{
		painter.setPen(EDGE_COLOR);
		painter.drawLine(m_last_mouse_position, m_mouse_node->getCenter());
	}
}


void AnimationGraphView::drawNodes(QPainter& painter)
{
	if (m_node)
	{
		m_node->paintContent(painter);
	}
}
