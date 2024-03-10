#pragma once

#include "core/allocators.h"
#include "core/array.h"
#include "core/log.h"
#include "core/sync.h"
#include "core/string.h"

#include "editor/studio_app.h"
#include "editor/utils.h"


namespace Lumix
{


struct LUMIX_EDITOR_API LogUI : StudioApp::GUIPlugin
{
	public:
		explicit LogUI(StudioApp& app, IAllocator& allocator);
		~LogUI();

		int addNotification(const char* text);
		int getUnreadErrorCount() const;

	private:
		struct Notification
		{
			explicit Notification(IAllocator& alloc)
				: message(alloc)
			{
			}
			float time;
			int uid;
			String message;
		};

		struct Message
		{
			Message(IAllocator& allocator)
			: text(allocator)
			{}

			String text;
			LogLevel level;
		};

		void onLog(LogLevel level, const char* message);
		void push(LogLevel level, const char* message);
		void showNotifications();
		void onSettingsLoaded() override;
		void onBeforeSettingsSaved() override;
		const char* getName() const override { return "log"; }
		void onGUI() override;
		void update(float time_delta) override;
		bool isOpen() const { return m_is_open; }
		void toggleUI() { m_is_open = !m_is_open; }

		TagAllocator m_allocator;
		StudioApp& m_app;
		Array<Message> m_messages;
		Array<Notification> m_notifications;
		int m_new_message_count[(int)LogLevel::COUNT];
		u8 m_level_filter;
		int m_last_uid;
		bool m_move_notifications_to_front;
		bool m_are_notifications_hovered;
		bool m_scroll_to_bottom = false;
		bool m_autoscroll = true;
		Mutex m_guard;
		bool m_is_open = false;
		bool m_focus_request = false;
		Action m_toggle_ui;
		TextFilter m_filter;
};


} // namespace Lumix
