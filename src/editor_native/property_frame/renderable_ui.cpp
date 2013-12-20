#include "editor_native/property_frame/renderable_ui.h"
#include <cstdio>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/property_frame/property_frame.h"
#include "gui/controls.h"
#include "gui/gui.h"


RenderableUI::RenderableUI(Lux::UI::Block* owner, Lux::EditorClient& client)
{
	m_client = &client;
	m_ui = owner->getGui()->createBlock(owner, NULL);
	m_ui->setArea(0, 0, 0, 0, 1, 0, 0, 20);
	Lux::UI::Block* label = owner->getGui()->createBlock(m_ui, "_text");
	label->setText("Renderable");
	label->setArea(0, 0, 0, 0, 1, 0, 0, 20);

	label = owner->getGui()->createBlock(m_ui, "_text");
	label->setText("Source");
	label->setArea(0, 0, 0, 20, 0, 50, 0, 40);

	owner->getGui()->addCallback("RenderableUI_sourceChanged", &RenderableUI::sourceChanged);

	m_source_box = Lux::UI::createTextBox(50, 20, m_ui, *owner->getGui());
	m_source_box->setArea(0, 50, 0, 20, 1, -1, 0, 40);
	m_source_box->getChild(0)->setText("empty");
	m_source_box->setTag(this);
	m_source_box->getChild(0)->registerEventHandler("text_accepted", "RenderableUI_sourceChanged");
	
}


void RenderableUI::sourceChanged(Lux::UI::Block& block)
{
	RenderableUI* that = static_cast<RenderableUI*>(block.getParent()->getTag());
	const Lux::string& s = that->m_source_box->getChild(0)->getText();
	that->m_client->setComponentProperty("renderable", "source", s.c_str(), s.length()+1);
}


void RenderableUI::onEntityProperties(Lux::PropertyListEvent& evt)
{
	if(evt.type_hash == crc32("renderable"))
	{
		for(int i = 0; i < evt.properties.size(); ++i)
		{
			if(evt.properties[i].name_hash == crc32("source"))
			{
				if(evt.properties[i].data_size > 0)
				{
					m_source_box->getChild(0)->setText((char*)evt.properties[i].data);
				}
			}
		}
	}
}


RenderableUI::~RenderableUI()
{
	m_ui->destroy();
}
