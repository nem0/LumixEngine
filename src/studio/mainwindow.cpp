#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "assetbrowser.h"
#include "editor/entity_template_system.h"
#include "editor/gizmo.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "entity_list.h"
#include "entity_template_list.h"
#include "fileserverwidget.h"
#include "gameview.h"
#include "log_widget.h"
#include "notifications.h"
#include "property_view.h"
#include "sceneview.h"
#include "scripts/scriptcompilerwidget.h"
#include "scripts/scriptcompiler.h"
#include "profilerui.h"
#include <qcombobox.h>
#include <qdir.h>
#include <qevent.h>
#include <qfiledialog.h>
#include <qinputdialog.h>
#include <qlabel.h>
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
	m_game_view = new GameView(*this);
	m_asset_browser = new AssetBrowser;
	m_script_compiler_ui = new ScriptCompilerWidget;
	m_file_server_ui = new FileServerWidget;
	m_profiler_ui = new ProfilerUI;
	m_entity_template_list_ui = new EntityTemplateList;
	m_notifications = Notifications::create(*this);
	m_entity_list = new EntityList(NULL);

	m_toggle_game_mode_after_compile = false;
	connect(m_script_compiler_ui->getCompiler(), &ScriptCompiler::compiled, this, &MainWindow::onScriptCompiled);

	QSettings settings("Lumix", "QtEditor");
	bool geometry_restored = restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
	
	m_window_menu = new QMenu("Windows", m_ui->menuView);
	m_ui->menuView->addMenu(m_window_menu);
	m_window_menu->connect(m_window_menu, &QMenu::aboutToShow, [this]()
	{
		for (auto info : m_dock_infos)
		{
			info.m_action->setChecked(info.m_widget->isVisible());
		}
	});
	addEditorDock(static_cast<Qt::DockWidgetArea>(2), m_asset_browser, &MainWindow::on_actionAsset_Browser_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(2), m_entity_list, &MainWindow::on_actionEntity_list_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(2), m_entity_template_list_ui, &MainWindow::on_actionEntity_templates_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(8), m_file_server_ui, &MainWindow::on_actionFile_server_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(1), m_game_view, &MainWindow::on_actionGame_view_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(8), m_log, &MainWindow::on_actionLog_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(1), m_profiler_ui, &MainWindow::on_actionProfiler_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(1), m_property_view, &MainWindow::on_actionProperties_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(2), m_scene_view, &MainWindow::on_actionScene_View_triggered);
	addEditorDock(static_cast<Qt::DockWidgetArea>(8), m_script_compiler_ui, &MainWindow::on_actionScript_compiler_triggered);

	createLayoutCombobox();

	m_property_view->setAssetBrowser(*m_asset_browser);

	int size = settings.beginReadArray("recent_files");
	for (int i = 0; i < size; ++i)
	{
		settings.setArrayIndex(i);
		m_recent_files.push_back(settings.value("filename").toString());
	}
	settings.endArray();
	m_recent_files_menu = new QMenu(m_ui->menuFile);
	m_recent_files_menu->setTitle("Recent Files");
	m_ui->menuFile->insertMenu(m_ui->actionSave, m_recent_files_menu);
	m_recent_files_menu->connect(m_recent_files_menu, &QMenu::triggered, [this](QAction* action)
	{
		auto path = action->text().toLatin1();
		m_world_editor->loadUniverse(Lumix::Path(path.data()));
	});
	fillRecentFiles();

	geometry_restored = geometry_restored && restoreState(settings.value("mainWindowState").toByteArray());
	if (!geometry_restored)
	{
		QFile file("editor/layouts/main.bin");
		if (file.open(QIODevice::ReadWrite))
		{
			int size;
			file.read((char*)&size, sizeof(size));
			QByteArray geom = file.read(size);
			restoreGeometry(geom);
			file.read((char*)&size, sizeof(size));
			QByteArray state = file.read(size);
			restoreState(state);
		}
	}
}


void MainWindow::installPlugins()
{
	m_property_view->addEntityComponentPlugin(new ScriptComponentPlugin(*m_world_editor, *m_script_compiler_ui->getCompiler()));
	m_property_view->addEntityComponentPlugin(new TerrainComponentPlugin(*m_world_editor, m_entity_template_list_ui, m_entity_list));
	m_property_view->addEntityComponentPlugin(new GlobalLightComponentPlugin());
}


void MainWindow::createLayoutCombobox()
{
	m_layout_combobox = new QComboBox();
	QWidget* widget = new QWidget(m_ui->menuBar);
	QHBoxLayout* layout = new QHBoxLayout(widget);
	QLabel* label = new QLabel("Layout");
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(label);
	layout->addWidget(m_layout_combobox);
	m_ui->menuBar->setCornerWidget(widget);
	QDir dir("editor/layouts/");
	auto files = dir.entryInfoList();
	for (const auto& file : files)
	{
		if (file.baseName() != "")
		{
			m_layout_combobox->addItem(file.baseName());
		}
	}
	connect(m_layout_combobox, &QComboBox::currentTextChanged, [this](const QString & text)
	{
		QFile file(QString("editor/layouts/%1.bin").arg(text));
		if (file.open(QIODevice::ReadWrite))
		{
			int size;
			file.read((char*)&size, sizeof(size));
			QByteArray geom = file.read(size);
			restoreGeometry(geom);
			file.read((char*)&size, sizeof(size));
			QByteArray state = file.read(size);
			restoreState(state);
		}
	});;
}


void MainWindow::addEditorDock(Qt::DockWidgetArea area, QDockWidget* widget, void (MainWindow::*callback)())
{
	DockInfo info;
	info.m_widget = widget;
	QAction* action = widget->toggleViewAction();
	action->setCheckable(true);
	m_window_menu->addAction(action);
	info.m_action = action;
	action->connect(action, &QAction::triggered, this, callback);
	m_dock_infos.push_back(info);
	addDockWidget(area, widget);
}


void MainWindow::fillRecentFiles()
{
	m_recent_files_menu->clear();
	for (auto file : m_recent_files)
	{
		m_recent_files_menu->addAction(file);
	}
}


void MainWindow::resizeEvent(QResizeEvent* event)
{
	emit resized(event->size());
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
	settings.beginWriteArray("recent_files");
	int i = 0;
	for (auto file : m_recent_files)
	{
		settings.setArrayIndex(i);
		settings.setValue("filename", file);
		++i;
	}
	settings.endArray();
	QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow()
{
	delete m_log;
	delete m_ui;
	delete m_scene_view;
	delete m_property_view;
	delete m_asset_browser;
	delete m_script_compiler_ui;
	delete m_file_server_ui;
	delete m_profiler_ui;
	delete m_entity_template_list_ui;
	Notifications::destroy(m_notifications);
}


void MainWindow::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	m_file_server_ui->setWorldEditor(editor);
	m_asset_browser->setWorldEditor(editor);
	m_property_view->setWorldEditor(editor);
	m_entity_template_list_ui->setWorldEditor(editor);
	m_game_view->setWorldEditor(editor);
	m_entity_list->setWorldEditor(editor);
	m_script_compiler_ui->setWorldEditor(editor);
	m_asset_browser->setScriptCompiler(m_script_compiler_ui->getCompiler());
	m_asset_browser->setNotifications(m_notifications);

	m_world_editor->universeLoaded().bind<MainWindow, &MainWindow::onUniverseLoaded>(this);

	installPlugins();
}

void MainWindow::onUniverseLoaded()
{
	const char* path = m_world_editor->getUniversePath().c_str();
	
	if (m_recent_files.indexOf(path, 0) < 0)
	{
		m_recent_files.push_back(path);
		if (m_recent_files.size() > 6)
		{
			m_recent_files.pop_front();
		}
		fillRecentFiles();
	}
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
	QString filename = QFileDialog::getOpenFileName(NULL, QString(), QString(), "universe (*.unv)");
	QByteArray path = filename.toLocal8Bit();
	if (!path.isEmpty())
	{
		m_world_editor->loadUniverse(Lumix::Path(path.data()));
	}
}

void MainWindow::on_actionSave_As_triggered()
{
	QByteArray path = QFileDialog::getSaveFileName().toLocal8Bit();
	if (!path.isEmpty())
	{
		m_world_editor->saveUniverse(Lumix::Path(path.data()));
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

void MainWindow::on_actionPolygon_Mode_changed()
{
	m_world_editor->setWireframe(m_ui->actionPolygon_Mode->isChecked());
}

void MainWindow::onScriptCompiled()
{
	if (m_toggle_game_mode_after_compile)
	{
		m_world_editor->toggleGameMode();
	}
	m_toggle_game_mode_after_compile = false;
}

void MainWindow::on_actionGame_mode_triggered()
{
	if (!m_world_editor->isGameMode())
	{
		m_script_compiler_ui->getCompiler()->compileAllModules();
		m_toggle_game_mode_after_compile = true;
	}
	else
	{
		m_world_editor->toggleGameMode();
	}
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
	if (m_world_editor->getSelectedEntities().size() == 1)
	{
		bool ok = false;
		QString text = QInputDialog::getText(this, tr("Entity template"), tr("Template name:"), QLineEdit::Normal, tr(""), &ok);
		if (ok)
		{
			m_world_editor->getEntityTemplateSystem().createTemplateFromEntity(text.toLatin1().data(), m_world_editor->getSelectedEntities()[0]);
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

void MainWindow::on_actionRemove_triggered()
{
	if (!m_world_editor->getSelectedEntities().empty())
	{
		m_world_editor->destroyEntities(&m_world_editor->getSelectedEntities()[0], m_world_editor->getSelectedEntities().size());
	}
}

void MainWindow::on_actionEntity_list_triggered()
{
	m_entity_list->show();
}

void MainWindow::on_actionMeasure_triggered()
{
	m_world_editor->toggleMeasure();
}

void MainWindow::on_actionSave_Layout_triggered()
{
	bool ok;
	QString text = QInputDialog::getText(this, "Save layout", "Layout name:", QLineEdit::Normal, "", &ok);
	if (ok && !text.isEmpty())
	{
		QFile file(QString("editor/layouts/%1.bin").arg(text));
		if (file.open(QIODevice::ReadWrite))
		{
			auto geom = saveGeometry();
			auto state = saveState();
			int size = geom.size();
			file.write((const char*)&size, sizeof(size));
			file.write(geom);
			size = state.size();
			file.write((const char*)&size, sizeof(size));
			file.write(state);
			bool item_exists = false;
			for (int i = 0; i < m_layout_combobox->count(); ++i)
			{
				if (m_layout_combobox->itemText(i) == text)
				{
					item_exists = true;
					break;
				}
			}
			if (!item_exists)
			{
				m_layout_combobox->addItem(text);
			}
		}
	}
}


void MainWindow::on_actionCenter_Pivot_triggered()
{
	m_world_editor->getGizmo().togglePivotMode();
}


void MainWindow::on_actionLocal_Global_triggered()
{
	m_world_editor->getGizmo().toggleCoordSystem();
}

void MainWindow::on_actionCopy_triggered()
{
	m_world_editor->copyEntity();
}

void MainWindow::on_actionPaste_triggered()
{
	m_world_editor->pasteEntity();
}

void MainWindow::on_actionSame_mesh_triggered()
{
	m_world_editor->selectEntitiesWithSameMesh();
}

void MainWindow::on_actionHide_triggered()
{
	m_world_editor->hideEntities();
}

void MainWindow::on_actionShow_triggered()
{
	m_world_editor->showEntities();
}


void MainWindow::on_actionSave_commands_triggered()
{
	QByteArray path = QFileDialog::getSaveFileName().toLocal8Bit();
	if (!path.isEmpty())
	{
		m_world_editor->saveUndoStack(Lumix::Path(path.data()));
	}
}


void MainWindow::on_actionExecute_commands_triggered()
{
	QByteArray path = QFileDialog::getOpenFileName().toLocal8Bit();
	if (!path.isEmpty())
	{
		m_world_editor->executeUndoStack(Lumix::Path(path.data()));
	}
}