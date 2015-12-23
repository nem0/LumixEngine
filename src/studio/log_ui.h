#pragma once


#include "core/array.h"
#include "core/MT/sync.h"
#include "core/string.h"


class GUIInterface;


class LogUI
{
	public:
		LogUI(Lumix::IAllocator& allocator);
		~LogUI();

		void onGUI();
		void update(float time_delta);
		int addNotification(const char* text);
		void setNotificationTime(int uid, float time);
		void setGUIInterface(GUIInterface& gui);

	public:
		bool m_is_opened;

	private:
		enum Type
		{
			Info,
			Warning,
			Error,
			BGFX,

			Count
		};

		struct Notification
		{
			Notification(Lumix::IAllocator& alloc)
				: message(alloc)
			{
			}
			float time;
			int uid;
			Lumix::string message;
		};

	private:
		void onInfo(const char* system, const char* message);
		void onWarning(const char* system, const char* message);
		void onError(const char* system, const char* message);
		void push(Type type, const char* message);
		void showNotifications();

	private:
		Lumix::IAllocator& m_allocator;
		Lumix::Array<Lumix::Array<Lumix::string> > m_messages;
		Lumix::Array<Notification> m_notifications;
		int m_new_message_count[Count];
		int m_current_tab;
		int m_last_uid;
		bool m_move_notifications_to_front;
		Lumix::MT::SpinMutex m_guard;
		GUIInterface* m_gui;
};