#include "property_widget_base.h"
#include "ui_property_widget_base.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"


PropertyWidgetBase::PropertyWidgetBase(QWidget* parent) :
	QFrame(parent),
	m_ui(new Ui::PropertyWidgetBase)
{
	m_ui->setupUi(this);
	m_form_layout = new QFormLayout(this);
}

PropertyWidgetBase::~PropertyWidgetBase()
{
	delete m_ui;
}

void PropertyWidgetBase::addProperty(const char* name, const char* label_text, Property::Type type, const char* file_type)
{
	Property& prop = m_properties.pushEmpty();
	prop.m_name = name;
	prop.m_name_hash = crc32(name);
	prop.m_type = type;
	prop.m_file_type = file_type ? file_type : "";
	
	QLabel* label = new QLabel(label_text, this);
	m_form_layout->setWidget(m_properties.size() - 1, QFormLayout::LabelRole, label);
	
	switch(type)
	{
		case Property::FILE:
			{
				QWidget* widget = new QWidget();
				QLineEdit* edit = new QLineEdit(widget);
				QPushButton* button = new QPushButton("...", widget);
				m_form_layout->setWidget(m_properties.size() - 1, QFormLayout::FieldRole, widget);
				QHBoxLayout* horizontal_layout = new QHBoxLayout(widget);
				horizontal_layout->addWidget(edit);
				horizontal_layout->addWidget(button);
				horizontal_layout->setContentsMargins(0, 0, 0, 0);
				prop.m_widget = edit;
				button->setProperty("general_widget_property", (int)(m_properties.size() - 1));
				edit->setProperty("general_widget_property", (int)(m_properties.size() - 1));
				connect(button, &QPushButton::clicked, this, &PropertyWidgetBase::browseFile);
				connect(edit, &QLineEdit::editingFinished, this, &PropertyWidgetBase::setString);
			}
			break;
		case Property::VEC3:
			{
				QWidget* widget = new QWidget();
				QDoubleSpinBox* edit_x = new QDoubleSpinBox(widget);
				QDoubleSpinBox* edit_y = new QDoubleSpinBox(widget);
				QDoubleSpinBox* edit_z = new QDoubleSpinBox(widget);
				m_form_layout->setWidget(m_properties.size() - 1, QFormLayout::FieldRole, widget);
				QHBoxLayout* horizontal_layout = new QHBoxLayout(widget);
				horizontal_layout->addWidget(edit_x);
				horizontal_layout->addWidget(edit_y);
				horizontal_layout->addWidget(edit_z);
				horizontal_layout->setContentsMargins(0, 0, 0, 0);
				prop.m_widget = widget;
				edit_x->setProperty("general_widget_property", (int)(m_properties.size() - 1));
				edit_y->setProperty("general_widget_property", (int)(m_properties.size() - 1));
				edit_z->setProperty("general_widget_property", (int)(m_properties.size() - 1));
				connect(edit_x, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyWidgetBase::setVec3);
				connect(edit_y, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyWidgetBase::setVec3);
				connect(edit_z, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyWidgetBase::setVec3);
			}
			break;
		case Property::STRING:
			{
				QLineEdit* edit = new QLineEdit(this);
				m_form_layout->setWidget(m_properties.size() - 1, QFormLayout::FieldRole, edit);
				prop.m_widget = edit;
				edit->setProperty("general_widget_property", (int)(m_properties.size() - 1));
				connect(edit, &QLineEdit::editingFinished, this, &PropertyWidgetBase::setString);
			}
			break;
		case Property::DECIMAL:
			{
				QDoubleSpinBox* edit = new QDoubleSpinBox(this);
				m_form_layout->setWidget(m_properties.size() - 1, QFormLayout::FieldRole, edit);
				prop.m_widget = edit;
				edit->setProperty("general_widget_property", (int)(m_properties.size() - 1));
				connect(edit, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyWidgetBase::setDecimal);
			}
			break;
		case Property::BOOL:
			{
				QCheckBox* edit = new QCheckBox(this);
				m_form_layout->setWidget(m_properties.size() - 1, QFormLayout::FieldRole, edit);
				prop.m_widget = edit;
				edit->setProperty("general_widget_property", (int)(m_properties.size() - 1));
				connect(edit, &QCheckBox::stateChanged, this, &PropertyWidgetBase::setBool);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


void PropertyWidgetBase::setBool()
{
	QCheckBox* edit = qobject_cast<QCheckBox*>(QObject::sender());
	int property_index = edit->property("general_widget_property").toInt();
	bool b = edit->isChecked();
	m_client->setComponentProperty(m_component_type.c_str(), m_properties[property_index].m_name.c_str(), &b, sizeof(b));
}


void PropertyWidgetBase::setVec3()
{
	QDoubleSpinBox* edit = qobject_cast<QDoubleSpinBox*>(QObject::sender());
	int property_index = edit->property("general_widget_property").toInt();

	Lux::Vec3 v;
	v.x = (float)qobject_cast<QDoubleSpinBox*>(m_properties[property_index].m_widget->children()[0])->value();
	v.y = (float)qobject_cast<QDoubleSpinBox*>(m_properties[property_index].m_widget->children()[1])->value();
	v.z = (float)qobject_cast<QDoubleSpinBox*>(m_properties[property_index].m_widget->children()[2])->value();
	
	m_client->setComponentProperty(m_component_type.c_str(), m_properties[property_index].m_name.c_str(), &v, sizeof(v));
}


void PropertyWidgetBase::setDecimal()
{
	QDoubleSpinBox* edit = qobject_cast<QDoubleSpinBox*>(QObject::sender());
	int property_index = edit->property("general_widget_property").toInt();
	float f = (float)edit->value();
	m_client->setComponentProperty(m_component_type.c_str(), m_properties[property_index].m_name.c_str(), &f, sizeof(f));
}


void PropertyWidgetBase::setString()
{
	QLineEdit* edit = qobject_cast<QLineEdit*>(QObject::sender());
	int property_index = edit->property("general_widget_property").toInt();
	m_client->setComponentProperty(m_component_type.c_str(), m_properties[property_index].m_name.c_str(), edit->text().toLocal8Bit().data(), edit->text().size());
}


void PropertyWidgetBase::browseFile()
{
	int property_index = qobject_cast<QPushButton*>(QObject::sender())->property("general_widget_property").toInt();
	QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), m_properties[property_index].m_file_type.c_str());
	int len = (int)strlen(m_client->getBasePath());
	QLineEdit* edit = (qobject_cast<QLineEdit*>(m_properties[property_index].m_widget));
	if (strncmp(str.toLocal8Bit().data(), m_client->getBasePath(), len) == 0)
	{
		edit->setText(str.toLocal8Bit().data() + len);
	}
	else
	{
		edit->setText(str);
	}
	m_client->setComponentProperty(m_component_type.c_str(), m_properties[property_index].m_name.c_str(), edit->text().toLocal8Bit().data(), edit->text().size());
}


void PropertyWidgetBase::onEntityProperties(Lux::PropertyListEvent& event)
{
	if (event.type_hash == crc32(m_component_type.c_str()))
	{
		for (int i = 0; i < event.properties.size(); ++i)
		{
			for (int j = 0; j < m_properties.size(); ++j)
			{
				if (event.properties[i].name_hash == m_properties[j].m_name_hash)
				{
					if (event.properties[i].data_size > 0)
					{
						switch(m_properties[j].m_type)
						{
							case Property::STRING:
							case Property::FILE:
								(qobject_cast<QLineEdit*>(m_properties[j].m_widget))->setText((char*)event.properties[i].data);
								break;
							case Property::DECIMAL:
								{
									float f;
									memcpy(&f, event.properties[i].data, sizeof(f));
									(qobject_cast<QDoubleSpinBox*>(m_properties[j].m_widget))->setValue(f);
								}
								break;
							case Property::VEC3:
								{
									Lux::Vec3 v;
									memcpy(&v, event.properties[i].data, sizeof(v));
									qobject_cast<QDoubleSpinBox*>(m_properties[j].m_widget->children()[0])->setValue(v.x);
									qobject_cast<QDoubleSpinBox*>(m_properties[j].m_widget->children()[1])->setValue(v.y);
									qobject_cast<QDoubleSpinBox*>(m_properties[j].m_widget->children()[2])->setValue(v.z);
								}
								break;
							case Property::BOOL:
								{
									bool b;
									memcpy(&b, event.properties[i].data, sizeof(b));
									(qobject_cast<QCheckBox*>(m_properties[j].m_widget))->setChecked(b);
								}
								break;
							default:
								ASSERT(false);
								break;
						}
					}
					break;
				}
			}
		}
	}
}

