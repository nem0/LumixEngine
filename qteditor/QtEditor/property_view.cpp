#include "property_view.h"
#include "ui_property_view.h"
#include <qpushbutton.h>
#include "core/crc32.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "propertywidgets/animable_widget.h"
#include "propertywidgets/camerawidget.h"
#include "propertywidgets/lightwidget.h"
#include "propertywidgets/physics_box_widget.h"
#include "propertywidgets/renderable_widget.h"
#include "propertywidgets/script_widget.h"
#include "propertywidgets/terrain_widget.h"


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
			widget = new PhysicsBoxWidget;
		}
		else if (e.components[i] == crc32("renderable"))
		{
			widget = new RenderableWidget;
		}
		else if (e.components[i] == crc32("animable"))
		{
			widget = new AnimableWidget;
		}
		else if (e.components[i] == crc32("script"))
		{
			widget = new ScriptWidget;
		}
		else if (e.components[i] == crc32("light"))
		{
			widget = new LightWidget;
		}
		else if (e.components[i] == crc32("camera"))
		{
			widget = new CameraWidget;
		}
		else if (e.components[i] == crc32("terrain"))
		{
			widget = new TerrainWidget;
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
