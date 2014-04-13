#pragma once

#include <QDockWidget>
#include "core/file_events_device.h"

namespace Lux
{
	class EditorServer;
}

namespace Ui
{
	class FileServerWidget;
}

class FileServerWidget : public QDockWidget
{
	Q_OBJECT

public:
	explicit FileServerWidget(QWidget* parent = NULL);
	~FileServerWidget();
	
	void setEditorServer(Lux::EditorServer& server);
	void fsEventCb(const Lux::FS::Event& event, int32_t param);
	void emitFileEvent(const Lux::FS::Event& event, qint64 time);

signals:
	void fileEvent(qint32 event, qint64 handle, const QString& path, qint32 ret, qint32 param, qint64 time);

public slots:
	void onFileEvent(qint32 event, qint64 handle, const QString& path, qint32 ret, qint32 param, qint64 time);

private slots:
	void on_pushButton_clicked();

	void on_checkBox_stateChanged(int arg1);

private:
	Ui::FileServerWidget* m_ui;
	Lux::EditorServer* m_server;
	class FileServerWatcher* m_watcher;
};

