#pragma once


#include "core/string.h"
#include "gui/controls/dockable.h"


class MainFrame;


namespace Lux
{

	class EditorClient;
	class Event;
	struct ServerMessage;

	namespace UI
	{
		class Gui;
		class Scrollable;
	}
}


class LogUI : public Lux::UI::Dockable
{
	public:
		LogUI(MainFrame& main_frame);
		virtual void layout() LUX_OVERRIDE;

	private:
		void onLogMessage(Lux::Event& evt);

	private:
		MainFrame& m_main_frame;
		Lux::UI::Scrollable* m_scrollable;
};