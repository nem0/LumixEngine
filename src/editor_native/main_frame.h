#pragma once


#include "core/string.h"


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


class MainFrame
{
	public:
		bool create(Lux::EditorClient& client, Lux::UI::Gui& gui, float width, float height);
		Lux::UI::Gui& getGui() { return *m_gui; }
		Lux::UI::Block& getUI() { return *m_ui; }
		Lux::EditorClient* getEditorClient() const { return m_editor_client; }
		Lux::string& getStartupDirectory() { return m_startup_directory; }

	private:
		Lux::UI::Block* m_ui;
		Lux::UI::Gui* m_gui;
		Lux::EditorClient* m_editor_client;
		Lux::string m_startup_directory;
		class PropertyFrame* m_property_frame;
		class MainMenu* m_main_menu;
};