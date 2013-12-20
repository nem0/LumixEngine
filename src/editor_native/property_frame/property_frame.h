#pragma once


#include "core/vector.h"


namespace Lux
{
	class Event;

	namespace UI
	{
		class Block;
	}
}


class MainFrame;
class IComponentUI;


class PropertyFrame
{
	public:
		bool create(MainFrame& main_frame);

	private:
		static void createComponentClicked(Lux::UI::Block& block);
		static void typeListPopupBlur(Lux::UI::Block& block);
		static void newComponentClick(Lux::UI::Block& block);
		static void positionChanged(Lux::UI::Block& block);
		static void onEntitySelected(void* user_data, Lux::Event& evt);
		static void onEntityPosition(void* user_data, Lux::Event& evt);
		static void onEntityProperties(void* user_data, Lux::Event& evt);

	private:
		Lux::UI::Block* m_ui;
		Lux::UI::Block* m_create_component_button;
		Lux::UI::Block* m_type_list_popup;
		Lux::UI::Block* m_pos_x_box;
		Lux::UI::Block* m_pos_y_box;
		Lux::UI::Block* m_pos_z_box;
		Lux::UI::Block* m_component_container;
		MainFrame* m_main_frame;
		Lux::vector<IComponentUI*> m_component_uis; 
};