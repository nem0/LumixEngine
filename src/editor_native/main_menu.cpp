#include "editor_native/main_menu.h"
#include <Windows.h>
#include "editor/editor_client.h"
#include "editor_native/main_frame.h"
#include "gui/gui.h"
#include "gui/controls/menu_item.h"


void MainMenu::openFileClicked(Lux::UI::Block& block, void*)
{
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFilter = "scenes\0*.unv\0";
	char buf[MAX_PATH];
	buf[0] = 0;
	ofn.lpstrFile = buf;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	if(GetOpenFileName(&ofn) != FALSE)
	{
		Lux::string& s = m_main_frame->getStartupDirectory();
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
		m_main_frame->getEditorClient()->loadUniverse(buf);
	}
}



void MainMenu::saveFileClicked(Lux::UI::Block& block, void*)
{
	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFilter = "scenes\0*.unv\0";
	char buf[MAX_PATH];
	buf[0] = 0;
	ofn.lpstrFile = buf;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY;
	if(GetOpenFileName(&ofn) != FALSE)
	{
		Lux::string& s = m_main_frame->getStartupDirectory();
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
		m_main_frame->getEditorClient()->saveUniverse(buf);
	}
}



MainMenu::MainMenu(MainFrame& main_frame)
	: MenuBar(main_frame.getGui(), &main_frame.getUI())
{
	m_main_frame = &main_frame;
	// file
	Lux::UI::MenuItem* file_item = new Lux::UI::MenuItem("File", *getGui());
	addItem(file_item);

	// file -> open
	Lux::UI::MenuItem* file_open_item = new Lux::UI::MenuItem("Open", *getGui());
	file_item->addSubItem(file_open_item);
	getGui()->getCallback("MainMenu__openFile").bind<MainMenu, &MainMenu::openFileClicked>(this);
	file_open_item->registerEventHandler("click", "MainMenu__openFile");

	// file -> save
	Lux::UI::MenuItem* file_save_item = new Lux::UI::MenuItem("Save", *getGui());
	file_item->addSubItem(file_save_item);
	getGui()->getCallback("MainMenu__saveFile").bind<MainMenu, &MainMenu::saveFileClicked>(this);
	file_save_item->registerEventHandler("click", "MainMenu__saveFile");

	// entity
	Lux::UI::MenuItem* entity_item = new Lux::UI::MenuItem("Entity", *getGui());
	addItem(entity_item);

	// entity -> create
	Lux::UI::MenuItem* entity_create_item = new Lux::UI::MenuItem("Create", *getGui());
	entity_item->addSubItem(entity_create_item);
	getGui()->getCallback("MainMenu__entityCreate").bind<MainMenu, &MainMenu::entityCreateClick>(this);
	entity_create_item->registerEventHandler("click", "MainMenu__entityCreate");

	// tools
	Lux::UI::MenuItem* tools_item = new Lux::UI::MenuItem("Tools", *getGui());
	addItem(tools_item);

	// tools -> game mode
	Lux::UI::MenuItem* tools_play_item = new Lux::UI::MenuItem("Play", *getGui());
	tools_item->addSubItem(tools_play_item);
	getGui()->getCallback("MainMenu__toolsPlay").bind<MainMenu, &MainMenu::toolsPlay>(this);
	tools_play_item->registerEventHandler("click", "MainMenu__toolsPlay");
}


void MainMenu::toolsPlay(Lux::UI::Block& block, void*)
{
	m_main_frame->getEditorClient()->toggleGameMode();
}


void MainMenu::entityCreateClick(Lux::UI::Block& block, void*)
{
	m_main_frame->getEditorClient()->addEntity();
}

