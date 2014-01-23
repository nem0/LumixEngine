#include "editor_native/property_frame/point_light_ui.h"
#include <cstdio>
#include <Windows.h>
#include "core/crc32.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"
#include "editor_native/property_frame/property_frame.h"
#include "editor_native/script_compiler.h"
#include "gui/controls/button.h"
#include "gui/controls/text_box.h"
#include "gui/gui.h"


PointLightUI::PointLightUI(PropertyFrame& property_frame, Lux::UI::Block* parent, Lux::EditorClient& client)
	: Block(parent->getGui(), parent, NULL)
	, m_property_frame(property_frame)
{
	m_client = &client;
	setArea(0, 0, 0, 0, 1, 0, 0, 83);
	Lux::UI::Block* label = LUX_NEW(Lux::UI::Block)(getGui(), this, "_text_centered");
	label->setBlockText("Point light");
	label->setArea(0, 0, 0, 0, 1, 0, 0, 20);
}


void PointLightUI::onEntityProperties(Lux::PropertyListEvent& evt)
{

}
