#pragma once


#include "engine/array.h"
#include "engine/mt/sync.h"
#include "engine/string.h"


namespace Lumix
{


class LUMIX_EDITOR_API LogUI
{
	public:
		explicit LogUI(IAllocator& allocator);
		~LogUI();

		void onGUI();
		void update(float time_delta);
		int addNotification(const char* text);
		void setNotificationTime(int uid, float time);
		int getUnreadErrorCount() const;

	public:
		bool m_is_open;

	private:
		enum Type
		{
			INFO,
			WARNING,
			ERROR,

			COUNT
		};

		struct Notification
		{
			explicit Notification(IAllocator& alloc)
				: message(alloc)
			{
			}
			float time;
			int uid;
			string message;
		};

	private:
		void onInfo(const char* system, const char* message);
		void onWarning(const char* system, const char* message);
		void onError(const char* system, const char* message);
		void push(Type type, const char* message);
		void showNotifications();

	private:
		struct Message
		{
			Message(IAllocator& allocator)
				: text(allocator)
			{}

			string text;
			Type type;
		};

		IAllocator& m_allocator;
		Array<Message> m_messages;
		Array<Notification> m_notifications;
		int m_new_message_count[COUNT];
		u8 m_level_filter;
		int m_last_uid;
		bool m_move_notifications_to_front;
		bool m_are_notifications_hovered;
		MT::SpinMutex m_guard;
};


} // namespace Lumix
