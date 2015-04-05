#pragma once

#include <QMainWindow>
#include "core/delegate_list.h"


namespace Lumix
{
	class WorldEditor;
}

namespace Ui
{
	class MainWindow;
}


class PropertyView;


class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = NULL);
	~MainWindow();

	void update();
	void setWorldEditor(Lumix::WorldEditor& world_editor);
	class SceneView* getSceneView() const;
	class GameView* getGameView() const;
	PropertyView* getPropertyView() const { return m_property_view; }
	class ScriptCompiler* getScriptCompiler() const;
	QMenuBar* getMenuBar() const;

signals:
	void resized(const QSize& size);

public slots:
	void on_actionGame_mode_triggered();

private slots:
	void onScriptCompiled();
	void on_actionLog_triggered();
	void on_actionOpen_triggered();
	void on_actionSave_As_triggered();
	void on_actionCreate_triggered();
	void on_actionProperties_triggered();
	void on_actionE_xit_triggered();
	void on_actionGame_view_triggered();
	void on_actionScript_compiler_triggered();
	void on_actionFile_server_triggered();
	void on_actionAsset_Browser_triggered();
	void on_actionScene_View_triggered();
	void on_actionProfiler_triggered();
	void on_actionPolygon_Mode_changed();
	void on_actionLook_at_selected_entity_triggered();
	void on_actionNew_triggered();
	void on_actionSave_triggered();
	void on_actionSnap_to_terrain_triggered();
	void on_actionSave_as_template_triggered();
	void on_actionEntity_templates_triggered();
	void on_actionInstantiate_template_triggered();
	void on_actionUndo_triggered();
	void on_actionRedo_triggered();
	void on_actionRemove_triggered();
	void on_actionEntity_list_triggered();
	void on_actionMeasure_triggered();
	void on_actionSave_Layout_triggered();
	void on_actionCenter_Pivot_triggered();
	void on_actionLocal_Global_triggered();
	void on_actionCopy_triggered();
	void on_actionPaste_triggered();
	void on_actionSame_mesh_triggered();
	void on_actionHide_triggered();
	void on_actionShow_triggered();
	void on_actionSave_commands_triggered();
	void on_actionExecute_commands_triggered();

private:
	class DockInfo
	{
		public:
			QDockWidget* m_widget;
			QAction* m_action;
	};

private:
	virtual void resizeEvent(QResizeEvent* event) override;
	virtual void closeEvent(QCloseEvent* event) override;
	void fillRecentFiles();
	void onUniverseLoaded();
	void addEditorDock(Qt::DockWidgetArea area, QDockWidget* widget, void (MainWindow::*callback)());
	void createLayoutCombobox();

private:
	Ui::MainWindow* m_ui;
	Lumix::WorldEditor* m_world_editor;
	class AnimationEditor* m_animation_editor;
	class LogWidget* m_log;
	PropertyView* m_property_view;
	class SceneView* m_scene_view;
	class GameView* m_game_view;
	class AssetBrowser* m_asset_browser;
	class ScriptCompilerWidget* m_script_compiler_ui;
	class FileServerWidget* m_file_server_ui;
	class ProfilerUI* m_profiler_ui;
	class EntityTemplateList* m_entity_template_list_ui;
	class Notifications* m_notifications;
	class EntityList* m_entity_list;
	QMenu* m_recent_files_menu;
	QMenu* m_window_menu;
	class QComboBox* m_layout_combobox;
	QList<QString> m_recent_files;
	QList<DockInfo> m_dock_infos;
	bool m_toggle_game_mode_after_compile;
};

