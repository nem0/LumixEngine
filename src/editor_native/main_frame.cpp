#include "main_frame.h"
#include <Windows.h>
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_menu.h"
#include "editor_native/property_frame/property_frame.h"
#include "gui/gui.h"

#include "gui/controls/scrollbar.h"

bool MainFrame::create(Lux::EditorClient& client, Lux::UI::Gui& gui, float width, float height)
{
	m_editor_client = &client;
	m_gui = &gui;
	m_ui = gui.createTopLevelBlock(width, height);
	m_ui->setArea(0, 0, 0, 0, 0, width, 0, height);
	m_ui->setIsClickable(false);

	m_main_menu = new MainMenu(*this);
	m_property_frame = new PropertyFrame(*this);

	Lux::UI::Scrollbar* scrollbar = new Lux::UI::Scrollbar(gui, m_ui);
	scrollbar->setArea(0.5f, 0, 1, -20, 1, -50, 1, 0);

	scrollbar = new Lux::UI::Scrollbar(gui, m_ui);
	scrollbar->setArea(1, -20, 0.5f, 0, 1, 0, 1, 0);
	scrollbar->setScrollbarType(Lux::UI::Scrollbar::VERTICAL);


	m_ui->layout();

	char path[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, path);
	m_startup_directory = path;

	return true;
}


