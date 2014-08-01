#pragma once

#include <QDockWidget>

namespace Lumix
{
	class WorldEditor;
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
	void setWorldEditor(Lumix::WorldEditor& server) { m_server = &server; }
	void emitFileChanged(const char* path);

private:
	void handleDoubleClick(const class QFileInfo& file_info);
	void exportAnimation(const QFileInfo& file_info);
	void exportModel(const QFileInfo& file_info);
	void onFileSystemWatcherCallback(const char* path);

signals:
	void fileChanged(const QString& string);

private slots:
	void on_treeView_doubleClicked(const QModelIndex &index);
	void onFileChanged(const QString& path);
	void on_searchInput_textEdited(const QString &arg1);
	void on_listWidget_activated(const QModelIndex &index);
	void on_treeView_customContextMenuRequested(const QPoint &pos);
	void on_exportFinished(int);
	void on_filterComboBox_currentTextChanged(const QString &arg1);

	private:
	Ui::AssetBrowser* m_ui;
	class QFileSystemModel* m_model;
	class FileSystemWatcher* m_watcher;
	Lumix::WorldEditor* m_server;
	QString m_base_path;
};
