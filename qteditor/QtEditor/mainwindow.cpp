#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <qfiledialog.h>
#include "editor/editor_client.h"
#include "log_widget.h"
#include "property_view.h"
#include "sceneview.h"
#include "gameview.h"


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
	m_client = NULL;
    ui->setupUi(this);
	ui->centralWidget->hide();
	m_log = new LogWidget;
	m_property_view = new PropertyView;
	m_scene_view = new SceneView;
	m_game_view = new GameView;
	addDockWidget(static_cast<Qt::DockWidgetArea>(1), m_game_view);
	m_game_view->hide();
	addDockWidget(static_cast<Qt::DockWidgetArea>(8), m_log);
	addDockWidget(static_cast<Qt::DockWidgetArea>(1), m_property_view);
	addDockWidget(static_cast<Qt::DockWidgetArea>(2), m_scene_view);
}

MainWindow::~MainWindow()
{
	delete m_log;
    delete ui;
	delete m_scene_view;
	delete m_property_view;
	delete m_game_view;
}


void MainWindow::setEditorClient(Lux::EditorClient& client)
{
	m_client = &client;
	m_property_view->setEditorClient(client);
	m_scene_view->setClient(&client);
}


GameView* MainWindow::getGameView() const
{
	return m_game_view;
}


SceneView* MainWindow::getSceneView() const
{
	return m_scene_view;
}


void MainWindow::on_actionLog_triggered()
{
	m_log->show();
}

void MainWindow::on_actionOpen_triggered()
{
	QByteArray path = QFileDialog::getOpenFileName(NULL, QString(), QString(), "universe (*.unv)").toLocal8Bit();
	int len = (int)strlen(m_client->getBasePath());
	if (!path.isEmpty())
	{
		if (strncmp(path.data(), m_client->getBasePath(), len) == 0)
		{
			m_client->loadUniverse(path.data() + len);
		}
		else
		{
			m_client->loadUniverse(path.data());
		}
	}
}

void MainWindow::on_actionSave_As_triggered()
{
	QByteArray path = QFileDialog::getSaveFileName().toLocal8Bit();
	int len = (int)strlen(m_client->getBasePath());
	if (!path.isEmpty())
	{
		if (strncmp(path.data(), m_client->getBasePath(), len) == 0)
		{
			m_client->saveUniverse(path.data() + len);
		}
		else
		{
			m_client->saveUniverse(path.data());
		}
	}
}

void MainWindow::on_actionCreate_triggered()
{
	m_client->addEntity();
}

void MainWindow::on_actionProperties_triggered()
{
	m_property_view->show();
}

void MainWindow::on_actionE_xit_triggered()
{
	close();
}

void MainWindow::on_actionGame_view_triggered()
{
	m_game_view->show();
}
