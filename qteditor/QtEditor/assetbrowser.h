#pragma once

#include <QDockWidget>

namespace Lux
{
	class EditorClient;
	class EditorServer;
}

namespace Ui 
{
	class AssetBrowser;
}

class AssetBrowser : public QDockWidget
{
	Q_OBJECT

public:
	explicit AssetBrowser(QWidget* parent = NULL);
	~AssetBrowser();
	void setEditorClient(Lux::EditorClient& client) { m_client = &client; }
	void setEditorServer(Lux::EditorServer& server) { m_server = &server; }
	void emitFileChanged(const char* path);

signals:
	void fileChanged(const QString& string);

private slots:
	void on_treeView_doubleClicked(const QModelIndex &index);
	void onFileChanged(const QString& path);

private:
	Ui::AssetBrowser* m_ui;
	class QFileSystemModel* m_model;
	class FileSystemWatcher* m_watcher;
	Lux::EditorClient* m_client;
	Lux::EditorServer* m_server;
	QString m_base_path;
};
