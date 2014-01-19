#pragma once


#include "core/string.h"
#include "gui/controls/dockable.h"


class ScriptCompiler;


namespace Lux
{

	class EditorClient;
	struct ServerMessage;

	namespace UI
	{

		class Block;
		class Gui;

	}
}


class MainFrame : public Lux::UI::Block
{
	public:
		MainFrame(Lux::EditorClient& client, Lux::UI::Gui& gui, Lux::UI::Block* parent);
		void update();
		Lux::UI::Gui& getGui() { return *m_gui; }
		Lux::EditorClient* getEditorClient() const { return m_editor_client; }
		Lux::string& getStartupDirectory() { return m_startup_directory; }
		Lux::UI::Dockable& getDockable() { return *m_dockable; }
		ScriptCompiler& getScriptCompiler() { return *m_script_compiler; }

	private:
		void scriptCompiled(const char* path, uint32_t exit_code);

	private:
		Lux::UI::Gui* m_gui;
		Lux::EditorClient* m_editor_client;
		Lux::string m_startup_directory;
		class PropertyFrame* m_property_frame;
		class MainMenu* m_main_menu;
		class LogUI* m_log_ui;
		ScriptCompiler* m_script_compiler; 
		class Notifications* m_notifications;
		Lux::UI::Dockable* m_dockable;
};