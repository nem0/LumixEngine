#include "fileserverwidget.h"
#include "ui_fileserverwidget.h"
#include <QElapsedTimer>
#include "engine/engine.h"
#include "core/file_system.h"
#include "core/tcp_file_server.h"
#include "core/file_events_device.h"
#include "editor/editor_server.h"

const QString eventNames[] = {
			"OPEN_BEGIN",
			"OPEN_FINISHED",
			"CLOSE_BEGIN",
			"CLOSE_FINISHED",
			"READ_BEGIN",
			"READ_FINISHED",
			"WRITE_BEGIN",
			"WRITE_FINISHED",
			"SIZE_BEGIN",
			"SIZE_FINISHED",
			"SEEK_BEGIN",
			"SEEK_FINISHED",
			"POS_BEGIN",
			"POS_FINISHED"
};


class FileServerWatcher
{
	public:
		FileServerWatcher(FileServerWidget& widget)
			: m_widget(widget)
		{
			m_timer.start();
			m_fe_device.OnEvent.bind<FileServerWatcher, &FileServerWatcher::fsEventCb>(this);
		}

		void fsEventCb(const Lux::FS::Event& event)
		{
			m_widget.emitFileEvent(event, m_timer.nsecsElapsed());
		}

		Lux::FS::FileEventsDevice& getFileEventDevice() 
		{ 
			return m_fe_device; 
		}

private:
		Lux::FS::FileEventsDevice m_fe_device;
		QElapsedTimer m_timer;
		FileServerWidget& m_widget;
};

FileServerWidget::FileServerWidget(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::FileServerWidget)
{
	m_ui->setupUi(this);
	m_ui->tableWidget->setColumnWidth(0, 75);
	m_ui->tableWidget->setColumnWidth(1, 200);
	m_server = NULL;
	m_watcher = new FileServerWatcher(*this);
	connect(this, SIGNAL(fileEvent(qint32, qint64, const QString&, qint32, qint32, qint64)), this, SLOT(onFileEvent(qint32, qint64, const QString&, qint32, qint32, qint64)));
}

FileServerWidget::~FileServerWidget()
{
	delete m_ui;
	delete m_watcher;
}



void FileServerWidget::onFileEvent(qint32 event, qint64 handle, const QString& path, qint32 ret, qint32 param, qint64 time)
{
	int row = m_ui->tableWidget->rowCount();
	m_ui->tableWidget->insertRow(row);
	
	QString base_path = m_server->getTCPFileServer().getBasePath();

	QTableWidgetItem* new_item = new QTableWidgetItem(QString::number(double(time / 1000000.0)));
	m_ui->tableWidget->setItem(row, 0, new_item);

	QTableWidgetItem* event_item = new QTableWidgetItem(eventNames[qint32(event)]);
	m_ui->tableWidget->setItem(row, 1, event_item);

	QTableWidgetItem* handle_item = new QTableWidgetItem(QString::number(handle));
	m_ui->tableWidget->setItem(row, 2, handle_item);

	QString real_path(path);
	if(path.startsWith(base_path))
	{
		QTableWidgetItem* path_item = new QTableWidgetItem(real_path.remove(0, base_path.length()));
		m_ui->tableWidget->setItem(row, 3, path_item);
	}
	else
	{
		QTableWidgetItem* path_item = new QTableWidgetItem(real_path);
		m_ui->tableWidget->setItem(row, 3, path_item);
	}
	
	QTableWidgetItem* param_item = new QTableWidgetItem(QString::number(param));
	m_ui->tableWidget->setItem(row, 4, param_item);

	QTableWidgetItem* return_item = new QTableWidgetItem(QString::number(ret));
	m_ui->tableWidget->setItem(row, 5, return_item);
}


void FileServerWidget::emitFileEvent(const Lux::FS::Event& event, qint64 time)
{
	emit fileEvent((qint32)event.type, event.handle, event.path, event.ret, event.param, time);
}


void FileServerWidget::setEditorServer(Lux::EditorServer& server)
{
	m_server = &server;

	Lux::FS::FileEventsDevice& dev = m_watcher->getFileEventDevice();
	Lux::FS::FileSystem& fs = m_server->getEngine().getFileSystem();
	fs.mount(&dev);
	fs.setDefaultDevice("memory:events:tcp");
}

void FileServerWidget::on_pushButton_clicked()
{
	while (m_ui->tableWidget->rowCount() > 0)
	{
		m_ui->tableWidget->removeRow(0);
	}
}

void FileServerWidget::on_checkBox_stateChanged(int)
{
	ASSERT(m_server);
	if (m_ui->checkBox->isChecked())
	{
		m_server->getEngine().getFileSystem().setDefaultDevice("memory:events:tcp");
	}
	else
	{
		m_server->getEngine().getFileSystem().setDefaultDevice("memory:tcp");
	}
}
