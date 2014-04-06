#pragma once

#include <QDockWidget>

namespace Lux
{
	class EditorServer;
}

namespace Ui {
class FileServerWidget;
}

class FileServerWidget : public QDockWidget
{
	Q_OBJECT

public:
	explicit FileServerWidget(QWidget *parent = NULL);
	~FileServerWidget();
	
	void setEditorServer(Lux::EditorServer& server);
	void emitFileOpen(const char* path, bool success, qint64 time);

signals:
	void fileOpened(const QString& path, bool success, qint64 time);

public slots:
	void onFileOpen(const QString& path, bool success, qint64 time);

private slots:
    void on_pushButton_clicked();

private:
	Ui::FileServerWidget *m_ui;
	Lux::EditorServer* m_server;
	class FileServerWatcher* m_watcher;
};

