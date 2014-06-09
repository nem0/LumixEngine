#include "property_view.h"
#include "ui_property_view.h"
#include <qpushbutton.h>
#include "core/crc32.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "property_widget_base.h"


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
		PropertyWidgetBase* widget = NULL;
		for(int j = 0; j < sizeof(component_map) / sizeof(component_map[0]); j += 2)
		{
			if(e.components[i] == crc32(component_map[j + 1]))
			{
				widget = new PropertyWidgetBase(component_map[j + 1], component_map[j]);
				break;
			}
		}
		/// TODO refactor
		if (e.components[i] == crc32("box_rigid_actor"))
		{
			widget->addProperty("size", "Size", PropertyWidgetBase::Property::VEC3, NULL);
			widget->addProperty("dynamic", "Is dynamic", PropertyWidgetBase::Property::BOOL, NULL);
		}
		else if (e.components[i] == crc32("renderable"))
		{
			widget->addProperty("source", "Source", PropertyWidgetBase::Property::FILE, "models (*.msh)");
		}
		else if (e.components[i] == crc32("script"))
		{
			widget->addProperty("source", "Source", PropertyWidgetBase::Property::FILE, "scripts (*.cpp)");
		}
		else if (e.components[i] == crc32("camera"))
		{
			widget->addProperty("slot", "Slot", PropertyWidgetBase::Property::STRING, NULL);
			widget->addProperty("near", "Near", PropertyWidgetBase::Property::DECIMAL, NULL);
			widget->addProperty("far", "Far", PropertyWidgetBase::Property::DECIMAL, NULL);
			widget->addProperty("fov", "Field of view", PropertyWidgetBase::Property::DECIMAL, NULL);
		}
		else if (e.components[i] == crc32("terrain"))
		{
			widget->addProperty("heightmap", "Heightmap", PropertyWidgetBase::Property::FILE, "TGA image (*.tga)");
			widget->addProperty("material", "Material", PropertyWidgetBase::Property::FILE, "material (*.mat)");
		}
		else if (e.components[i] == crc32("physical_controller") || e.components[i] == crc32("mesh_rigid_actor"))
		{
		}
		else if (e.components[i] == crc32("physical_heightfield"))
		{
			widget->addProperty("heightmap", "Heightmap", PropertyWidgetBase::Property::FILE, "TGA image (*.tga)");
		}
		else if (e.components[i] == crc32("light"))
		{
		}
		else
		{
			ASSERT(false);
		}
		widget->setEditorClient(*m_client);
		m_ui->components->addItem(widget, widget->getTitle());
		m_component_uis.push(widget);
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
