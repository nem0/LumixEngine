#pragma once


#include <qobject.h>
#include <qvector.h>


class MainWindow;


class Notifications : public QObject
{
	Q_OBJECT
	public:
		Notifications(MainWindow& main_window);
		~Notifications();

		void update(float time_delta);
		int showProgressNotification(const char* text);
		void setProgress(int id, int value);
		void setNotificationTime(int id, float time);

	public slots:
		void showNotification(const char* text);

	signals:
		void notification(const char* text);

	private:
		class Notification
		{
		public:
			QWidget* m_widget;
			float m_time;
			int m_id;
		};

	private:
		void onLogInfo(const char*, const char* message);
		void onLogError(const char*, const char* message);
		void onLogWarning(const char*, const char* message);
		void updateLayout();

	private:
		MainWindow& m_main_window;
		QVector<Notification> m_items;
};