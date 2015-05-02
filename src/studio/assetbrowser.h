#pragma once

#include <qdockwidget.h>
#include <qitemselectionmodel.h>

namespace Lumix
{
	class IEditorCommand;
	class WorldEditor;
}

namespace Ui
{
	class AssetBrowser;
}

class Notifications;
class ScriptCompiler;

class AssetBrowser : public QDockWidget
{
	Q_OBJECT

public:
	explicit AssetBrowser(QWidget* parent = NULL);
	~AssetBrowser();
	void setWorldEditor(Lumix::WorldEditor& editor);
	void emitFileChanged(const char* path);
	void setScriptCompiler(ScriptCompiler* compiler) { m_compiler = compiler; }
	void setNotifications(Notifications* notifications) { m_notifications = notifications; }

private:
	void handleDoubleClick(const class QFileInfo& file_info);
	void exportAnimation(const QFileInfo& file_info);
	void exportModel(const QFileInfo& file_info);
	void importAsset(const QFileInfo& file_info);
	void onFileSystemWatcherCallback(const char* path);
	void setExtentionsFilter(const QStringList& filter);

	static Lumix::IEditorCommand* createInsertMeshCommand(Lumix::WorldEditor& editor);

signals:
	void fileChanged(const QString& string);
	void fileSelected(const char* path);

private slots:
	void on_treeView_doubleClicked(const QModelIndex &index);
	void onFileChanged(const QString& path);
	void on_searchInput_textEdited(const QString &arg1);
	void on_treeView_customContextMenuRequested(const QPoint &pos);
	void on_exportFinished(int);
	void on_filterComboBox_currentTextChanged(const QString &arg1);

    void on_treeView_clicked(const QModelIndex &index);

private:
	Ui::AssetBrowser* m_ui;
	class QFileSystemModel* m_model;
	class FlatFileListModel* m_flat_filtered_model;
	class FileSystemWatcher* m_watcher;
	Lumix::WorldEditor* m_editor;
	QString m_base_path;
	Notifications* m_notifications;
	ScriptCompiler* m_compiler;
	QList<struct ProcessInfo> m_processes;
	QStringList m_extension_filter;
};
