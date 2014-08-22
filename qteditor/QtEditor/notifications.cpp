#include "notifications.h"
#include "core/log.h"
#include "mainwindow.h"
#include <qlabel.h>


static const float DISPLAY_TIME = 5.0f;
static const int NOTIFICATION_WIDTH = 200;
static const int WIDGET_SPACING = 5;

class NotificationsImpl : public Notifications
{
	public:
		NotificationsImpl(MainWindow& main_window)
			: m_main_window(main_window)
		{
			Lumix::g_log_info.getCallback().bind<NotificationsImpl, &NotificationsImpl::onLogInfo>(this);
		}


		void onLogInfo(const char* system, const char* message)
		{
			showNotification(message);
		}


		virtual void update(float time_delta) override
		{
			for (int i = m_items.size() - 1; i >= 0; --i)
			{
				m_items[i].m_time -= time_delta;
				if (m_items[i].m_time < 0)
				{
					delete m_items[i].m_widget;
					m_items.eraseFast(i);
				}
			}
		}
		
		
		virtual void showNotification(const char* text) override
		{
			QWidget* widget = new QWidget(&m_main_window);
			widget->setObjectName("notification");
			QLabel* label = new QLabel(widget);
			label->setWordWrap(true);
			int w = m_main_window.width();
			int h = m_main_window.height();
			label->setContentsMargins(2, 2, 2, 2);

			if (m_items.empty())
			{
				widget->setGeometry(w - NOTIFICATION_WIDTH - WIDGET_SPACING, h - label->height() - WIDGET_SPACING, NOTIFICATION_WIDTH, label->height());
			}
			else
			{
				widget->setGeometry(w - NOTIFICATION_WIDTH - WIDGET_SPACING, m_items.back().m_widget->geometry().top() - label->height() - WIDGET_SPACING, NOTIFICATION_WIDTH, label->height());
			}
			
			widget->setWindowFlags(Qt::WindowStaysOnTopHint);
			label->setText(text);
			widget->show();
			
			Notification n;
			n.m_widget = widget;
			n.m_time = DISPLAY_TIME;
			m_items.push(n);
		}

	private:
		class Notification
		{
			public:
				QWidget* m_widget;
				float m_time;
		};

	private:
		MainWindow& m_main_window;
		Lumix::Array<Notification> m_items;
};


Notifications* Notifications::create(MainWindow& main_window)
{
	return new NotificationsImpl(main_window);
}


void Notifications::destroy(Notifications* notifications)
{
	delete notifications;
}
