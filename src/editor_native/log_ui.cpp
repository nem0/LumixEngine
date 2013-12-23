#include "editor_native/log_ui.h"
#include "core/event_manager.h"
#include "editor/editor_client.h"
#include "editor/server_message_types.h"
#include "editor_native/main_frame.h"


LogUI::LogUI(MainFrame& main_frame)
	: Scrollable(main_frame.getGui(), &main_frame.getUI())
	, m_main_frame(main_frame)
{
	setArea(0.4f, 0, 0.7f, 0, 1, 0, 1, 0);
	main_frame.getEditorClient()->getEventManager().addListener(Lux::ServerMessageType::LOG_MESSAGE).bind<LogUI, &LogUI::onLogMessage>(this);
}


void LogUI::onLogMessage(Lux::Event& evt)
{
	float y = getContainer()->getChildCount() > 0 ? getContainer()->getChild(getContainer()->getChildCount() -1 )->getLocalArea().bottom : 0;
	Lux::LogEvent log_evt = static_cast<Lux::LogEvent&>(evt);
	Lux::UI::Block* cell = new Lux::UI::Block(getGui(), getContainer(), "_text");
	cell->setBlockText(log_evt.system.c_str());
	cell->setArea(0, 0, 0, y, 0.3f, 0, 0, y + 20);

	cell = new Lux::UI::Block(getGui(), getContainer(), "_text");
	cell->setBlockText(log_evt.message.c_str());
	cell->setArea(0.3f, 0, 0, y, 1, 0, 0, y + 20);
	layout();
}
