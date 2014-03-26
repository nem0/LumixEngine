#include "log_widget.h"
#include "ui_logwidget.h"
#include "core/log.h"


LogWidget::LogWidget(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::LogWidget)
{
    ui->setupUi(this);
	Lux::g_log_info.getCallback().bind<LogWidget, &LogWidget::onInfo>(this);
}


void LogWidget::onInfo(const char* system, const char* message)
{
	int row = ui->tableWidget->rowCount();
	ui->tableWidget->insertRow(row);
	QTableWidgetItem *item1 = new QTableWidgetItem;
	item1->setText(system);
	ui->tableWidget->setItem(row, 0, item1);
	QTableWidgetItem *item2 = new QTableWidgetItem;
	item2->setText(message);
	ui->tableWidget->setItem(row, 1, item2);
}


LogWidget::~LogWidget()
{
    delete ui;
}

void LogWidget::on_clearButton_clicked()
{
	while (ui->tableWidget->rowCount() > 0)
	{
		ui->tableWidget->removeRow(0);
	}
}
