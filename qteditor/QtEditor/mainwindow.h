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
	Lumix::DelegateList<void(const QSize&)>& resized() { return m_resized; }

private slots:
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
	virtual void closeEvent(QCloseEvent* event) override;
	void on_actionProfiler_triggered();
	void on_actionPolygon_Mode_changed();
	void on_actionGame_mode_triggered();
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

private:
	virtual void resizeEvent(QResizeEvent* event) override;
	void fillRecentFiles();
	void onUniverseLoaded();

private:
	Ui::MainWindow* m_ui;
	Lumix::WorldEditor* m_world_editor;
	class LogWidget* m_log;
	class PropertyView* m_property_view;
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
	QList<QString> m_recent_files;
	Lumix::DelegateList<void(const QSize&)> m_resized;
};

