#pragma once

#include <QDockWidget>
#include <qtablewidget.h>
#include "core/fs/file_events_device.h"

namespace Lumix
{
	class WorldEditor;
}

namespace Ui
{
	class FileServerWidget;
}

class FileServerWidget : public QDockWidget
{
	Q_OBJECT

public:
	explicit FileServerWidget(QWidget* parent = nullptr);
	~FileServerWidget();
	
	void setWorldEditor(Lumix::WorldEditor& server);
	void fsEventCb(const Lumix::FS::Event& event, int32_t param);
	void emitFileEvent(const Lumix::FS::Event& event, qint64 time);

signals:
	void fileEvent(qint32 event, qint64 handle, const QString& path, qint32 ret, qint32 param, qint64 time);

public slots:
	void onFileEvent(qint32 event, qint64 handle, const QString& path, qint32 ret, qint32 param, qint64 time);

private slots:
	void on_pushButton_clicked();
	void on_filterCB_clicked();
    void on_filter_returnPressed();

	void on_checkBox_stateChanged(int arg1);

private:
	void filterRow(int row);
	void filterTable();

	Ui::FileServerWidget* m_ui;
	Lumix::WorldEditor* m_server;
	class FileServerWatcher* m_watcher;
};

