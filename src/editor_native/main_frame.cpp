#include "main_frame.h"
#include <cstdio>
#include <Windows.h>
#include "core/log.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/log_ui.h"
#include "editor_native/main_menu.h"
#include "editor_native/notifications.h"
#include "editor_native/property_frame/property_frame.h"
#include "editor_native/script_compiler.h"
#include "gui/gui.h"


MainFrame::MainFrame(Lux::EditorClient& client, Lux::UI::Gui& gui, Lux::UI::Block* parent)
	: Block(gui, parent, NULL)
{
	m_script_compiler = new ScriptCompiler();
	m_editor_client = &client;
	m_gui = &gui;

	setArea(0, 0, 0, 0, 1, 0, 1, 0);
	m_dockable = new Lux::UI::Dockable(gui, this);
	m_dockable->setArea(0, 0, 0, 20, 1, 0, 1, 0);

	parent->setIsClickable(false);
	setIsClickable(false);
	m_dockable->setIsClickable(false);
	m_dockable->getContent()->setIsClickable(false);

	m_main_menu = new MainMenu(*this);
	m_property_frame = new PropertyFrame(*this);
	m_log_ui = new LogUI(*this);
	m_notifications = new Notifications(*this);

	char path[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, path);
	m_startup_directory = path;
	m_script_compiler->setBasePath(path);
	m_script_compiler->onCompile().bind<MainFrame, &MainFrame::scriptCompiled>(this);
	m_script_compiler->compileAll();
}


void MainFrame::scriptCompiled(const char* path, uint32_t exit_code)
{
	char msg[500];
	if(exit_code != 0)
	{
		sprintf(msg, "Script %s failed to compile", path);
		Lux::g_log_info.log("scripts", msg);
		m_notifications->showNotification(msg);
	}
	if(m_script_compiler->isEmpty())
	{
		m_notifications->showNotification("All scripts processed");
	}
}


void MainFrame::update()
{
	m_notifications->update();
	m_script_compiler->checkFinished();
}