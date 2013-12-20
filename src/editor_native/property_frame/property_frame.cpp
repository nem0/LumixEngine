#include "editor_native/property_frame/property_frame.h"
#include <cstdio>
#include "core/crc32.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"
#include "editor_native/property_frame/box_rigid_actor_ui.h"
#include "editor_native/property_frame/renderable_ui.h"
#include "gui/controls.h"
#include "gui/gui.h"


void PropertyFrame::createComponentClicked(Lux::UI::Block& block)
{
	static_cast<PropertyFrame*>(block.getTag())->m_type_list_popup->getGui()->focus(static_cast<PropertyFrame*>(block.getTag())->m_type_list_popup);
	static_cast<PropertyFrame*>(block.getTag())->m_type_list_popup->show();
}


void PropertyFrame::newComponentClick(Lux::UI::Block& block)
{
	static_cast<PropertyFrame*>(block.getParent()->getTag())->m_main_frame->getEditorClient()->addComponent((uint32_t)block.getTag());
	block.getParent()->hide();
}


void PropertyFrame::positionChanged(Lux::UI::Block& block)
{
	PropertyFrame* that = static_cast<PropertyFrame*>(block.getParent()->getTag());
	Lux::Vec3 v;
	sscanf_s(that->m_pos_x_box->getChild(0)->getText().c_str(), "%f", &v.x);
	sscanf_s(that->m_pos_y_box->getChild(0)->getText().c_str(), "%f", &v.y);
	sscanf_s(that->m_pos_z_box->getChild(0)->getText().c_str(), "%f", &v.z);
	ASSERT(false); // TODO
}


void PropertyFrame::typeListPopupBlur(Lux::UI::Block& block)
{
	block.hide();
}


void PropertyFrame::onEntitySelected(void* user_data, Lux::Event& evt)
{
	PropertyFrame* that = static_cast<PropertyFrame*>(user_data);
	for(int i = 0; i < that->m_component_uis.size(); ++i)
	{
		delete that->m_component_uis[i];
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
			ui = new RenderableUI(that->m_component_container, *that->m_main_frame->getEditorClient());
		}
		else
		{
			ASSERT(false);
		}
		that->m_component_uis.push_back(ui);
	}
	that->m_ui->layout();
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
	that->m_pos_x_box->getChild(0)->setText(tmp);
	sprintf_s(tmp, "%f", e.y);
	that->m_pos_y_box->getChild(0)->setText(tmp);
	sprintf_s(tmp, "%f", e.z);
	that->m_pos_z_box->getChild(0)->setText(tmp);
}


bool PropertyFrame::create(MainFrame& main_frame)
{
	m_main_frame = &main_frame;
	m_ui = main_frame.getGui().createBlock(&main_frame.getUI(), "_box");
	m_ui->setArea(0, 0, 0, 23, 0.4f, 0, 1, 0);

	m_create_component_button = Lux::UI::createButton("create component", 0, 0, m_ui, main_frame.getGui());
	m_create_component_button->setArea(0, 5, 1, -25, 1, -5, 1, -5);
	main_frame.getGui().addCallback("PropertyFrame_createComponentClicked", &PropertyFrame::createComponentClicked);
	m_create_component_button->registerEventHandler("click", "PropertyFrame_createComponentClicked");
	m_create_component_button->setTag(this);

	Lux::UI::Block* label = main_frame.getGui().createBlock(m_ui, "_text");
	label->setText("Position");
	label->setArea(0, 0, 0, 0, 0, 50, 0, 20);

	Lux::UI::Block* positions = main_frame.getGui().createBlock(m_ui, NULL);
	positions->setArea(0, 50, 0, 2, 1, -2, 0, 20);
	main_frame.getGui().addCallback("PropertyFrame_positionChanged", &PropertyFrame::positionChanged);
	m_pos_x_box = Lux::UI::createTextBox(0, 0, positions, main_frame.getGui());
	m_pos_x_box->setArea(0, 1, 0, 0, 0.33f, 0, 0, 20);
	m_pos_x_box->setTag(this);
	m_pos_x_box->getChild(0)->registerEventHandler("text_accepted", "PropertyFrame_positionChanged");
	m_pos_x_box->getChild(0)->setText("0");
	m_pos_y_box = Lux::UI::createTextBox(0, 0, positions, main_frame.getGui());
	m_pos_y_box->setArea(0.33f, 1, 0, 0, 0.66f, 0, 0, 20);
	m_pos_y_box->setTag(this);
	m_pos_y_box->getChild(0)->registerEventHandler("text_accepted", "PropertyFrame_positionChanged");
	m_pos_y_box->getChild(0)->setText("0");
	m_pos_z_box = Lux::UI::createTextBox(0, 0, positions, main_frame.getGui());
	m_pos_z_box->setArea(0.66f, 1, 0, 0, 1, 0, 0, 20);
	m_pos_z_box->setTag(this);
	m_pos_z_box->getChild(0)->registerEventHandler("text_accepted", "PropertyFrame_positionChanged");
	m_pos_z_box->getChild(0)->setText("0");

	m_component_container = main_frame.getGui().createBlock(m_ui, NULL);
	m_component_container->setArea(0, 0, 0, 24, 1, 0, 1, -30);

	m_type_list_popup = main_frame.getGui().createBlock(m_ui, "_box");
	m_type_list_popup->setArea(0, 5, 0.5f, 0, 1, -5, 1, -25);
	m_type_list_popup->hide();
	main_frame.getGui().addCallback("PropertyFrame_typeListPopupBlur", &PropertyFrame::typeListPopupBlur);
	m_type_list_popup->registerEventHandler("blur", "PropertyFrame_typeListPopupBlur");
	m_type_list_popup->setTag(this);
	m_type_list_popup->setZIndex(1);

	main_frame.getGui().addCallback("PropertyFrame_newComponentClick", &PropertyFrame::newComponentClick);
	{
		Lux::UI::Block* item = main_frame.getGui().createBlock(m_type_list_popup, "_text");
		item->setArea(0, 0, 0, 0, 1, 0, 0, 20);
		item->setText("Renderable");
		item->registerEventHandler("click", "PropertyFrame_newComponentClick");
		item->setTag((void*)crc32("renderable"));
	}
	{
		Lux::UI::Block* item = main_frame.getGui().createBlock(m_type_list_popup, "_text");
		item->setArea(0, 0, 0, 20, 1, 0, 0, 40);
		item->setText("Box rigid actor");
		item->registerEventHandler("click", "PropertyFrame_newComponentClick");
		item->setTag((void*)crc32("box_rigid_actor"));
	}

	main_frame.getEditorClient()->getEventManager().registerListener(Lux::ServerMessageType::ENTITY_POSITION, this, &PropertyFrame::onEntityPosition);
	main_frame.getEditorClient()->getEventManager().registerListener(Lux::ServerMessageType::PROPERTY_LIST, this, &PropertyFrame::onEntityProperties);
	main_frame.getEditorClient()->getEventManager().registerListener(Lux::ServerMessageType::ENTITY_SELECTED, this, &PropertyFrame::onEntitySelected);

	return true;
}