#include "gameview.h"
#include "ui_gameview.h"

#include "core/input_system.h"
#include "editor/world_editor.h"
#include "engine.h"
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
	m_isInputHandling = false;
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
	m_render_device =
		new WGLRenderDevice(m_editor->getEngine(), "pipelines/game_view.lua");
	m_render_device->setWidget(*getContentWidget());
}


QWidget* GameView::getContentWidget() const
{
	return m_ui->viewFrame;
}


void GameView::shutdown()
{
	m_render_device->shutdown();
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
	if (m_editor->isGameMode())
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
		m_isInputHandling = true;
	}
}


void GameView::disableInputHandling()
{
	m_isInputHandling = false;
	releaseMouse();
	releaseKeyboard();
	setMouseTracking(false);
	unsetCursor();
	m_editor->getEngine().getInputSystem().enable(false);
}


void GameView::focusOutEvent(QFocusEvent* event)
{
	disableInputHandling();
}


void GameView::mouseMoveEvent(QMouseEvent* event)
{
	if (!m_isInputHandling)
	{
		return;
	}

	int half_width = width() / 2;
	int half_height = height() / 2;
	if (event->x() != half_width || event->y() != half_height)
	{
		auto& input_system = m_editor->getEngine().getInputSystem();
		input_system.injectMouseXMove(event->x() - half_width);
		input_system.injectMouseYMove(event->y() - half_height);
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
		disableInputHandling();
	}
}


void GameView::onGameModeTriggered()
{
	if (m_editor->isGameMode())
	{
		m_ui->playButton->setText("Stop");
	}
	else
	{
		m_ui->playButton->setText("Play");
	}
}


void GameView::on_playButton_clicked()
{
	m_main_window.on_actionGame_mode_triggered();
}
