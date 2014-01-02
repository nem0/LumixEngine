#include "editor_native/property_frame/script_ui.h"
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


ScriptUI::ScriptUI(PropertyFrame& property_frame, Lux::UI::Block* parent, Lux::EditorClient& client)
	: Block(parent->getGui(), parent, NULL)
	, m_property_frame(property_frame)
{
	m_client = &client;
	setArea(0, 0, 0, 0, 1, 0, 0, 40);
	Lux::UI::Block* label = new Lux::UI::Block(getGui(), this, "_text_centered");
	label->setBlockText("Script");
	label->setArea(0, 0, 0, 0, 1, 0, 0, 20);

	label = new Lux::UI::Block(getGui(), this, "_text");
	label->setBlockText("Source");
	label->setArea(0, 0, 0, 20, 0, 50, 0, 40);

	m_source_box = new Lux::UI::TextBox("empty", getGui(), this);
	m_source_box->setArea(0, 50, 0, 20, 1, -21, 0, 40);
//	m_source_box->onEvent("text_accepted").bind<ScriptUI, &ScriptUI::sourceChanged>(this);
}

