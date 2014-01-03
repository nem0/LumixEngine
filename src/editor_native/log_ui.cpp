#include "editor_native/log_ui.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"
#include "gui/controls/scrollable.h"
#include "gui/gui.h"


LogUI::LogUI(MainFrame& main_frame)
	: Dockable(main_frame.getGui(), NULL)
	, m_main_frame(main_frame)
{
	m_scrollable = NULL;
	main_frame.getDockable().dock(*this, Dockable::BOTTOM);
	main_frame.getEditorClient()->getEventManager().addListener(Lux::ServerMessageType::LOG_MESSAGE).bind<LogUI, &LogUI::onLogMessage>(this);
	Lux::UI::Block* handle = LUX_NEW(Lux::UI::Block)(getGui(), this, "_box");
	handle->setArea(0, 0, 0, 0, 1, 0, 0, 20);
	handle->onEvent("mouse_down").bind<Dockable, &Dockable::startDrag>(this);
	m_scrollable = LUX_NEW(Lux::UI::Scrollable)(getGui(), this);
	m_scrollable->setArea(0, 0, 0, 20, 1, 0, 1, 0);
}


void LogUI::onLogMessage(Lux::Event& evt)
{
	Lux::UI::Block* container = m_scrollable->getContainer();
	float y = container->getChildCount() > 0 ? container->getChild(container->getChildCount() -1 )->getLocalArea().bottom : 0;
	Lux::LogEvent log_evt = static_cast<Lux::LogEvent&>(evt);
	Lux::UI::Block* cell = LUX_NEW(Lux::UI::Block)(getGui(), m_scrollable->getContainer(), "_text");
	cell->setBlockText(log_evt.system.c_str());
	cell->setArea(0, 0, 0, y, 0.3f, 0, 0, y + 20);

	cell = LUX_NEW(Lux::UI::Block)(getGui(), container, "_text");
	float w, h;
	getGui().getRenderer().measureText(log_evt.message.c_str(), &w, &h, getGlobalWidth() * 0.7f);
	cell->setBlockText(log_evt.message.c_str());
	cell->setArea(0.3f, 0, 0, y, 1, 0, 0, y + h + 5);
	layout();
}


void LogUI::layout()
{
	Dockable::layout();
	if(m_scrollable)
	{
		Lux::UI::Block* container = m_scrollable->getContainer();
		float w, h;
		float y = 0;
		for(int i = 1; i < container->getChildCount(); i += 2)
		{
			getGui().getRenderer().measureText(container->getChild(i)->getBlockText().c_str(), &w, &h, getGlobalWidth() * 0.7f);
		
			container->getChild(i-1)->getLocalArea().top = y;
			container->getChild(i)->getLocalArea().top = y;

			y += h + 5;
		}
		Dockable::layout();
	}
}
