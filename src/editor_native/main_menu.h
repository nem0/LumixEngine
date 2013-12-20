#pragma once


namespace Lux
{

	namespace UI
	{
		class Block;
	}

} 


class MainFrame;


class MainMenu
{
	public:
		bool create(MainFrame& main_frame);

	private:
		static void entityCreateClick(Lux::UI::Block& block);

	private:
		Lux::UI::Block* m_ui;
		MainFrame* m_main_frame;
};
