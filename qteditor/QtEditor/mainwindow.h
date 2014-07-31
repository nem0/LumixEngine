#pragma once

#include <QMainWindow>

namespace Lumix
{
	class EditorServer;
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

	void setEditorServer(Lumix::EditorServer& server);
	class SceneView* getSceneView() const;
	class GameView* getGameView() const;
	class MaterialManager* getMaterialManager() const { return m_material_manager_ui; }

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

    void on_actionMaterial_manager_triggered();

    void on_actionPolygon_Mode_changed();

    void on_actionGame_mode_triggered();

    void on_actionLook_at_selected_entity_triggered();

    void on_actionNew_triggered();

    void on_actionSave_triggered();

private:
	Ui::MainWindow* m_ui;
	Lumix::EditorServer* m_server;
	class LogWidget* m_log;
	class PropertyView* m_property_view;
	class SceneView* m_scene_view;
	class GameView* m_game_view;
	class AssetBrowser* m_asset_browser;
	class ScriptCompilerWidget* m_script_compiler_ui;
	class FileServerWidget* m_file_server_ui;
	class MaterialManager* m_material_manager_ui;
	class ProfilerUI* m_profiler_ui;
};

