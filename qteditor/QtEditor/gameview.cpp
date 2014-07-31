#include "gameview.h"
#include "ui_gameview.h"
#include <QMouseEvent>
#include "editor/editor_server.h"
#include "graphics/pipeline.h"

GameView::GameView(QWidget* parent) :
	QDockWidget(parent),
	m_ui(new Ui::GameView)
{
	m_ui->setupUi(this);
	m_pipeline = NULL;
}

GameView::~GameView()
{
	delete m_ui;
}


QWidget* GameView::getContentWidget() const
{
	return m_ui->viewFrame;
}


void GameView::resizeEvent(QResizeEvent* event)
{
	int w = event->size().width();
	int h = event->size().height();
	if (m_pipeline)
	{
		m_pipeline->resize(w, h);
	}
}


void GameView::on_playButton_clicked()
{
	m_server->toggleGameMode();
}

