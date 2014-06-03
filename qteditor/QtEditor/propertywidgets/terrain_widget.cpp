#include "terrain_widget.h"
#include "ui_terrain_widget.h"
#include <QFileDialog>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"

TerrainWidget::TerrainWidget(QWidget* parent) 
	: PropertyWidgetBase(parent)
	, m_ui(new Ui::TerrainWidget)
{
	m_ui->setupUi(this);
}

TerrainWidget::~TerrainWidget()
{
	delete m_ui;
}

void TerrainWidget::onEntityProperties(Lux::PropertyListEvent& event)
{
	if (event.type_hash == crc32("terrain"))
	{
		for (int i = 0; i < event.properties.size(); ++i)
		{
			if (event.properties[i].name_hash == crc32("heightmap"))
			{
				if (event.properties[i].data_size > 0)
				{
					m_ui->heightmapEdit->setText((char*)event.properties[i].data);
				}
			}
		}
	}
}

void TerrainWidget::on_heightmapEdit_editingFinished()
{
	getClient()->setComponentProperty("terrain", "heightmap", m_ui->heightmapEdit->text().toLocal8Bit().data(), m_ui->heightmapEdit->text().size());
}

void TerrainWidget::on_browseHeightmap_clicked()
{
	QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "TGA images (*.tga)");
	int len = (int)strlen(getClient()->getBasePath());
	if (strncmp(str.toLocal8Bit().data(), getClient()->getBasePath(), len) == 0)
	{
		m_ui->heightmapEdit->setText(str.toLocal8Bit().data() + len);
	}
	else
	{
		m_ui->heightmapEdit->setText(str);
	}
	getClient()->setComponentProperty("terrain", "heightmap", m_ui->heightmapEdit->text().toLocal8Bit().data(), m_ui->heightmapEdit->text().size());
}

void TerrainWidget::on_browseMaterial_clicked()
{
	QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "Materials (*.mat)");
	int len = (int)strlen(getClient()->getBasePath());
	if (strncmp(str.toLocal8Bit().data(), getClient()->getBasePath(), len) == 0)
	{
		m_ui->materialEdit->setText(str.toLocal8Bit().data() + len);
	}
	else
	{
		m_ui->materialEdit->setText(str);
	}
	getClient()->setComponentProperty("terrain", "material", m_ui->materialEdit->text().toLocal8Bit().data(), m_ui->materialEdit->text().size());
}

void TerrainWidget::on_materialEdit_editingFinished()
{
	getClient()->setComponentProperty("terrain", "material", m_ui->materialEdit->text().toLocal8Bit().data(), m_ui->materialEdit->text().size());
}
