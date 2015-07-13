#include "gameview.h"
#include "ui_gameview.h"

#include "core/input_system.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/pipeline.h"
#include "mainwindow.h"
#include "wgl_render_device.h"

#include <QMouseEvent>


GameView::GameView(MainWindow& parent)
	: QDockWidget(&parent)
	, m_ui(new Ui::GameView)
	, m_main_window(parent)
{
	m_ui->setupUi(this);
	m_editor = nullptr;
	m_render_device = nullptr;
}


GameView::~GameView()
{
	delete m_render_device;
	delete m_ui;
}


void GameView::render()
{
	if (!getContentWidget()->visibleRegion().isEmpty() && m_render_device)
	{
		m_render_device->getPipeline().render();
	}
}


void GameView::setWorldEditor(Lumix::WorldEditor& editor)
{
	ASSERT(m_editor == nullptr);
	m_editor = &editor;
	m_render_device = new WGLRenderDevice(m_editor->getEngine(), "pipelines/game_view.json");
	m_render_device->setWidget(*this);
}


QWidget* GameView::getContentWidget() const
{
	return m_ui->viewFrame;
}


void GameView::resizeEvent(QResizeEvent* event)
{
	int w = event->size().width();
	int h = event->size().height();
	if (m_render_device)
	{
		m_render_device->getPipeline().resize(w, h);
	}
}


void GameView::mousePressEvent(QMouseEvent*)
{
	setFocus();
	setMouseTracking(true);
	grabMouse();
	grabKeyboard();
	QCursor c = cursor();
	c.setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
	c.setShape(Qt::BlankCursor);
	setCursor(c);
	m_editor->getEngine().getInputSystem().enable(true);
}


void GameView::mouseMoveEvent(QMouseEvent* event)
{
	int half_width = width() / 2;
	int half_height = height() / 2;
	if (event->x() != half_width || event->y() != half_height)
	{
		m_editor->getEngine().getInputSystem().injectMouseXMove(event->x() - half_width);
		m_editor->getEngine().getInputSystem().injectMouseYMove(event->y() - half_height);
		QCursor c = cursor();
		c.setPos(mapToGlobal(QPoint(half_width, half_height)));
		c.setShape(Qt::BlankCursor);
		setCursor(c);
	}
}


void GameView::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape)
	{
		releaseMouse();
		releaseKeyboard();
		setMouseTracking(false);
		unsetCursor();
		m_editor->getEngine().getInputSystem().enable(false);
	}
}


void GameView::on_playButton_clicked()
{
	m_main_window.on_actionGame_mode_triggered();
}

