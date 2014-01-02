#include "editor_native/property_frame/renderable_ui.h"
#include <cstdio>
#include <Windows.h>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"
#include "editor_native/property_frame/property_frame.h"
#include "gui/controls/button.h"
#include "gui/controls/text_box.h"
#include "gui/gui.h"


RenderableUI::RenderableUI(PropertyFrame& property_frame, Lux::UI::Block* parent, Lux::EditorClient& client)
	: Block(parent->getGui(), parent, NULL)
	, m_property_frame(property_frame)
{
	m_client = &client;
	setArea(0, 0, 0, 0, 1, 0, 0, 40);
	Lux::UI::Block* label = LUX_NEW(Lux::UI::Block)(getGui(), this, "_text_centered");
	label->setBlockText("Renderable");
	label->setArea(0, 0, 0, 0, 1, 0, 0, 20);

	label = LUX_NEW(Lux::UI::Block)(getGui(), this, "_text");
	label->setBlockText("Source");
	label->setArea(0, 0, 0, 20, 0, 50, 0, 40);

	m_source_box = LUX_NEW(Lux::UI::TextBox)("empty", getGui(), this);
	m_source_box->setArea(0, 50, 0, 20, 1, -21, 0, 40);
	m_source_box->onEvent("text_accepted").bind<RenderableUI, &RenderableUI::sourceChanged>(this);
	
	m_browse_source_button = LUX_NEW(Lux::UI::Button)("...", getGui(), this);
	m_browse_source_button->setArea(1, -20, 0, 20, 1, -1, 0, 40);
	m_browse_source_button->onEvent("click").bind<RenderableUI, &RenderableUI::browseSource>(this);
}


void RenderableUI::sourceChanged(Lux::UI::Block& block, void*)
{
	const Lux::string& s = m_source_box->getChild(0)->getBlockText();
	m_client->setComponentProperty("renderable", "source", s.c_str(), s.length()+1);
}


void RenderableUI::browseSource(Lux::UI::Block& block, void*)
{
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFilter = "models\0*.scene.xml\0";
	char buf[MAX_PATH];
	buf[0] = 0;
	ofn.lpstrFile = buf;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	if(GetOpenFileName(&ofn) != FALSE)
	{
		Lux::string& s = m_property_frame.getMainFrame()->getStartupDirectory();
		if(strncmp(s.c_str(), buf, s.length()) == 0)
		{
			if(buf[s.length()] == '\\')
			{
				strcpy_s(buf, buf + s.length()+1);
			}
			else
			{
				strcpy_s(buf, buf + s.length());
			}
		}
		m_source_box->getChild(0)->setBlockText(buf);
		m_source_box->getChild(0)->emitEvent("text_accepted");
	}

	const Lux::string& s = m_source_box->getText();
	m_client->setComponentProperty("renderable", "source", s.c_str(), s.length()+1);
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
					m_source_box->setText((char*)evt.properties[i].data);
				}
			}
		}
	}
}


RenderableUI::~RenderableUI()
{
}
