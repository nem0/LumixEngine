#include "editor_native/main_menu.h"
#include "editor/editor_client.h"
#include "editor_native/main_frame.h"
#include "gui/controls.h"
#include "gui/gui.h"


bool MainMenu::create(MainFrame& main_frame)
{
	m_main_frame = &main_frame;
	m_ui = main_frame.getGui().createBlock(&main_frame.getUI(), "_box");
	m_ui->setArea(0, 0, 0, 0, 1, 0, 0, 24);

	Lux::UI::Block* menu_item = Lux::UI::createButton("Entity", 0, 2, m_ui, main_frame.getGui());
	menu_item->registerEventHandler("click", "_menu_show_submenu");

	Lux::UI::Block* submenu = main_frame.getGui().createBlock(menu_item, NULL);
	submenu->setArea(0, 0, 0, 20, 1, 0, 0, 42);
	submenu->setText("submenu");
	submenu->hide();
	submenu->setZIndex(1);
	submenu->registerEventHandler("blur", "_hide");

	Lux::UI::Block* submenu_item = Lux::UI::createButton("Create", 0, 0, submenu, main_frame.getGui());
	main_frame.getGui().addCallback("MainMenu_entityCreateClick", &MainMenu::entityCreateClick);
	submenu_item->registerEventHandler("click", "MainMenu_entityCreateClick");
	submenu_item->registerEventHandler("click", "_hide_parent");
	submenu_item->setTag(this);
	return true;
}

void MainMenu::entityCreateClick(Lux::UI::Block& block)
{
	static_cast<MainMenu*>(block.getTag())->m_main_frame->getEditorClient()->addEntity();
}

