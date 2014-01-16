#include "editor_native/property_frame/property_frame.h"
#include <cstdio>
#include "core/crc32.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"
#include "editor_native/property_frame/animable_ui.h"
#include "editor_native/property_frame/box_rigid_actor_ui.h"
#include "editor_native/property_frame/renderable_ui.h"
#include "editor_native/property_frame/script_ui.h"
#include "gui/controls/button.h"
#include "gui/controls/text_box.h"
#include "gui/gui.h"


PropertyFrame::PropertyFrame(MainFrame& main_frame)
	: Dockable(main_frame.getGui(), NULL)
{
	main_frame.getDockable().dock(*this, Dockable::LEFT);
	m_main_frame = &main_frame;
	Lux::UI::Block* handle = LUX_NEW(Lux::UI::Block)(getGui(), getContent(), "_box");
	handle->setArea(0, 0, 0, 0, 1, 0, 0, 20);
	handle->onEvent("mouse_down").bind<Dockable, &Dockable::startDrag>(this);
	Lux::UI::Block* root = LUX_NEW(Lux::UI::Block)(getGui(), getContent(), "_box");
	root->setArea(0, 0, 0, 20, 1, 0, 1, 0);
	m_create_component_button = LUX_NEW(Lux::UI::Button)("create component", main_frame.getGui(), root);
	m_create_component_button->setArea(0, 5, 1, -25, 1, -5, 1, -5);
	m_create_component_button->onEvent("click").bind<PropertyFrame, &PropertyFrame::createComponentClicked>(this);
	
	Lux::UI::Block* label = LUX_NEW(Block)(main_frame.getGui(), root, "_text");
	label->setArea(0, 2, 0, 2, 0, 50, 0, 20);
	label->setBlockText("Position");

	Lux::UI::Block* positions = LUX_NEW(Block)(main_frame.getGui(), root, NULL);
	positions->setArea(0, 50, 0, 2, 1, -2, 0, 20);

	m_pos_x_box = LUX_NEW(Lux::UI::TextBox)("0", main_frame.getGui(), positions);
	m_pos_x_box->setArea(0, 1, 0, 0, 0.33f, 0, 0, 20);
	m_pos_x_box->onChange().bind<PropertyFrame, &PropertyFrame::positionChanged>(this);
	m_pos_y_box = LUX_NEW(Lux::UI::TextBox)("0", main_frame.getGui(), positions);
	m_pos_y_box->setArea(0.33f, 1, 0, 0, 0.66f, 0, 0, 20);
	m_pos_y_box->onChange().bind<PropertyFrame, &PropertyFrame::positionChanged>(this);
	m_pos_z_box = LUX_NEW(Lux::UI::TextBox)("0", main_frame.getGui(), positions);
	m_pos_z_box->setArea(0.66f, 1, 0, 0, 1, 0, 0, 20);
	m_pos_z_box->onChange().bind<PropertyFrame, &PropertyFrame::positionChanged>(this);
	
	m_component_container = LUX_NEW(Block)(main_frame.getGui(), root, NULL);
	m_component_container->setArea(0, 0, 0, 24, 1, 0, 1, -30);
	
	m_type_list_popup = LUX_NEW(Block)(main_frame.getGui(), root, "_box");
	m_type_list_popup->setArea(0, 5, 0.5f, 0, 1, -5, 1, -25);
	m_type_list_popup->hide();
	m_type_list_popup->onEvent("blur").bind<PropertyFrame, &PropertyFrame::blurComponentTypeList>(this);
	m_type_list_popup->setZIndex(1);

	{
		Lux::UI::Block* item = LUX_NEW(Block)(main_frame.getGui(), m_type_list_popup, "_text_centered");
		item->setArea(0, 0, 0, 0, 1, 0, 0, 20);
		item->setBlockText("Animable");
		item->onEvent("click").bind<PropertyFrame, &PropertyFrame::newComponentClick>(this);
		item->setTag((void*)crc32("animable"));
	}
	{
		Lux::UI::Block* item = LUX_NEW(Block)(main_frame.getGui(), m_type_list_popup, "_text_centered");
		item->setArea(0, 0, 0, 20, 1, 0, 0, 40);
		item->setBlockText("Box rigid actor");
		item->onEvent("click").bind<PropertyFrame, &PropertyFrame::newComponentClick>(this);
		item->setTag((void*)crc32("box_rigid_actor"));
	}
	{
		Lux::UI::Block* item = LUX_NEW(Block)(main_frame.getGui(), m_type_list_popup, "_text_centered");
		item->setArea(0, 0, 0, 40, 1, 0, 0, 60);
		item->setBlockText("Renderable");
		item->onEvent("click").bind<PropertyFrame, &PropertyFrame::newComponentClick>(this);
		item->setTag((void*)crc32("renderable"));
	}
	{
		Lux::UI::Block* item = LUX_NEW(Block)(main_frame.getGui(), m_type_list_popup, "_text_centered");
		item->setArea(0, 0, 0, 60, 1, 0, 0, 80);
		item->setBlockText("Script");
		item->onEvent("click").bind<PropertyFrame, &PropertyFrame::newComponentClick>(this);
		item->setTag((void*)crc32("script"));
	}
	
	main_frame.getEditorClient()->getEventManager().addListener(Lux::ServerMessageType::ENTITY_POSITION).bind<PropertyFrame, &PropertyFrame::onEntityPosition>(this);
	main_frame.getEditorClient()->getEventManager().addListener(Lux::ServerMessageType::PROPERTY_LIST).bind<PropertyFrame, &PropertyFrame::onEntityProperties>(this);
	main_frame.getEditorClient()->getEventManager().addListener(Lux::ServerMessageType::ENTITY_SELECTED).bind<PropertyFrame, &PropertyFrame::onEntitySelected>(this);
	
}


void PropertyFrame::blurComponentTypeList(Lux::UI::Block& block, void*)
{
	m_type_list_popup->hide();
}


void PropertyFrame::createComponentClicked(Lux::UI::Block& block, void*)
{
	m_type_list_popup->getGui().focus(m_type_list_popup);
	m_type_list_popup->show();
}


void PropertyFrame::newComponentClick(Lux::UI::Block& block, void*)
{
	m_main_frame->getEditorClient()->addComponent((uint32_t)block.getTag());
	block.getParent()->hide();
}


void PropertyFrame::positionChanged(Lux::UI::Block& block, void*)
{
	if(m_selected_entity != -1)
	{
		Lux::Vec3 v;
		sscanf_s(m_pos_x_box->getText().c_str(), "%f", &v.x);
		sscanf_s(m_pos_y_box->getText().c_str(), "%f", &v.y);
		sscanf_s(m_pos_z_box->getText().c_str(), "%f", &v.z);
		m_main_frame->getEditorClient()->setEntityPosition(m_selected_entity, v);
	}
}


void PropertyFrame::onEntitySelected(Lux::Event& evt)
{
	while(m_component_container->getChildCount() > 0)
	{
		m_component_container->getChild(0)->destroy();
	}
	m_component_uis.clear();
	Lux::EntitySelectedEvent& e = static_cast<Lux::EntitySelectedEvent&>(evt);
	m_selected_entity = e.index;
	for(int i = 0; i < e.components.size(); ++i)
	{
		IComponentUI* ui = NULL;
		m_main_frame->getEditorClient()->requestProperties(e.components[i]);
		if(e.components[i] == crc32("box_rigid_actor"))
		{
			ui = LUX_NEW(BoxRigidActorUI)(m_component_container, *m_main_frame->getEditorClient());
		}
		else if(e.components[i] == crc32("renderable"))
		{
			ui = LUX_NEW(RenderableUI)(*this, m_component_container, *m_main_frame->getEditorClient());
		}
		else if(e.components[i] == crc32("animable"))
		{
			ui = LUX_NEW(AnimableUI)(*this, m_component_container, *m_main_frame->getEditorClient());
		}
		else if(e.components[i] == crc32("script"))
		{
			ui = LUX_NEW(ScriptUI)(*this, m_component_container, *m_main_frame->getEditorClient());
		}
		else
		{
			ASSERT(false);
		}
		m_component_uis.push(ui);
	}
	layout();
	float h = 0;
	for(int i = 0; i < m_component_container->getChildCount(); ++i)
	{
		Lux::UI::Block::Area& area = m_component_container->getChild(i)->getLocalArea();
		area.top += h;
		area.bottom += h;
		m_component_container->getChild(i)->setArea(area);
		h += area.bottom - area.top;
	}
	layout();
}


void PropertyFrame::onEntityProperties(Lux::Event& evt)
{
	Lux::PropertyListEvent& e = static_cast<Lux::PropertyListEvent&>(evt);
	for(int i = 0; i < m_component_uis.size(); ++i)
	{
		m_component_uis[i]->onEntityProperties(e);
	}
}


void PropertyFrame::onEntityPosition(Lux::Event& evt)
{
	Lux::EntityPositionEvent& e = static_cast<Lux::EntityPositionEvent&>(evt);
	char tmp[30];
	sprintf_s(tmp, "%f", e.x);
	m_pos_x_box->setText(tmp);
	sprintf_s(tmp, "%f", e.y);
	m_pos_y_box->setText(tmp);
	sprintf_s(tmp, "%f", e.z);
	m_pos_z_box->setText(tmp);
}


