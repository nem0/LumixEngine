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


class ScriptUI : public IComponentUI, public Lux::UI::Block
{
	public:
		ScriptUI(PropertyFrame& property_frame, Lux::UI::Block* owner, Lux::EditorClient& client);
		~ScriptUI() {}
		virtual void onEntityProperties(Lux::PropertyListEvent& evt) LUX_OVERRIDE;

	private:
		void sourceChanged(Lux::UI::Block& block, void*);
		void browseSource(Lux::UI::Block& block, void*);
		void editScript(Lux::UI::Block& block, void*);
		void compileScript(Lux::UI::Block& block, void*);

	private:
		Lux::UI::TextBox* m_source_box;
		Lux::EditorClient* m_client;
		PropertyFrame& m_property_frame;
};