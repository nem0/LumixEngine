#include "editor_native/notifications.h"
#include <Windows.h>
#include "editor_native/main_frame.h"


Notifications::Notifications(MainFrame& main_frame)
	: Block(main_frame.getGui(), &main_frame, NULL)
{
	setArea(0, 0, 0, 0, 1, 0, 1, 0);
	setIsClickable(false);
}


void Notifications::showNotification(const char* text)
{
	Block* b = new Block(getGui(), this, "_box");
	b->setArea(1, -200, 1, -60 - m_notifications.size() * 60.0f, 1, -10, 1, -10 - m_notifications.size() * 60.0f);
	Block* t = new Block(getGui(), b, "_text");
	t->setArea(0, 0, 0, 0, 1, 0, 1, 0);
	t->setBlockText(text);
	b->setZIndex(999);
	layout();
	Notification n;
	n.m_ui = b;
	n.m_time = GetTickCount();
	m_notifications.push(n);
}


void Notifications::update()
{
	uint32_t t = GetTickCount();
	for(int i = m_notifications.size() - 1; i >= 0; --i)
	{
		if(t - m_notifications[i].m_time > 5000)
		{
			m_notifications[i].m_ui->destroy();
			m_notifications.eraseFast(i);
		}
	}
}
