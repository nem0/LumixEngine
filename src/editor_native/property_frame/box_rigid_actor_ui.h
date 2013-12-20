#pragma once


#include "editor_native/property_frame/icomponent_ui.h"


class PropertyFrame;
namespace Lux
{
	class EditorClient;
}


class BoxRigidActorUI : public IComponentUI
{
	public:
		BoxRigidActorUI(Lux::UI::Block* owner, Lux::EditorClient& client);
		~BoxRigidActorUI();
		Lux::UI::Block* getUI() const { return m_ui; }
		virtual void onEntityProperties(Lux::PropertyListEvent& evt) LUX_OVERRIDE;

	private:
		static void sizeChanged(Lux::UI::Block& block);

	private:
		Lux::UI::Block* m_ui;
		Lux::UI::Block* m_x_size_box;
		Lux::UI::Block* m_y_size_box;
		Lux::UI::Block* m_z_size_box;
		Lux::EditorClient* m_client;
};