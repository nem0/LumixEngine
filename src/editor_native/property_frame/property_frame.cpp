#include "editor_native/property_frame/property_frame.h"
#include <cstdio>
#include "core/crc32.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"
#include "editor_native/property_frame/box_rigid_actor_ui.h"
#include "editor_native/property_frame/renderable_ui.h"
#include "gui/button.h"
#include "gui/gui.h"
#include "gui/text_box.h"


void PropertyFrame::createComponentClicked(Lux::UI::Block& block, void*)
{
	m_type_list_popup->getGui()->focus(m_type_list_popup);
	m_type_list_popup->show();
}


void PropertyFrame::newComponentClick(Lux::UI::Block& block, void*)
{
	m_main_frame->getEditorClient()->addComponent((uint32_t)block.getTag());
	block.getParent()->hide();
}


void PropertyFrame::positionChanged(Lux::UI::Block& block, void*)
{
	Lux::Vec3 v;
	sscanf_s(m_pos_x_box->getText().c_str(), "%f", &v.x);
	sscanf_s(m_pos_y_box->getText().c_str(), "%f", &v.y);
	sscanf_s(m_pos_z_box->getText().c_str(), "%f", &v.z);
	ASSERT(false); // TODO
}


void PropertyFrame::onEntitySelected(void* user_data, Lux::Event& evt)
{
	PropertyFrame* that = static_cast<PropertyFrame*>(user_data);
	while(that->m_component_container->getChildCount() > 0)
	{
		that->m_component_container->getChild(0)->destroy();
	}
	that->m_component_uis.clear();
	Lux::EntitySelectedEvent& e = static_cast<Lux::EntitySelectedEvent&>(evt);
	for(int i = 0; i < e.components.size(); ++i)
	{
		IComponentUI* ui = NULL;
		that->m_main_frame->getEditorClient()->requestProperties(e.components[i]);
		if(e.components[i] == crc32("box_rigid_actor"))
		{
			ui = new BoxRigidActorUI(that->m_component_container, *that->m_main_frame->getEditorClient());
		}
		else if(e.components[i] == crc32("renderable"))
		{
			ui = new RenderableUI(*that, that->m_component_container, *that->m_main_frame->getEditorClient());
		}
		else
		{
			ASSERT(false);
		}
		that->m_component_uis.push_back(ui);
	}
	that->layout();
	float h = 0;
	for(int i = 0; i < that->m_component_container->getChildCount(); ++i)
	{
		Lux::UI::Block::Area& area = that->m_component_container->getChild(i)->getLocalArea();
		area.top += h;
		area.bottom += h;
		that->m_component_container->getChild(i)->setArea(area);
		h += area.bottom - area.top;
	}
	that->layout();
}


void PropertyFrame::onEntityProperties(void* user_data, Lux::Event& evt)
{
	PropertyFrame* that = static_cast<PropertyFrame*>(user_data);
	Lux::PropertyListEvent& e = static_cast<Lux::PropertyListEvent&>(evt);
	for(int i = 0; i < that->m_component_uis.size(); ++i)
	{
		that->m_component_uis[i]->onEntityProperties(e);
	}
}


void PropertyFrame::onEntityPosition(void* user_data, Lux::Event& evt)
{
	PropertyFrame* that = static_cast<PropertyFrame*>(user_data);
	Lux::EntityPositionEvent& e = static_cast<Lux::EntityPositionEvent&>(evt);
	char tmp[30];
	sprintf_s(tmp, "%f", e.x);
	that->m_pos_x_box->setText(tmp);
	sprintf_s(tmp, "%f", e.y);
	that->m_pos_y_box->setText(tmp);
	sprintf_s(tmp, "%f", e.z);
	that->m_pos_z_box->setText(tmp);
}


PropertyFrame::PropertyFrame(MainFrame& main_frame)
	: Block(main_frame.getGui(), &main_frame.getUI(), "_box")
{
	m_main_frame = &main_frame;

	setArea(0, 0, 0, 19, 0.4f, 0, 1, 0);
	m_create_component_button = new Lux::UI::Button("create component", main_frame.getGui(), this);
	m_create_component_button->setArea(0, 5, 1, -25, 1, -5, 1, -5);
	main_frame.getGui().getCallback("PropertyFrame_createComponentClicked").bind<PropertyFrame, &PropertyFrame::createComponentClicked>(this);
	m_create_component_button->registerEventHandler("click", "PropertyFrame_createComponentClicked");
	
	Lux::UI::Block* label = new Block(main_frame.getGui(), this, "_text");
	label->setArea(0, 2, 0, 2, 0, 50, 0, 20);
	label->setBlockText("Position");

	Lux::UI::Block* positions = new Block(main_frame.getGui(), this, NULL);
	positions->setArea(0, 50, 0, 2, 1, -2, 0, 20);

	main_frame.getGui().getCallback("PropertyFrame_positionChanged").bind<PropertyFrame, &PropertyFrame::positionChanged>(this);
	m_pos_x_box = new Lux::UI::TextBox("0", main_frame.getGui(), positions);
	m_pos_x_box->setArea(0, 1, 0, 0, 0.33f, 0, 0, 20);
	m_pos_x_box->setOnTextAccepted("PropertyFrame_positionChanged");
	m_pos_y_box = new Lux::UI::TextBox("0", main_frame.getGui(), positions);
	m_pos_y_box->setArea(0.33f, 1, 0, 0, 0.66f, 0, 0, 20);
	m_pos_y_box->setOnTextAccepted("PropertyFrame_positionChanged");
	m_pos_z_box = new Lux::UI::TextBox("0", main_frame.getGui(), positions);
	m_pos_z_box->setArea(0.66f, 1, 0, 0, 1, 0, 0, 20);
	m_pos_z_box->setOnTextAccepted("PropertyFrame_positionChanged");
	
	m_component_container = new Block(main_frame.getGui(), this, NULL);
	m_component_container->setArea(0, 0, 0, 24, 1, 0, 1, -30);
	
	m_type_list_popup = new Block(main_frame.getGui(), this, "_box");
	m_type_list_popup->setArea(0, 5, 0.5f, 0, 1, -5, 1, -25);
	m_type_list_popup->hide();
	m_type_list_popup->registerEventHandler("blur", "_hide");
	m_type_list_popup->setZIndex(1);

	main_frame.getGui().getCallback("PropertyFrame_newComponentClick").bind<PropertyFrame, &PropertyFrame::newComponentClick>(this);
	{
		Lux::UI::Block* item = new Block(main_frame.getGui(), m_type_list_popup, "_text_centered");
		item->setArea(0, 0, 0, 0, 1, 0, 0, 20);
		item->setBlockText("Renderable");
		item->registerEventHandler("click", "PropertyFrame_newComponentClick");
		item->setTag((void*)crc32("renderable"));
	}
	{
		Lux::UI::Block* item = new Block(main_frame.getGui(), m_type_list_popup, "_text_centered");
		item->setArea(0, 0, 0, 20, 1, 0, 0, 40);
		item->setBlockText("Box rigid actor");
		item->registerEventHandler("click", "PropertyFrame_newComponentClick");
		item->setTag((void*)crc32("box_rigid_actor"));
	}
	
	main_frame.getEditorClient()->getEventManager().registerListener(Lux::ServerMessageType::ENTITY_POSITION, this, &PropertyFrame::onEntityPosition);
	main_frame.getEditorClient()->getEventManager().registerListener(Lux::ServerMessageType::PROPERTY_LIST, this, &PropertyFrame::onEntityProperties);
	main_frame.getEditorClient()->getEventManager().registerListener(Lux::ServerMessageType::ENTITY_SELECTED, this, &PropertyFrame::onEntitySelected);
	
}