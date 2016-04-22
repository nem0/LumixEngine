#pragma once


#include "engine/core/array.h"
#include "engine/core/mt/sync.h"
#include "engine/core/string.h"


class LUMIX_EDITOR_API LogUI
{
	public:
		explicit LogUI(Lumix::IAllocator& allocator);
		~LogUI();

		void onGUI();
		void update(float time_delta);
		int addNotification(const char* text);
		void setNotificationTime(int uid, float time);
		int getUnreadErrorCount() const;

	public:
		bool m_is_opened;

	private:
		enum Type
		{
			Info,
			Warning,
			Error,

			Count
		};

		struct Notification
		{
			explicit Notification(Lumix::IAllocator& alloc)
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
		bool m_are_notifications_hovered;
		Lumix::MT::SpinMutex m_guard;
};