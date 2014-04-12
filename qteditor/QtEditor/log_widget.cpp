#include "log_widget.h"
#include "ui_logwidget.h"
#include "core/log.h"


LogWidget::LogWidget(QWidget* parent) :
	QDockWidget(parent),
	m_ui(new Ui::LogWidget)
{
	m_ui->setupUi(this);
	connect(this, SIGNAL(infoReceived(const QString&, const QString&)), this, SLOT(onInfoReceived(const QString&, const QString&)));
	Lux::g_log_info.getCallback().bind<LogWidget, &LogWidget::onInfo>(this);
	Lux::g_log_warning.getCallback().bind<LogWidget, &LogWidget::onInfo>(this);
	Lux::g_log_error.getCallback().bind<LogWidget, &LogWidget::onInfo>(this);

}


void LogWidget::onInfo(const char* system, const char* message)
{
	emit infoReceived(system, message);
}


void LogWidget::onInfoReceived(const QString& system, const QString& message)
{
	int row = m_ui->tableWidget->rowCount();
	m_ui->tableWidget->insertRow(row);
	QTableWidgetItem* item1 = new QTableWidgetItem;
	item1->setText(system);
	m_ui->tableWidget->setItem(row, 0, item1);
	QTableWidgetItem* item2 = new QTableWidgetItem;
	item2->setText(message);
	m_ui->tableWidget->setItem(row, 1, item2);
}


LogWidget::~LogWidget()
{
	delete m_ui;
}

void LogWidget::on_clearButton_clicked()
{
	while (m_ui->tableWidget->rowCount() > 0)
	{
		m_ui->tableWidget->removeRow(0);
	}
}
