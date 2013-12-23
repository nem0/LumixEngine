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
	main_frame.getEditorClient()->getEventManager().registerListener(Lux::ServerMessageType::LOG_MESSAGE, this, &LogUI::onLogMessage);
}


void LogUI::onLogMessage(void* user_data, Lux::Event& evt)
{
	ASSERT(user_data);
	LogUI* that = static_cast<LogUI*>(user_data);
	Lux::UI::Block* container = that->getContainer();
	float y = container->getChildCount() > 0 ? container->getChild(container->getChildCount() -1 )->getLocalArea().bottom : 0;
	Lux::LogEvent log_evt = static_cast<Lux::LogEvent&>(evt);
	Lux::UI::Block* cell = new Lux::UI::Block(*that->getGui(), that->getContainer(), "_text");
	cell->setBlockText(log_evt.system.c_str());
	cell->setArea(0, 0, 0, y, 0.3f, 0, 0, y + 20);

	cell = new Lux::UI::Block(*that->getGui(), that->getContainer(), "_text");
	cell->setBlockText(log_evt.message.c_str());
	cell->setArea(0.3f, 0, 0, y, 1, 0, 0, y + 20);
	that->layout();
}
