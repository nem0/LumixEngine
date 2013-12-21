#pragma once


#include "core/vector.h"
#include "gui/block.h"


namespace Lux
{
	class Event;

	namespace UI
	{
		class Button;
		class TextBox;
	}
}


class MainFrame;
class IComponentUI;


class PropertyFrame : public Lux::UI::Block
{
	public:
		PropertyFrame(MainFrame& main_frame);
		MainFrame* getMainFrame() const { return m_main_frame; }

	private:
		void createComponentClicked(Lux::UI::Block& block, void*);
		void positionChanged(Lux::UI::Block& block, void*);
		void newComponentClick(Lux::UI::Block& block, void*);
		static void onEntitySelected(void* user_data, Lux::Event& evt);
		static void onEntityPosition(void* user_data, Lux::Event& evt);
		static void onEntityProperties(void* user_data, Lux::Event& evt);

	private:
		Lux::UI::Button* m_create_component_button;
		Lux::UI::Block* m_type_list_popup;
		Lux::UI::TextBox* m_pos_x_box;
		Lux::UI::TextBox* m_pos_y_box;
		Lux::UI::TextBox* m_pos_z_box;
		Lux::UI::Block* m_component_container;
		MainFrame* m_main_frame;
		Lux::vector<IComponentUI*> m_component_uis; 
};