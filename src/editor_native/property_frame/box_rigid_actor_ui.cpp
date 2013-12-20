#include "editor_native/property_frame/box_rigid_actor_ui.h"
#include <cstdio>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/property_frame/property_frame.h"
#include "gui/controls.h"
#include "gui/gui.h"


BoxRigidActorUI::BoxRigidActorUI(Lux::UI::Block* owner, Lux::EditorClient& client)
{
	m_client = &client;
	m_ui = owner->getGui()->createBlock(owner, NULL);
	m_ui->setArea(0, 0, 0, 0, 1, 0, 0, 20);
	Lux::UI::Block* label = owner->getGui()->createBlock(m_ui, "_text");
	label->setText("Box rigid actor");
	label->setArea(0, 0, 0, 0, 1, 0, 0, 20);

	label = owner->getGui()->createBlock(m_ui, "_text");
	label->setText("Size");
	label->setArea(0, 0, 0, 20, 0, 50, 0, 40);

	Lux::UI::Block* container = owner->getGui()->createBlock(m_ui, NULL);
	container->setArea(0, 50, 0, 20, 1, 0, 0, 40);

	owner->getGui()->addCallback("BoxRigidActorUI_sizeChanged", &BoxRigidActorUI::sizeChanged);

	m_x_size_box = Lux::UI::createTextBox(0, 0, container, *owner->getGui());
	m_x_size_box->setArea(0, 0, 0, 0, 0.33f, -1, 0, 20);
	m_x_size_box->getChild(0)->setText("-1");
	m_x_size_box->setTag(this);
	m_x_size_box->getChild(0)->registerEventHandler("text_accepted", "BoxRigidActorUI_sizeChanged");
	
	m_y_size_box = Lux::UI::createTextBox(0, 0, container, *owner->getGui());
	m_y_size_box->setArea(0.33f, 0, 0, 0, 0.66f, -1, 0, 20);
	m_y_size_box->setTag(this);
	m_y_size_box->getChild(0)->setText("-1");
	m_y_size_box->getChild(0)->registerEventHandler("text_accepted", "BoxRigidActorUI_sizeChanged");
	
	m_z_size_box = Lux::UI::createTextBox(0, 0, container, *owner->getGui());
	m_z_size_box->setArea(0.66f, 0, 0, 0, 1, -1, 0, 20);
	m_z_size_box->setTag(this);
	m_z_size_box->getChild(0)->setText("-1");
	m_z_size_box->getChild(0)->registerEventHandler("text_accepted", "BoxRigidActorUI_sizeChanged");
}


void BoxRigidActorUI::sizeChanged(Lux::UI::Block& block)
{
	BoxRigidActorUI* that = static_cast<BoxRigidActorUI*>(block.getParent()->getTag());
	Lux::Vec3 size;
	sscanf_s(that->m_x_size_box->getChild(0)->getText().c_str(), "%f", &size.x);
	sscanf_s(that->m_y_size_box->getChild(0)->getText().c_str(), "%f", &size.y);
	sscanf_s(that->m_z_size_box->getChild(0)->getText().c_str(), "%f", &size.z);
	that->m_client->setComponentProperty("box_rigid_actor", "size", &size, sizeof(size));
}


void BoxRigidActorUI::onEntityProperties(Lux::PropertyListEvent& evt)
{
	if(evt.type_hash == crc32("box_rigid_actor"))
	{
		for(int i = 0; i < evt.properties.size(); ++i)
		{
			if(evt.properties[i].name_hash == crc32("size"))
			{
				Lux::Vec3 size = *(Lux::Vec3*)evt.properties[i].data;
				char tmp[30];
				sprintf_s(tmp, "%f", size.x);
				m_x_size_box->getChild(0)->setText(tmp);
				sprintf_s(tmp, "%f", size.y);
				m_y_size_box->getChild(0)->setText(tmp);
				sprintf_s(tmp, "%f", size.z);
				m_z_size_box->getChild(0)->setText(tmp);
			}
		}
	}
}


BoxRigidActorUI::~BoxRigidActorUI()
{
	m_ui->destroy();
}
