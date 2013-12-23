#pragma once


#include "core/string.h"
#include "gui/controls/scrollable.h"


class MainFrame;


namespace Lux
{

	class EditorClient;
	class Event;
	struct ServerMessage;

	namespace UI
	{
		class Gui;
	}
}


class LogUI : public Lux::UI::Scrollable
{
	public:
		LogUI(MainFrame& main_frame);

	private:
		static void onLogMessage(void* user_data, Lux::Event& evt);

	private:
		MainFrame& m_main_frame;
};