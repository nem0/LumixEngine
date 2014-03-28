#include "gameview.h"
#include "ui_gameview.h"
#include <QMouseEvent>
#include "graphics/pipeline.h"


GameView::GameView(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::GameView)
{
    ui->setupUi(this);
	m_pipeline = NULL;
}

GameView::~GameView()
{
    delete ui;
}


QWidget* GameView::getContentWidget() const
{
	return ui->viewFrame;
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
