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
#include "propertywidgets/physics_controller_widget.h"
#include "propertywidgets/physics_mesh_widget.h"
#include "propertywidgets/renderable_widget.h"
#include "propertywidgets/script_widget.h"


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
		if (e.components[i] == crc32("physical_controller"))
		{
			widget = new PhysicsControllerWidget;
		}
		if (e.components[i] == crc32("mesh_rigid_actor"))
		{
			widget = new PhysicsMeshWidget;
		}
		else if (e.components[i] == crc32("box_rigid_actor"))
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
	if (strcmp(c, "Script") == 0)
	{
		m_client->addComponent(crc32("script"));
	}
	else if (strcmp(c, "Renderable") == 0)
	{
		m_client->addComponent(crc32("renderable"));
	}
	else if (strcmp(c, "Point Light") == 0)
	{
		m_client->addComponent(crc32("light"));
	}
	else if (strcmp(c, "Animable") == 0)
	{
		m_client->addComponent(crc32("animable"));
	}
	else if (strcmp(c, "Camera") == 0)
	{
		m_client->addComponent(crc32("camera"));
	}
	else if (strcmp(c, "Physics Box") == 0)
	{
		m_client->addComponent(crc32("box_rigid_actor"));
	}
	else if (strcmp(c, "Physics Controller") == 0)
	{
		m_client->addComponent(crc32("physical_controller"));
	}
	else if (strcmp(c, "Physics Mesh") == 0)
	{
		m_client->addComponent(crc32("mesh_rigid_actor"));
	}
}
