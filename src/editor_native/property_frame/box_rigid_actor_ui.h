#pragma once


#include "editor_native/property_frame/icomponent_ui.h"
#include "gui/block.h"


class PropertyFrame;
namespace Lux
{
	class EditorClient;
	namespace UI
	{
		class CheckBox;
		class TextBox;
	}
}


class BoxRigidActorUI : public IComponentUI, Lux::UI::Block
{
	public:
		BoxRigidActorUI(Lux::UI::Block* parent, Lux::EditorClient& client);
		virtual ~BoxRigidActorUI();
		virtual void onEntityProperties(Lux::PropertyListEvent& evt) LUX_OVERRIDE;

	private:
		void sizeChanged(Lux::UI::Block& block, void*);
		void isDynamicChanged(Lux::UI::Block& block, void*);

	private:
		Lux::UI::TextBox* m_x_size_box;
		Lux::UI::TextBox* m_y_size_box;
		Lux::UI::TextBox* m_z_size_box;
		Lux::UI::CheckBox* m_is_dynamic_ui;
		Lux::EditorClient* m_client;
};