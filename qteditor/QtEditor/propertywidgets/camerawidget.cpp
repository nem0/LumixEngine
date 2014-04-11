#include "camerawidget.h"
#include "ui_camerawidget.h"
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"


CameraWidget::CameraWidget(QWidget *parent) 
	: PropertyWidgetBase(parent)
	, m_ui(new Ui::CameraWidget)
{
	m_ui->setupUi(this);
}

CameraWidget::~CameraWidget()
{
	delete m_ui;
}


void CameraWidget::onEntityProperties(Lux::PropertyListEvent& event)
{
	if (event.type_hash == crc32("camera"))
	{
		for (int i = 0; i < event.properties.size(); ++i)
		{
			if (event.properties[i].name_hash == crc32("priority"))
			{
				if (event.properties[i].data_size == 4)
				{
					m_ui->priorityInput->setValue(*(int*)event.properties[i].data);
				}
			}
		}
	}
}


void CameraWidget::on_priorityInput_valueChanged(int arg1)
{
	int32_t priority = arg1;
	getClient()->setComponentProperty("camera", "priority", &priority, sizeof(priority));
}
