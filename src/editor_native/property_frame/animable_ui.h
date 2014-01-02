#pragma once


#include "gui/block.h"
#include "editor_native/property_frame/icomponent_ui.h"


class PropertyFrame;
namespace Lux
{
	class EditorClient;
	namespace UI
	{
		class Button;
		class TextBox;
	}
}


class AnimableUI : public IComponentUI, public Lux::UI::Block
{
	public:
		AnimableUI(PropertyFrame& property_frame, Lux::UI::Block* owner, Lux::EditorClient& client);
		~AnimableUI() {}
		virtual void onEntityProperties(Lux::PropertyListEvent& evt) LUX_OVERRIDE {}

	private:
		Lux::EditorClient* m_client;
		PropertyFrame& m_property_frame;
};