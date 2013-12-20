#pragma once


#include "editor_native/property_frame/icomponent_ui.h"


class PropertyFrame;
namespace Lux
{
	class EditorClient;
}


class RenderableUI : public IComponentUI
{
	public:
		RenderableUI(Lux::UI::Block* owner, Lux::EditorClient& client);
		~RenderableUI();
		Lux::UI::Block* getUI() const { return m_ui; }
		virtual void onEntityProperties(Lux::PropertyListEvent& evt) LUX_OVERRIDE;

	private:
		static void sourceChanged(Lux::UI::Block& block);

	private:
		Lux::UI::Block* m_ui;
		Lux::UI::Block* m_source_box;
		Lux::EditorClient* m_client;
};