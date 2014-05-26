#include "physics_box_widget.h"
#include "ui_physics_box_widget.h"
#include "core/crc32.h"
#include "core/vec3.h"
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
				if (event.properties[i].data_size == sizeof(bool))
				{
					bool b;
					memcpy(&b, event.properties[i].data, sizeof(bool));
					m_ui->isDynamicCheckBox->setChecked(b);
				}
			}
			else if (event.properties[i].name_hash == crc32("size"))
			{
				if (event.properties[i].data_size == sizeof(Lux::Vec3))
				{
					Lux::Vec3 size;
					memcpy(&size, event.properties[i].data, sizeof(Lux::Vec3));
					m_ui->xSizeInput->setValue(size.x);
					m_ui->ySizeInput->setValue(size.y);
					m_ui->zSizeInput->setValue(size.z);
				}
			}
		}
	}
}

void PhysicsBoxWidget::on_isDynamicCheckBox_toggled(bool)
{
	bool b = m_ui->isDynamicCheckBox->isChecked();
	getClient()->setComponentProperty("box_rigid_actor", "dynamic", &b, sizeof(b)); 
}

void PhysicsBoxWidget::on_xSizeInput_valueChanged(double arg1)
{
	Lux::Vec3 v;
	v.x = (float)m_ui->xSizeInput->value();
	v.y = (float)m_ui->ySizeInput->value();
	v.z = (float)m_ui->zSizeInput->value();
	getClient()->setComponentProperty("box_rigid_actor", "size", &v, sizeof(v)); 
}

void PhysicsBoxWidget::on_ySizeInput_valueChanged(double arg1)
{
	Lux::Vec3 v;
	v.x = (float)m_ui->xSizeInput->value();
	v.y = (float)m_ui->ySizeInput->value();
	v.z = (float)m_ui->zSizeInput->value();
	getClient()->setComponentProperty("box_rigid_actor", "size", &v, sizeof(v)); 
}

void PhysicsBoxWidget::on_zSizeInput_valueChanged(double arg1)
{
	Lux::Vec3 v;
	v.x = (float)m_ui->xSizeInput->value();
	v.y = (float)m_ui->ySizeInput->value();
	v.z = (float)m_ui->zSizeInput->value();
	getClient()->setComponentProperty("box_rigid_actor", "size", &v, sizeof(v)); 
}
