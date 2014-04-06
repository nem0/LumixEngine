#include "fileserverwidget.h"
#include "ui_fileserverwidget.h"
#include <QElapsedTimer>
#include "core/tcp_file_server.h"
#include "editor/editor_server.h"


class FileServerWatcher : public Lux::FS::TCPFileServer::IWatcher
{
	public:
		FileServerWatcher(FileServerWidget& widget)
			: m_widget(widget)
		{
			m_timer.start();
		}

		virtual void onFileOpen(const char* path, bool success)
		{
			m_widget.emitFileOpen(path, success, m_timer.elapsed());
		}

		QElapsedTimer m_timer;
		FileServerWidget& m_widget;
};

FileServerWidget::FileServerWidget(QWidget *parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::FileServerWidget)
{
	m_ui->setupUi(this);
	m_ui->tableWidget->setColumnWidth(0, 75);
	m_ui->tableWidget->setColumnWidth(1, 200);
	m_server = NULL;
	m_watcher = new FileServerWatcher(*this);
	connect(this, SIGNAL(fileOpened(const QString&, bool, qint64)), this, SLOT(onFileOpen(const QString&, bool, qint64)));
}

FileServerWidget::~FileServerWidget()
{
	delete m_ui;
	delete m_watcher;
}


void FileServerWidget::onFileOpen(const QString& path, bool success, qint64 time)
{
	int row = m_ui->tableWidget->rowCount();
	m_ui->tableWidget->insertRow(row);
	
	QString base_path = m_server->getTCPFileServer().getBasePath();

	QTableWidgetItem* new_item = new QTableWidgetItem(QString::number(time));
	m_ui->tableWidget->setItem(row, 0, new_item);

	if(path.startsWith(base_path))
	{
		QTableWidgetItem* new_item = new QTableWidgetItem(QString(path).remove(0, base_path.length()));
		m_ui->tableWidget->setItem(row, 1, new_item);
	}
	else
	{
		QTableWidgetItem* new_item = new QTableWidgetItem(path);
		m_ui->tableWidget->setItem(row, 1, new_item);
	}
	
	new_item = new QTableWidgetItem(success ? "succeeded" : "failed");
	m_ui->tableWidget->setItem(row, 2, new_item);
}


void FileServerWidget::emitFileOpen(const char* path, bool success, qint64 time)
{
	emit fileOpened(path, success, time);
}


void FileServerWidget::setEditorServer(Lux::EditorServer& server)
{
	m_server = &server;
	m_server->getTCPFileServer().setWatcher(m_watcher);
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
	m_server->getTCPFileServer().setWatcher(m_ui->checkBox->isChecked() ? m_watcher : NULL);
}
