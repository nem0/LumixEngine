#include "physics_mesh_widget.h"
#include "ui_physics_mesh_widget.h"
#include <QFileDialog>
#include "core/crc32.h"
#include "core/path.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"


PhysicsMeshWidget::PhysicsMeshWidget(QWidget* parent)
	: PropertyWidgetBase(parent)
	, m_ui(new Ui::PhysicsMeshWidget)
{
	m_ui->setupUi(this);
}

PhysicsMeshWidget::~PhysicsMeshWidget()
{
	delete m_ui;
}

const char* PhysicsMeshWidget::getTitle() const
{
	return "Physics Mesh";
}

void PhysicsMeshWidget::onEntityProperties(Lux::PropertyListEvent& event)
{
	if (event.type_hash == crc32("mesh_rigid_actor"))
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

void PhysicsMeshWidget::on_lineEdit_editingFinished()
{
	getClient()->setComponentProperty("mesh_rigid_actor", "source", m_ui->sourceEdit->text().toLocal8Bit().data(), m_ui->sourceEdit->text().size());
}

void PhysicsMeshWidget::on_browseButton_clicked()
{
	QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "physics (*.pda)");
	int len = (int)strlen(getClient()->getBasePath());
	if (strncmp(str.toLocal8Bit().data(), getClient()->getBasePath(), len) == 0)
	{
		m_ui->sourceEdit->setText(str.toLocal8Bit().data() + len);
	}
	else
	{
		m_ui->sourceEdit->setText(str);
	}
	Lux::Path path((const char*)m_ui->sourceEdit->text().toLocal8Bit().data());
	getClient()->setComponentProperty("mesh_rigid_actor", "source", path.c_str(), path.length());
}
