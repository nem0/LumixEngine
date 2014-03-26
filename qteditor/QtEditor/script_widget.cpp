#include "script_widget.h"
#include "ui_script_widget.h"
#include <qfiledialog.h>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"

ScriptWidget::ScriptWidget(QWidget *parent) :
	PropertyWidgetBase(parent),
    ui(new Ui::ScriptWidget)
{
    ui->setupUi(this);
}

ScriptWidget::~ScriptWidget()
{
    delete ui;
}


void ScriptWidget::onEntityProperties(Lux::PropertyListEvent& event)
{
	if (event.type_hash == crc32("script"))
	{
		for (int i = 0; i < event.properties.size(); ++i)
		{
			if (event.properties[i].name_hash == crc32("source"))
			{
				if (event.properties[i].data_size > 0)
				{
					ui->sourceEdit->setText((char*)event.properties[i].data);
				}
			}
		}
	}
}


void ScriptWidget::on_browseSourceButton_clicked()
{
	QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "scripts (*.cpp)");
	int len = (int)strlen(getClient()->getBasePath());
	if (strncmp(str.toLocal8Bit().data(), getClient()->getBasePath(), len) == 0)
	{
		ui->sourceEdit->setText(str.toLocal8Bit().data() + len);
	}
	else
	{
		ui->sourceEdit->setText(str);
	}
	getClient()->setComponentProperty("script", "source", ui->sourceEdit->text().toLocal8Bit().data(), ui->sourceEdit->text().size());
}

void ScriptWidget::on_sourceEdit_editingFinished()
{
	getClient()->setComponentProperty("script", "source", ui->sourceEdit->text().toLocal8Bit().data(), ui->sourceEdit->text().size());
}
