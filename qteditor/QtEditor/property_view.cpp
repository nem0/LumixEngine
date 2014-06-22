#include "property_view.h"
#include "ui_property_view.h"
#include <qcheckbox.h>
#include <qdesktopservices.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QMimeData>
#include <qlineedit.h>
#include <qpushbutton.h>
#include "core/crc32.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "scripts/scriptcompiler.h"


static const char* component_map[] =
{
	"Animable", "animable",
	"Camera", "camera",
	"Physics Box", "box_rigid_actor",
	"Physics Controller", "physical_controller",
	"Physics Mesh", "mesh_rigid_actor",
	"Physics Heightfield", "physical_heightfield",
	"Point Light", "light",
	"Renderable", "renderable",
	"Script", "script",
	"Terrain", "terrain"
};


PropertyView::PropertyView(QWidget* parent) :
	QDockWidget(parent),
	m_ui(new Ui::PropertyView)
{
	m_ui->setupUi(this);

	QStringList component_list;
	for(int j = 0; j < sizeof(component_map) / sizeof(component_map[0]); j += 2)
	{
		component_list << component_map[j];
	}
	
	m_ui->componentTypeCombo->insertItems(0, component_list);
}


Lumix::EditorClient* PropertyView::getEditorClient()
{
	return m_client;
}


void PropertyView::setEditorClient(Lumix::EditorClient& client)
{
	m_client = &client;
	m_client->getEventManager().addListener(Lumix::ServerMessageType::PROPERTY_LIST).bind<PropertyView, &PropertyView::onPropertyList>(this);
	m_client->getEventManager().addListener(Lumix::ServerMessageType::ENTITY_SELECTED).bind<PropertyView, &PropertyView::onEntitySelected>(this);
}


void PropertyView::on_checkboxStateChanged()
{
	QCheckBox* cb = qobject_cast<QCheckBox*>(QObject::sender());
	int i = cb->property("cpp_prop").toInt();
	bool b = cb->isChecked();
	m_client->setComponentProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), &b, sizeof(b)); 
}


void PropertyView::on_vec3ValueChanged()
{
	QDoubleSpinBox* sb = qobject_cast<QDoubleSpinBox*>(QObject::sender());
	int i = sb->property("cpp_prop").toInt();
	Lumix::Vec3 v;
	v.x = (float)qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item->child(0), 1))->value();
	v.y = (float)qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item->child(1), 1))->value();
	v.z = (float)qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item->child(2), 1))->value();
	m_client->setComponentProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), &v, sizeof(v)); 
}

void PropertyView::on_doubleSpinBoxValueChanged()
{
	QDoubleSpinBox* sb = qobject_cast<QDoubleSpinBox*>(QObject::sender());
	int i = sb->property("cpp_prop").toInt();
	float f = (float)qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1))->value();
	m_client->setComponentProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), &f, sizeof(f)); 
}

void PropertyView::on_lineEditEditingFinished()
{
	QLineEdit* edit = qobject_cast<QLineEdit*>(QObject::sender());
	int i = edit->property("cpp_prop").toInt();
	QByteArray byte_array = edit->text().toLatin1();
	const char* text = byte_array.data();
	m_client->setComponentProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), text, byte_array.size()); 
}

void PropertyView::on_browseFilesClicked()
{
	QPushButton* button = qobject_cast<QPushButton*>(QObject::sender());
	int i = button->property("cpp_prop").toInt();
	QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), m_properties[i]->m_file_type.c_str());
	int len = (int)strlen(m_client->getBasePath());
	
	QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1)->children()[0]);
	if (strncmp(str.toLocal8Bit().data(), m_client->getBasePath(), len) == 0)
	{
		edit->setText(str.toLocal8Bit().data() + len);
	}
	else
	{
		edit->setText(str);
	}
	m_client->setComponentProperty(m_properties[i]->m_component_name.c_str(), m_properties[i]->m_name.c_str(), edit->text().toLocal8Bit().data(), edit->text().size());
}

void PropertyView::onPropertyValue(Property* property, void* data, int32_t)
{
	if(property->m_component_name == "script" && property->m_name == "source")
	{
		setScriptStatus(m_compiler->getStatus(static_cast<char*>(data)));
	}

	switch(property->m_type)
	{
		case Property::VEC3:
			{
				Lumix::Vec3 v = *(Lumix::Vec3*)data;
				QString text;
				text.sprintf("[%f; %f; %f]", v.x, v.y, v.z);
				property->m_tree_item->setText(1, text);
				QTreeWidgetItem* item = property->m_tree_item->child(0);
				qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(item, 1))->setValue(v.x);
				item = property->m_tree_item->child(1);
				qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(item, 1))->setValue(v.y);
				item = property->m_tree_item->child(2);
				qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(item, 1))->setValue(v.z);
			}
			break;
		case Property::BOOL:
			{
				bool b = *(bool*)data; 
				QCheckBox* cb = qobject_cast<QCheckBox*>(m_ui->propertyList->itemWidget(property->m_tree_item, 1));
				cb->setChecked(b);
			}
			break;
		case Property::FILE:
			{
				QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(property->m_tree_item, 1)->children()[0]);
				edit->setText((char*)data);
			}
			break;
		case Property::STRING:
			{
				QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(property->m_tree_item, 1));
				edit->setText((char*)data);
			}
			break;
		case Property::DECIMAL:
			{
				QDoubleSpinBox* edit = qobject_cast<QDoubleSpinBox*>(m_ui->propertyList->itemWidget(property->m_tree_item, 1));
				float f = *(float*)data;
				edit->setValue(f);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


void PropertyView::onPropertyList(Lumix::Event& event)
{
	Lumix::PropertyListEvent& e = static_cast<Lumix::PropertyListEvent&>(event);
	for(int i = 0; i < e.properties.size(); ++i)
	{
		for(int j = 0; j < m_properties.size(); ++j)
		{
			if(e.type_hash == m_properties[j]->m_component && e.properties[i].name_hash == m_properties[j]->m_name_hash)
			{
				onPropertyValue(m_properties[j], e.properties[i].data, e.properties[i].data_size);
				break;
			}
		}
	}
}


class FileEdit : public QLineEdit
{
	public:
		FileEdit(QWidget* parent, PropertyView* property_view)
			: QLineEdit(parent)
			, m_property_view(property_view)
		{
			setAcceptDrops(true);
		}

		virtual void dragEnterEvent(QDragEnterEvent* event) override
		{
			if (event->mimeData()->hasUrls())
			{
				event->acceptProposedAction();
			}
		}

		virtual void dropEvent(QDropEvent* event)
		{
			const QList<QUrl>& list = event->mimeData()->urls();
			if(!list.empty())
			{
				QString file = list[0].toLocalFile();
				if(file.toLower().startsWith(m_property_view->getEditorClient()->getBasePath()))
				{
					file.remove(0, QString(m_property_view->getEditorClient()->getBasePath()).length());
				}
				if(file.startsWith("/"))
				{
					file.remove(0, 1);
				}
				setText(file);
				emit editingFinished();
			}
		}

	private:
		PropertyView* m_property_view;
};


void PropertyView::addProperty(const char* component, const char* name, const char* label, Property::Type type, const char* file_type)
{
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << label);
	m_ui->propertyList->topLevelItem(0)->insertChild(0, item);
	Property* prop = new Property();
	prop->m_component_name = component;
	prop->m_component = crc32(component);
	prop->m_name = name;
	prop->m_file_type = file_type;
	prop->m_name_hash = crc32(name);
	prop->m_type = type;
	prop->m_tree_item = item;
	m_properties.push(prop);
	switch(type)
	{
		case Property::BOOL:
			{
				QCheckBox* checkbox = new QCheckBox();
				m_ui->propertyList->setItemWidget(item, 1, checkbox);
				connect(checkbox, &QCheckBox::stateChanged, this, &PropertyView::on_checkboxStateChanged);
				checkbox->setProperty("cpp_prop", (int)(m_properties.size() - 1));
			}
			break;
		case Property::VEC3:
			{
				item->setText(1, "");
				QDoubleSpinBox* sb = new QDoubleSpinBox();
				item->insertChild(0, new QTreeWidgetItem(QStringList() << "x"));
				m_ui->propertyList->setItemWidget(item->child(0), 1, sb);
				sb->setProperty("cpp_prop", (int)(m_properties.size() - 1));
				connect(sb, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyView::on_vec3ValueChanged);

				sb = new QDoubleSpinBox();
				item->insertChild(1, new QTreeWidgetItem(QStringList() << "y"));
				m_ui->propertyList->setItemWidget(item->child(1), 1, sb);
				sb->setProperty("cpp_prop", (int)(m_properties.size() - 1));
				connect(sb, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyView::on_vec3ValueChanged);

				sb = new QDoubleSpinBox();
				item->insertChild(2, new QTreeWidgetItem(QStringList() << "z"));
				m_ui->propertyList->setItemWidget(item->child(2), 1, sb);
				sb->setProperty("cpp_prop", (int)(m_properties.size() - 1));
				connect(sb, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyView::on_vec3ValueChanged);
			}
			break;
		case Property::FILE:
			{
				QWidget* widget = new QWidget();
				QLineEdit* edit = new FileEdit(widget, this);
				edit->setProperty("cpp_prop", (int)(m_properties.size() - 1)); 
				QHBoxLayout* layout = new QHBoxLayout(widget);
				layout->addWidget(edit);
				layout->setContentsMargins(0, 0, 0, 0);
				QPushButton* button = new QPushButton("...", widget);
				layout->addWidget(button);
				button->setProperty("cpp_prop", (int)(m_properties.size() - 1)); 
				connect(button, &QPushButton::clicked, this, &PropertyView::on_browseFilesClicked);
				m_ui->propertyList->setItemWidget(item, 1, widget);
				connect(edit, &QLineEdit::editingFinished, this, &PropertyView::on_lineEditEditingFinished);
			}
			break;
		case Property::STRING:
			{
				QLineEdit* edit = new QLineEdit();
				edit->setProperty("cpp_prop", (int)(m_properties.size() - 1)); 
				m_ui->propertyList->setItemWidget(item, 1, edit);
				connect(edit, &QLineEdit::editingFinished, this, &PropertyView::on_lineEditEditingFinished);
			}
			break;
		case Property::DECIMAL:
			{
				QDoubleSpinBox* edit = new QDoubleSpinBox();
				edit->setProperty("cpp_prop", (int)(m_properties.size() - 1)); 
				m_ui->propertyList->setItemWidget(item, 1, edit);
				connect(edit, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, this, &PropertyView::on_doubleSpinBoxValueChanged);
			}
			break;
		default:
			ASSERT(false);
			break;
	}
}


void PropertyView::setScriptCompiler(ScriptCompiler* compiler)
{
	m_compiler = compiler;
	if(m_compiler)
	{
		m_compiler->onCompile().bind<PropertyView, &PropertyView::onScriptCompiled>(this);
	}
}


void PropertyView::clear()
{
	m_ui->propertyList->clear();
	for(int i = 0; i < m_properties.size(); ++i)
	{
		delete m_properties[i];
	}
	m_properties.clear();
}


void PropertyView::setScriptStatus(uint32_t status)
{
	for(int i = 0; i < m_ui->propertyList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem* item = m_ui->propertyList->topLevelItem(i);
		if(item->text(0) == "Script")
		{
			for(int j = 0; j < item->childCount(); ++j)
			{
				if(item->child(j)->text(0) == "Status")
				{
					switch(status)
					{
						case ScriptCompiler::SUCCESS:
							item->child(j)->setText(1, "Success");
							break;
						case ScriptCompiler::NOT_COMPILED:
							item->child(j)->setText(1, "Not compiled");
							break;
						case ScriptCompiler::UNKNOWN:
							item->child(j)->setText(1, "Unknown");
							break;
						case ScriptCompiler::FAILURE:
							item->child(j)->setText(1, "Failure");
							break;
						default:
							ASSERT(false);
							break;
					}
					
					return;
				}
			}
		}
	}
}


void PropertyView::onScriptCompiled(const Lumix::Path& path, uint32_t status)
{
	QString script_path(path);
	script_path = script_path.toLower();
	if(script_path.startsWith(m_client->getBasePath()))
	{
		script_path.remove(0, strlen(m_client->getBasePath()) + 1);
	}
	setScriptStatus(status == 0 ? ScriptCompiler::SUCCESS : ScriptCompiler::FAILURE);
}


void PropertyView::on_compileScriptClicked()
{
	for(int i = 0; i < m_properties.size(); ++i)
	{
		if(m_properties[i]->m_component_name == "script" && m_properties[i]->m_name == "source")
		{
			QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1)->children()[0]);
			m_compiler->compile(edit->text().toLatin1().data());
			break;
		}
	}
}


void PropertyView::on_editScriptClicked()
{
	for(int i = 0; i < m_properties.size(); ++i)
	{
		if(m_properties[i]->m_name == "source")
		{
			QLineEdit* edit = qobject_cast<QLineEdit*>(m_ui->propertyList->itemWidget(m_properties[i]->m_tree_item, 1)->children()[0]);
			QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
			break;
		}
	}
}


void PropertyView::addScriptCustomProperties()
{
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	m_ui->propertyList->topLevelItem(0)->insertChild(0, tools_item);
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* compile_button = new QPushButton("Compile", widget);
	QPushButton* edit_button = new QPushButton("Edit", widget);
	layout->addWidget(compile_button);
	layout->addWidget(edit_button);
	m_ui->propertyList->setItemWidget(tools_item, 1, widget);
	connect(compile_button, &QPushButton::clicked, this, &PropertyView::on_compileScriptClicked);
	connect(edit_button, &QPushButton::clicked, this, &PropertyView::on_editScriptClicked);

	QTreeWidgetItem* status_item = new QTreeWidgetItem(QStringList() << "Status");
	m_ui->propertyList->topLevelItem(0)->insertChild(0, status_item);
	status_item->setText(1, "Unknown");
}


void PropertyView::onEntitySelected(Lumix::Event& event)
{
	Lumix::EntitySelectedEvent& e = static_cast<Lumix::EntitySelectedEvent&>(event);
	clear();
	for (int i = 0; i < e.components.size(); ++i)
	{
		for(int j = 0; j < sizeof(component_map) / sizeof(component_map[0]); j += 2)
		{
			if(e.components[i] == crc32(component_map[j + 1]))
			{
				m_ui->propertyList->insertTopLevelItem(0, new QTreeWidgetItem());
				m_ui->propertyList->topLevelItem(0)->setText(0, component_map[j]);
				break;
			}
		}
		/// TODO refactor
		if (e.components[i] == crc32("box_rigid_actor"))
		{
			addProperty("box_rigid_actor", "size", "Size", Property::VEC3, NULL);
			addProperty("box_rigid_actor", "dynamic", "Is dynamic", Property::BOOL, NULL);
		}
		else if (e.components[i] == crc32("renderable"))
		{
			addProperty("renderable", "source", "Source", Property::FILE, "models (*.msh)");
		}
		else if (e.components[i] == crc32("script"))
		{
			addProperty("script", "source", "Source", Property::FILE, "scripts (*.cpp)");
			addScriptCustomProperties();
		}
		else if (e.components[i] == crc32("camera"))
		{
			addProperty("camera", "slot", "Slot", Property::STRING, NULL);
			addProperty("camera", "near", "Near", Property::DECIMAL, NULL);
			addProperty("camera", "far", "Far", Property::DECIMAL, NULL);
			addProperty("camera", "fov", "Field of view", Property::DECIMAL, NULL);
		}
		else if (e.components[i] == crc32("terrain"))
		{
			addProperty("terrain", "heightmap", "Heightmap", Property::FILE, "TGA image (*.tga)");
			addProperty("terrain", "material", "Material", Property::FILE, "material (*.mat)");
		}
		else if (e.components[i] == crc32("physical_controller") || e.components[i] == crc32("mesh_rigid_actor"))
		{
		}
		else if (e.components[i] == crc32("physical_heightfield"))
		{
			addProperty("physical_heightfield", "heightmap", "Heightmap", Property::FILE, "TGA image (*.tga)");
		}
		else if (e.components[i] == crc32("light"))
		{
		}
		else if (e.components[i] == crc32("animable"))
		{
			addProperty("animable", "preview", "Preview animation", Property::FILE, "Animation (*.ani)");
		}
		else
		{
			ASSERT(false);
		}
		m_ui->propertyList->expandAll();
		m_client->requestProperties(e.components[i]);
	}
}


PropertyView::~PropertyView()
{
	delete m_ui;
}

void PropertyView::on_addComponentButton_clicked()
{
	QByteArray s = m_ui->componentTypeCombo->currentText().toLocal8Bit();
	const char* c = s.data();
	/// TODO

	for(int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if(strcmp(c, component_map[i]) == 0)
		{
			m_client->addComponent(crc32(component_map[i+1]));
			return;
		}
	}
	ASSERT(false); // unknown component type
}
