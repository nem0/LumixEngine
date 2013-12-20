#include "main_frame.h"
#include "core/functor.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_menu.h"
#include "editor_native/property_frame/property_frame.h"
#include "gui/gui.h"


bool MainFrame::create(Lux::EditorClient& client, Lux::UI::Gui& gui, float width, float height)
{
	m_editor_client = &client;
	m_gui = &gui;
	m_ui = gui.createTopLevelBlock(width, height);
	m_ui->setArea(0, 0, 0, 0, 0, width, 0, height);
	m_ui->setIsClickable(false);

	m_main_menu = new MainMenu();
	m_main_menu->create(*this);

	m_property_frame = new PropertyFrame();
	m_property_frame->create(*this);

	m_ui->layout();
	return true;
}


