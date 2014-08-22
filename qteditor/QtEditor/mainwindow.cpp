#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "assetbrowser.h"
#include "editor/entity_template_system.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "entity_template_list.h"
#include "fileserverwidget.h"
#include "gameview.h"
#include "log_widget.h"
#include "notifications.h"
#include "property_view.h"
#include "sceneview.h"
#include "scripts/scriptcompilerwidget.h"
#include "materialmanager.h"
#include "profilerui.h"
#include <qfiledialog.h>
#include <qinputdialog.h>
#include <qevent.h>
#include <qsettings.h>


MainWindow::MainWindow(QWidget* parent) :
	QMainWindow(parent),
	m_ui(new Ui::MainWindow)
{
	m_ui->setupUi(this);
	m_ui->centralWidget->hide();
	setDockOptions(AllowNestedDocks | AnimatedDocks | AllowTabbedDocks);

	m_log = new LogWidget;
	m_property_view = new PropertyView;
	m_scene_view = new SceneView;
	m_game_view = new GameView;
	m_asset_browser = new AssetBrowser;
	m_script_compiler_ui = new ScriptCompilerWidget;
	m_file_server_ui = new FileServerWidget;
	m_material_manager_ui = new MaterialManager;
	m_profiler_ui = new ProfilerUI;
	m_entity_template_list_ui = new EntityTemplateList;
	m_notifications = Notifications::create(*this);

	QSettings settings("Lumix", "QtEditor");
	restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
	
	addDockWidget(static_cast<Qt::DockWidgetArea>(1), m_game_view);
	addDockWidget(static_cast<Qt::DockWidgetArea>(8), m_log);
	addDockWidget(static_cast<Qt::DockWidgetArea>(8), m_file_server_ui);
	addDockWidget(static_cast<Qt::DockWidgetArea>(8), m_script_compiler_ui);
	addDockWidget(static_cast<Qt::DockWidgetArea>(1), m_property_view);
	addDockWidget(static_cast<Qt::DockWidgetArea>(2), m_scene_view);
	addDockWidget(static_cast<Qt::DockWidgetArea>(2), m_asset_browser);
	addDockWidget(static_cast<Qt::DockWidgetArea>(8), m_material_manager_ui);
	addDockWidget(static_cast<Qt::DockWidgetArea>(1), m_profiler_ui);
	addDockWidget(static_cast<Qt::DockWidgetArea>(2), m_entity_template_list_ui);

	m_property_view->setScriptCompiler(m_script_compiler_ui->getCompiler());
	m_property_view->setAssetBrowser(*m_asset_browser);

	restoreState(settings.value("mainWindowState").toByteArray());
}


void MainWindow::resizeEvent(QResizeEvent* event)
{
	m_resized.invoke(event->size());
}


void MainWindow::update()
{
	m_notifications->update(m_world_editor->getEngine().getLastTimeDelta());
}


void MainWindow::closeEvent(QCloseEvent *event)
{
	QSettings settings("Lumix", "QtEditor");
	settings.setValue("mainWindowGeometry", saveGeometry());
	settings.setValue("mainWindowState", saveState());
	QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
	delete m_log;
	delete m_ui;
	delete m_scene_view;
	delete m_property_view;
	delete m_game_view;
	delete m_asset_browser;
	delete m_script_compiler_ui;
	delete m_file_server_ui;
	delete m_material_manager_ui;
	delete m_profiler_ui;
	delete m_entity_template_list_ui;
	Notifications::destroy(m_notifications);
}


void MainWindow::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	m_file_server_ui->setWorldEditor(editor);
	m_asset_browser->setWorldEditor(editor);
	m_material_manager_ui->setWorldEditor(editor);
	m_property_view->setWorldEditor(editor);
	m_entity_template_list_ui->setWorldEditor(editor);
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
	if (!path.isEmpty())
	{
		m_world_editor->loadUniverse(path.data());
	}
}

void MainWindow::on_actionSave_As_triggered()
{
	QByteArray path = QFileDialog::getSaveFileName().toLocal8Bit();
	if (!path.isEmpty())
	{
		m_world_editor->saveUniverse(path.data());
	}
}

void MainWindow::on_actionCreate_triggered()
{
	m_world_editor->addEntity();
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

void MainWindow::on_actionScript_compiler_triggered()
{
	m_script_compiler_ui->show();
}

void MainWindow::on_actionFile_server_triggered()
{
	m_file_server_ui->show();
}

void MainWindow::on_actionAsset_Browser_triggered()
{
	m_asset_browser->show();
}

void MainWindow::on_actionScene_View_triggered()
{
	m_scene_view->show();
}

void MainWindow::on_actionProfiler_triggered()
{
	m_profiler_ui->show();
}

void MainWindow::on_actionMaterial_manager_triggered()
{
	m_material_manager_ui->show();
}

void MainWindow::on_actionPolygon_Mode_changed()
{
	m_world_editor->setWireframe(m_ui->actionPolygon_Mode->isChecked());
}

void MainWindow::on_actionGame_mode_triggered()
{
	m_world_editor->toggleGameMode();
}

void MainWindow::on_actionLook_at_selected_entity_triggered()
{
	m_world_editor->lookAtSelected();
}

void MainWindow::on_actionNew_triggered()
{
	m_world_editor->newUniverse();
}

void MainWindow::on_actionSave_triggered()
{
	if (m_world_editor->getUniversePath()[0] == '\0')
	{
		on_actionSave_As_triggered();
	}
	else
	{
		m_world_editor->saveUniverse(m_world_editor->getUniversePath());
	}
}

void MainWindow::on_actionSnap_to_terrain_triggered()
{
	m_world_editor->snapToTerrain();
}

void MainWindow::on_actionSave_as_template_triggered()
{
	if (m_world_editor->getSelectedEntity().isValid())
	{
		bool ok = false;
		QString text = QInputDialog::getText(this, tr("Entity template"), tr("Template name:"), QLineEdit::Normal, tr(""), &ok);
		if (ok)
		{
			m_world_editor->getEntityTemplateSystem().createTemplateFromEntity(text.toLatin1().data(), m_world_editor->getSelectedEntity());
		}
	}
}

void MainWindow::on_actionEntity_templates_triggered()
{
	m_entity_template_list_ui->show();
}

void MainWindow::on_actionInstantiate_template_triggered()
{
	m_entity_template_list_ui->instantiateTemplate();
}

void MainWindow::on_actionUndo_triggered()
{
	m_world_editor->undo();
}

void MainWindow::on_actionRedo_triggered()
{
	m_world_editor->redo();
}
