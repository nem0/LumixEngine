#include "property_view.h"
#include "ui_property_view.h"
#include <qpushbutton.h>
#include "core/crc32.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "property_widget_base.h"


PropertyView::PropertyView(QWidget* parent) :
	QDockWidget(parent),
	m_ui(new Ui::PropertyView)
{
	m_ui->setupUi(this);
}


void PropertyView::setEditorClient(Lux::EditorClient& client)
{
	m_client = &client;
	m_client->getEventManager().addListener(Lux::ServerMessageType::PROPERTY_LIST).bind<PropertyView, &PropertyView::onPropertyList>(this);
	m_client->getEventManager().addListener(Lux::ServerMessageType::ENTITY_SELECTED).bind<PropertyView, &PropertyView::onEntitySelected>(this);
}


void PropertyView::onPropertyList(Lux::Event& event)
{
	Lux::PropertyListEvent& e = static_cast<Lux::PropertyListEvent&>(event);
	for (int i = 0; i < m_component_uis.size(); ++i)
	{
		m_component_uis[i]->onEntityProperties(e);
	}
}


void PropertyView::onEntitySelected(Lux::Event& event)
{
	m_component_uis.clear();
	while (m_ui->components->count() > 0)
	{
		m_ui->components->removeItem(0);
	}
	Lux::EntitySelectedEvent& e = static_cast<Lux::EntitySelectedEvent&>(event);
	//m_selected_entity = e.index;
	for (int i = 0; i < e.components.size(); ++i)
	{
		m_client->requestProperties(e.components[i]);
		/// TODO factory or something
		PropertyWidgetBase* widget = NULL;
		if (e.components[i] == crc32("box_rigid_actor"))
		{
			widget = new PropertyWidgetBase;
			widget->addProperty("size", "Size", PropertyWidgetBase::Property::VEC3, NULL);
			widget->addProperty("dynamic", "Is dynamic", PropertyWidgetBase::Property::BOOL, NULL);
			widget->setTitle("Physics Box");
			widget->setComponentType("box_rigid_actor");
		}
		else if (e.components[i] == crc32("renderable"))
		{
			widget = new PropertyWidgetBase;
			widget->addProperty("source", "Source", PropertyWidgetBase::Property::FILE, "models (*.msh)");
			widget->setTitle("Renderable");
			widget->setComponentType("renderable");
		}
		else if (e.components[i] == crc32("animable"))
		{
			widget = new PropertyWidgetBase;
			widget->setTitle("Animable");
			widget->setComponentType("animable");
		}
		else if (e.components[i] == crc32("script"))
		{
			widget = new PropertyWidgetBase;
			widget->addProperty("source", "Source", PropertyWidgetBase::Property::FILE, "scripts (*.cpp)");
			widget->setTitle("Script");
			widget->setComponentType("script");
		}
		else if (e.components[i] == crc32("light"))
		{
			widget = new PropertyWidgetBase;
			widget->setTitle("Light");
			widget->setComponentType("light");
		}
		else if (e.components[i] == crc32("camera"))
		{
			widget = new PropertyWidgetBase;
			widget->addProperty("slot", "Slot", PropertyWidgetBase::Property::STRING, NULL);
			widget->addProperty("near", "Near", PropertyWidgetBase::Property::DECIMAL, NULL);
			widget->addProperty("far", "Far", PropertyWidgetBase::Property::DECIMAL, NULL);
			widget->addProperty("fov", "Field of view", PropertyWidgetBase::Property::DECIMAL, NULL);
			widget->setTitle("Camera");
			widget->setComponentType("camera");
		}
		else if (e.components[i] == crc32("terrain"))
		{
			widget = new PropertyWidgetBase;
			widget->addProperty("heightmap", "Heightmap", PropertyWidgetBase::Property::FILE, "TGA image (*.tga)");
			widget->addProperty("material", "Material", PropertyWidgetBase::Property::FILE, "material (*.mat)");
			widget->setTitle("Terrain");
			widget->setComponentType("terrain");
		}
		else
		{
			ASSERT(false);
		}
		widget->setEditorClient(*m_client);
		m_ui->components->addItem(widget, widget->getTitle());
		m_component_uis.push(widget);
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
	const char* map[] =
	{
		"Script", "script",
		"Renderable", "renderable",
		"Point Light", "light",
		"Animable", "animable",
		"Camera", "camera",
		"Physics Box", "box_rigid_actor",
		"Terrain", "terrain"
	};

	for(int i = 0; i < sizeof(map) / sizeof(map[0]); i += 2)
	{
		if(strcmp(c, map[i]) == 0)
		{
			m_client->addComponent(crc32(map[i+1]));
			return;
		}
	}
	ASSERT(false); // unknown component type
}
