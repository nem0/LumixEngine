#pragma once

class Notifications
{
	public:
		static Notifications* create(class MainWindow& main_window);
		static void destroy(Notifications* notifications);

		virtual void update(float time_delta) = 0;
		virtual void showNotification(const char* text) = 0;
		virtual int showProgressNotification(const char* text) = 0;
		virtual void setProgress(int id, int value) = 0;
		virtual void setNotificationTime(int id, float time) = 0;

	protected:
		Notifications() {}
		virtual ~Notifications() {}
};