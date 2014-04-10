#include "script_widget.h"
#include "ui_script_widget.h"
#include <qfiledialog.h>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"

ScriptWidget::ScriptWidget(QWidget* parent) :
	PropertyWidgetBase(parent),
	m_ui(new Ui::ScriptWidget)
{
	m_ui->setupUi(this);
}

ScriptWidget::~ScriptWidget()
{
	delete m_ui;
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
					m_ui->sourceEdit->setText((char*)event.properties[i].data);
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
		m_ui->sourceEdit->setText(str.toLocal8Bit().data() + len);
	}
	else
	{
		m_ui->sourceEdit->setText(str);
	}
	getClient()->setComponentProperty("script", "source", m_ui->sourceEdit->text().toLocal8Bit().data(), m_ui->sourceEdit->text().size());
}

void ScriptWidget::on_sourceEdit_editingFinished()
{
	getClient()->setComponentProperty("script", "source", m_ui->sourceEdit->text().toLocal8Bit().data(), m_ui->sourceEdit->text().size());
}
