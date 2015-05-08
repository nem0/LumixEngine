#include "notifications.h"
#include "core/log.h"
#include "mainwindow.h"
#include <qlabel.h>
#include <qlayout.h>
#include <qprogressbar.h>


static const float DISPLAY_TIME = 2.0f;
static const int NOTIFICATION_WIDTH = 200;
static const int WIDGET_SPACING = 5;

Notifications::Notifications(MainWindow& main_window)
	: m_main_window(main_window)
{
	//Lumix::g_log_info.getCallback().bind<Notifications, &Notifications::onLogInfo>(this);
	Lumix::g_log_warning.getCallback().bind<Notifications, &Notifications::onLogWarning>(this);
	Lumix::g_log_error.getCallback().bind<Notifications, &Notifications::onLogError>(this);
	m_main_window.connect(&m_main_window, &MainWindow::resized, [this](const QSize&)
	{
		updateLayout();
	});
}


Notifications::~Notifications()
{
	//Lumix::g_log_info.getCallback().unbind<Notifications, &Notifications::onLogInfo>(this);
	Lumix::g_log_warning.getCallback().unbind<Notifications, &Notifications::onLogWarning>(this);
	Lumix::g_log_error.getCallback().unbind<Notifications, &Notifications::onLogError>(this);
}

void Notifications::updateLayout()
{
	int y = m_main_window.height() - WIDGET_SPACING;
	for (int i = m_items.size() - 1; i >= 0; --i)
	{
		QWidget* widget = m_items[i].m_widget;
		y -= widget->height() + WIDGET_SPACING;
		widget->move(m_main_window.width() - NOTIFICATION_WIDTH - WIDGET_SPACING, y);
	}
}


void Notifications::onLogInfo(const char*, const char* message)
{
	emit notification(message);
}


void Notifications::onLogWarning(const char*, const char* message)
{
	emit notification(message);
}


void Notifications::onLogError(const char*, const char* message)
{
	emit notification(message);
}


void Notifications::update(float time_delta)
{
	if (!m_items.empty())
	{
		m_items[0].m_time -= time_delta;
		if (m_items[0].m_time < 0)
		{
			delete m_items[0].m_widget;
			m_items.removeAt(0);
			updateLayout();
		}
	}
}
		

void Notifications::setProgress(int id, int value)
{
	for (auto iter = m_items.begin(); iter != m_items.end(); ++iter)
	{
		if (iter->m_id == id)
		{
			ASSERT(iter->m_widget->children().size() > 1);
			if (iter->m_widget->children().size() > 1)
			{
				QProgressBar* progress = qobject_cast<QProgressBar*>(iter->m_widget->children()[1]);
				ASSERT(progress);
				if (progress)
				{
					progress->setValue(value);
				}
				break;
			}
		}
	}
}


void Notifications::setNotificationTime(int id, float time)
{
	for (auto iter = m_items.begin(); iter != m_items.end(); ++iter)
	{
		if (iter->m_id == id)
		{
			iter->m_time = time;
			break;
		}
	}
}


int Notifications::showProgressNotification(const char* text)
{
	QWidget* widget = new QWidget(&m_main_window);
	widget->setObjectName("notification");
	QVBoxLayout* layout = new QVBoxLayout(widget);
	widget->setLayout(layout);
			
	QProgressBar* progress = new QProgressBar(widget);
	progress->setValue(0);
	progress->setMaximum(100);
	layout->addWidget(progress);
			
	QLabel* label = new QLabel(widget);
	label->setMinimumWidth(NOTIFICATION_WIDTH);
	label->setContentsMargins(2, 2, 2, 2);
	label->setText(text);
	label->setWordWrap(true);
	layout->addWidget(label);

	widget->show();
	widget->raise();
	widget->adjustSize();
			
	Notification n;
	n.m_id = m_items.empty() ? 0 : m_items.back().m_id + 1;
	n.m_widget = widget;
	n.m_time = FLT_MAX;
	m_items.push_back(n);

	updateLayout();
	return n.m_id;
}

		
void Notifications::showNotification(const char* text)
{
	QWidget* widget = new QWidget(&m_main_window);
	widget->setObjectName("notification");
	QLabel* label = new QLabel(widget);
	label->setMinimumWidth(NOTIFICATION_WIDTH);
	label->setContentsMargins(2, 2, 2, 2);
	label->setText(text);
	label->setWordWrap(true);
	widget->show();
	widget->raise();
	widget->adjustSize();

	Notification n;
	n.m_id = m_items.empty() ? 0 : m_items.back().m_id + 1;
	n.m_widget = widget;
	n.m_time = DISPLAY_TIME;
	m_items.push_back(n);

	updateLayout();
}

