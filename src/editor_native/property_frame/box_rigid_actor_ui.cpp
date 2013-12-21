#include "editor_native/property_frame/box_rigid_actor_ui.h"
#include <cstdio>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/property_frame/property_frame.h"
#include "gui/check_box.h"
#include "gui/gui.h"
#include "gui/text_box.h"


BoxRigidActorUI::BoxRigidActorUI(Lux::UI::Block* parent, Lux::EditorClient& client)
	: Block(*parent->getGui(), parent, NULL)
{
	m_client = &client;
	setArea(0, 0, 0, 0, 1, 0, 0, 60);
	Lux::UI::Block* label = new Lux::UI::Block(*getGui(), this, "_text_centered");
	label->setBlockText("Box rigid actor");
	label->setArea(0, 0, 0, 0, 1, 0, 0, 20);

	label = new Lux::UI::Block(*getGui(), this, "_text");
	label->setBlockText("Size");
	label->setArea(0, 0, 0, 20, 0, 60, 0, 40);

	Lux::UI::Block* container = new Block(*getGui(), this, NULL);
	container->setArea(0, 60, 0, 20, 1, 0, 0, 40);

	getGui()->getCallback("BoxRigidActorUI_sizeChanged").bind<BoxRigidActorUI, &BoxRigidActorUI::sizeChanged>(this);
	getGui()->getCallback("BoxRigidActorUI_isDynamicChanged").bind<BoxRigidActorUI, &BoxRigidActorUI::isDynamicChanged>(this);

	m_x_size_box = new Lux::UI::TextBox("0", *getGui(), container);
	m_x_size_box->setArea(0, 0, 0, 0, 0.33f, -1, 0, 20);
	m_x_size_box->setOnTextAccepted("BoxRigidActorUI_sizeChanged");
	
	m_y_size_box = new Lux::UI::TextBox("0", *getGui(), container);
	m_y_size_box->setArea(0.33f, 0, 0, 0, 0.66f, -1, 0, 20);
	m_y_size_box->setOnTextAccepted("BoxRigidActorUI_sizeChanged");
	
	m_z_size_box = new Lux::UI::TextBox("0", *getGui(), container);
	m_z_size_box->setArea(0.66f, 0, 0, 0, 1, -1, 0, 20);
	m_z_size_box->setOnTextAccepted("BoxRigidActorUI_sizeChanged");

	label = new Lux::UI::Block(*getGui(), this, "_text");
	label->setBlockText("Dynamic");
	label->setArea(0, 0, 0, 40, 0, 60, 0, 60);

	m_is_dynamic_ui = new Lux::UI::CheckBox(false, *getGui(), this);
	m_is_dynamic_ui->setArea(0, 60, 0, 42, 0, 76, 0, 58);
	m_is_dynamic_ui->registerEventHandler("check_state_changed", "BoxRigidActorUI_isDynamicChanged");
}


void BoxRigidActorUI::isDynamicChanged(Lux::UI::Block& block, void*)
{
	bool value = static_cast<Lux::UI::CheckBox&>(block).isChecked();
	m_client->setComponentProperty("box_rigid_actor", "dynamic", &value, sizeof(value));
}


void BoxRigidActorUI::sizeChanged(Lux::UI::Block& block, void*)
{
	Lux::Vec3 size;
	sscanf_s(m_x_size_box->getText().c_str(), "%f", &size.x);
	sscanf_s(m_y_size_box->getText().c_str(), "%f", &size.y);
	sscanf_s(m_z_size_box->getText().c_str(), "%f", &size.z);
	m_client->setComponentProperty("box_rigid_actor", "size", &size, sizeof(size));
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
				m_x_size_box->setText(tmp);
				sprintf_s(tmp, "%f", size.y);
				m_y_size_box->setText(tmp);
				sprintf_s(tmp, "%f", size.z);
				m_z_size_box->setText(tmp);
			}
		}
	}
}


BoxRigidActorUI::~BoxRigidActorUI()
{
	
}
