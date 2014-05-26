#include "physics_box_widget.h"
#include "ui_physics_box_widget.h"
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"

PhysicsBoxWidget::PhysicsBoxWidget(QWidget* parent) 
	: PropertyWidgetBase(parent)
	, m_ui(new Ui::PhysicsBoxWidget)
{
	m_ui->setupUi(this);
}

PhysicsBoxWidget::~PhysicsBoxWidget()
{
	delete m_ui;
}

const char* PhysicsBoxWidget::getTitle() const
{
	return "Physics Box";
}


void PhysicsBoxWidget::onEntityProperties(Lux::PropertyListEvent& event)
{
	if (event.type_hash == crc32("box_rigid_actor"))
	{
		for (int i = 0; i < event.properties.size(); ++i)
		{
			if (event.properties[i].name_hash == crc32("dynamic"))
			{
				if (event.properties[i].data_size > 0)
				{
					bool b;
					ASSERT(sizeof(bool) == event.properties[i].data_size);
					memcpy(&b, event.properties[i].data, sizeof(bool));
					m_ui->isDynamicCheckBox->setChecked(b);
				}
			}
		}
	}
}



void PhysicsBoxWidget::on_isDynamicCheckBox_toggled(bool checked)
{
	bool b = m_ui->isDynamicCheckBox->isChecked();
	getClient()->setComponentProperty("box_rigid_actor", "dynamic", &b, sizeof(b)); 
}
