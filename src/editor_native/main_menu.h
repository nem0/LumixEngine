#pragma once


#include "gui/controls/menu_bar.h"


namespace Lux
{

	namespace UI
	{
		class Block;
	}

} 


class MainFrame;


class MainMenu : public Lux::UI::MenuBar
{
	public:
		MainMenu(MainFrame& main_frame);

	private:
		void toolsPlay(Lux::UI::Block& block, void*);
		void openFileClicked(Lux::UI::Block& block, void*);
		void saveFileClicked(Lux::UI::Block& block, void*);
		void entityCreateClick(Lux::UI::Block& block, void*);

	private:
		MainFrame* m_main_frame;
};
