#pragma once

#include <QMainWindow>

namespace Lux
{
	class EditorClient;
	class EditorServer;
}

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = NULL);
	~MainWindow();

	void setEditorClient(Lux::EditorClient& client);
	void setEditorServer(Lux::EditorServer& server);
	class SceneView* getSceneView() const;
	class GameView* getGameView() const;

private slots:
	void on_actionLog_triggered();
	void on_actionOpen_triggered();
	void on_actionSave_As_triggered();
	void on_actionCreate_triggered();
	void on_actionProperties_triggered();
	void on_actionE_xit_triggered();
	void on_actionGame_view_triggered();
	virtual void closeEvent(QCloseEvent* event) override;

private:
	Ui::MainWindow* m_ui;
	Lux::EditorClient* m_client;
	class LogWidget* m_log;
	class PropertyView* m_property_view;
	class SceneView* m_scene_view;
	class GameView* m_game_view;
	class AssetBrowser* m_asset_browser;
	class ScriptCompilerWidget* m_script_compiler_ui;
	class FileServerWidget* m_file_server_ui;
};

