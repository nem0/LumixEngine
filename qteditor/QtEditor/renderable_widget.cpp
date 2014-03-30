#include "renderable_widget.h"
#include "ui_renderable_widget.h"
#include <QFileDialog>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"

RenderableWidget::RenderableWidget(QWidget *parent) :
	PropertyWidgetBase(parent),
	ui(new Ui::RenderableWidget)
{
	ui->setupUi(this);
}


RenderableWidget::~RenderableWidget()
{
	delete ui;
}


void RenderableWidget::onEntityProperties(Lux::PropertyListEvent& event)
{
	if (event.type_hash == crc32("renderable"))
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


void RenderableWidget::on_browseSource_clicked()
{
	QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "models (*.msh)");
	int len = (int)strlen(getClient()->getBasePath());
	if (strncmp(str.toLocal8Bit().data(), getClient()->getBasePath(), len) == 0)
	{
		ui->sourceEdit->setText(str.toLocal8Bit().data() + len);
	}
	else
	{
		ui->sourceEdit->setText(str);
	}
	getClient()->setComponentProperty("renderable", "source", ui->sourceEdit->text().toLocal8Bit().data(), ui->sourceEdit->text().size());
}


void RenderableWidget::on_sourceEdit_editingFinished()
{
	getClient()->setComponentProperty("renderable", "source", ui->sourceEdit->text().toLocal8Bit().data(), ui->sourceEdit->text().size());
}

