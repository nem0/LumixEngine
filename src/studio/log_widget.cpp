#include "log_widget.h"
#include "ui_logwidget.h"
#include "core/log.h"


LogWidget::LogWidget(QWidget* parent)
	: QDockWidget(parent)
	, m_ui(new Ui::LogWidget)
{
	m_timer.start();
	m_ui->setupUi(this);
	m_ui->tabWidget->connect(m_ui->tabWidget,
							 &QTabWidget::currentChanged,
							 this,
							 &LogWidget::onTabChanged);

	for (int i = 0; i < Lumix::lengthOf(m_all_logs_count); ++i)
	{
		m_all_logs_count[i] = 0;
		m_new_logs_count[i] = 0;
	}

	connect(this, &LogWidget::logReceived, this, &LogWidget::onLogReceived);
	Lumix::g_log_info.getCallback().bind<LogWidget, &LogWidget::onInfo>(this);
	Lumix::g_log_warning.getCallback().bind<LogWidget, &LogWidget::onWarning>(
		this);
	Lumix::g_log_error.getCallback().bind<LogWidget, &LogWidget::onError>(this);
}


void LogWidget::onTabChanged(int index)
{
	if (index >= 0 && index < TYPE_COUNT)
	{
		m_new_logs_count[index] = 0;
		updateCountersUI();
	}
}


void LogWidget::onInfo(const char* system, const char* message)
{
	emit logReceived(INFO, system, message);
}


void LogWidget::onWarning(const char* system, const char* message)
{
	emit logReceived(WARNING, system, message);
}


void LogWidget::onError(const char* system, const char* message)
{
	emit logReceived(ERROR, system, message);
}


void LogWidget::onLogReceived(Type type,
							  const QString& system,
							  const QString& message)
{
	QTableWidget* table = m_ui->errorsTable;
	++m_all_logs_count[(int)type];
	++m_new_logs_count[(int)type];
	switch (type)
	{
		case INFO:
			table = m_ui->infosTable;
			break;
		case WARNING:
			table = m_ui->warningsTable;
			break;
		case ERROR:
			table = m_ui->errorsTable;
			break;
	}
	int row = table->rowCount();
	table->insertRow(row);
	QTableWidgetItem* item0 = new QTableWidgetItem;
	item0->setText(QString::number(double(m_timer.nsecsElapsed() / 1000000000.0)));
	table->setItem(row, 0, item0);
	QTableWidgetItem* item1 = new QTableWidgetItem;
	item1->setText(system);
	table->setItem(row, 1, item1);
	QTableWidgetItem* item2 = new QTableWidgetItem;
	item2->setText(message);
	table->setItem(row, 2, item2);
	table->resizeRowToContents(row);
	
	updateCountersUI();
}


LogWidget::~LogWidget()
{
	delete m_ui;
}


void LogWidget::on_clearButton_clicked()
{
	for (int i = 0; i < Lumix::lengthOf(m_all_logs_count); ++i)
	{
		m_all_logs_count[i] = 0;
		m_new_logs_count[i] = 0;
	}

	while (m_ui->errorsTable->rowCount() > 0)
	{
		m_ui->errorsTable->removeRow(0);
	}
	while (m_ui->warningsTable->rowCount() > 0)
	{
		m_ui->warningsTable->removeRow(0);
	}
	while (m_ui->infosTable->rowCount() > 0)
	{
		m_ui->infosTable->removeRow(0);
	}

	updateCountersUI();
}


void LogWidget::updateCountersUI()
{
	m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->errorsTab),
		QString("Errors %1(%2)")
		.arg(m_all_logs_count[ERROR])
		.arg(m_new_logs_count[ERROR]));
	m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->warningsTab),
		QString("Warnings %1(%2)")
		.arg(m_all_logs_count[WARNING])
		.arg(m_new_logs_count[WARNING]));
	m_ui->tabWidget->setTabText(m_ui->tabWidget->indexOf(m_ui->infosTab),
		QString("Infos %1(%2)")
		.arg(m_all_logs_count[INFO])
		.arg(m_new_logs_count[INFO]));
}
