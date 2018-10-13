#include "log_ui.h"
#include "editor/platform_interface.h"
#include "engine/log.h"
#include "imgui/imgui.h"


namespace Lumix
{


LogUI::LogUI(IAllocator& allocator)
	: m_allocator(allocator)
	, m_messages(allocator)
	, m_level_filter(2 | 4)
	, m_notifications(allocator)
	, m_last_uid(1)
	, m_is_open(false)
	, m_are_notifications_hovered(false)
	, m_move_notifications_to_front(false)
{
	g_log_info.getCallback().bind<LogUI, &LogUI::onInfo>(this);
	g_log_error.getCallback().bind<LogUI, &LogUI::onError>(this);
	g_log_warning.getCallback().bind<LogUI, &LogUI::onWarning>(this);

	for (int i = 0; i < COUNT; ++i)
	{
		m_new_message_count[i] = 0;
	}
}


LogUI::~LogUI()
{
	g_log_info.getCallback().unbind<LogUI, &LogUI::onInfo>(this);
	g_log_error.getCallback().unbind<LogUI, &LogUI::onError>(this);
	g_log_warning.getCallback().unbind<LogUI, &LogUI::onWarning>(this);
}


void LogUI::setNotificationTime(int uid, float time)
{
	for (auto& notif : m_notifications)
	{
		if (notif.uid == uid)
		{
			notif.time = time;
			break;
		}
	}
}


int LogUI::addNotification(const char* text)
{
	m_move_notifications_to_front = true;
	if (!m_notifications.empty() && m_notifications.back().message == text) return -1;
	auto& notif = m_notifications.emplace(m_allocator);
	notif.time = 10.0f;
	notif.message = text;
	notif.uid = ++m_last_uid;
	return notif.uid;
}


void LogUI::push(Type type, const char* message)
{
	MT::SpinLock lock(m_guard);
	++m_new_message_count[type];
	Message& msg = m_messages.emplace(m_allocator);
	msg.text = message;
	msg.type = type;

	if (type == ERROR)
	{
		addNotification(message);
	}
}


void LogUI::onInfo(const char* system, const char* message)
{
	push(INFO, message);
}


void LogUI::onWarning(const char* system, const char* message)
{
	push(WARNING, message);
}


void LogUI::onError(const char* system, const char* message)
{
	push(ERROR, message);
}


void fillLabel(char* output, int max_size, const char* label, int count)
{
	copyString(output, max_size, label);
	catString(output, max_size, "(");
	int len = stringLength(output);
	toCString(count, output + len, max_size - len);
	catString(output, max_size, ")###");
	catString(output, max_size, label);
}


void LogUI::showNotifications()
{
	m_are_notifications_hovered = false;
	if (m_notifications.empty()) return;

	ImGui::SetNextWindowPos(ImVec2(10, 30));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
	if (!ImGui::Begin("Notifications", nullptr, flags)) goto end;

	m_are_notifications_hovered = ImGui::IsWindowHovered();

	if (ImGui::Button("Close")) m_notifications.clear();

	if (m_move_notifications_to_front) ImGui::BringToFront();
	m_move_notifications_to_front = false;
	for (int i = 0; i < m_notifications.size(); ++i)
	{
		if (i > 0) ImGui::Separator();
		ImGui::Text("%s", m_notifications[i].message.c_str());
	}

	end:
		ImGui::End();
		ImGui::PopStyleVar();
}


void LogUI::update(float time_delta)
{
	if (m_are_notifications_hovered) return;

	for (int i = 0; i < m_notifications.size(); ++i)
	{
		m_notifications[i].time -= time_delta;

		if (m_notifications[i].time < 0)
		{
			m_notifications.erase(i);
			--i;
		}
	}
}


int LogUI::getUnreadErrorCount() const
{
	return m_new_message_count[ERROR];
}


void LogUI::onGUI()
{
	MT::SpinLock lock(m_guard);
	showNotifications();

	if (!m_is_open) return;
	if (ImGui::Begin("Log", &m_is_open))
	{
		const char* labels[] = { "Info", "Warning", "Error" };
		for (int i = 0; i < lengthOf(labels); ++i)
		{
			char label[40];
			fillLabel(label, sizeof(label), labels[i], m_new_message_count[i]);
			if(i > 0) ImGui::SameLine();
			bool b = m_level_filter & (1 << i);
			if (ImGui::Checkbox(label, &b))
			{
				if (b) m_level_filter |= 1 << i;
				else m_level_filter &= ~(1 << i);
				m_new_message_count[i] = 0;
			}
		}
		
		ImGui::SameLine();
		char filter[128] = "";
		ImGui::LabellessInputText("Filter", filter, sizeof(filter));
		int len = 0;

		if (ImGui::BeginChild("log_messages", ImVec2(0, 0), true))
		{
			for (int i = 0; i < m_messages.size(); ++i)
			{
				if ((m_level_filter & (1 << m_messages[i].type)) == 0) continue;
				const char* msg = m_messages[i].text.c_str();
				if (filter[0] == '\0' || strstr(msg, filter) != nullptr)
				{
					ImGui::TextUnformatted(msg);
				}
			}
		}
		ImGui::EndChild();
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) ImGui::OpenPopup("Context");
		if (ImGui::BeginPopup("Context"))
		{
			if (ImGui::Selectable("Copy"))
			{
				for (int i = 0; i < m_messages.size(); ++i)
				{
					const char* msg = m_messages[i].text.c_str();
					if (filter[0] == '\0' || strstr(msg, filter) != nullptr)
					{
						len += stringLength(msg);
					}
				}

				if (len > 0)
				{
					char* mem = (char*)m_allocator.allocate(len);
					mem[0] = '\0';
					for (int i = 0; i < m_messages.size(); ++i)
					{
						const char* msg = m_messages[i].text.c_str();
						if (filter[0] == '\0' || strstr(msg, filter) != nullptr)
						{
							catString(mem, len, msg);
							catString(mem, len, "\n");
						}
					}

					PlatformInterface::copyToClipboard(mem);
					m_allocator.deallocate(mem);
				}
			}
			if (ImGui::Selectable("Clear"))
			{
				Array<Message> filtered_messages(m_allocator);
				for (int i = 0; i < m_messages.size(); ++i)
				{
					if ((m_level_filter & (1 << m_messages[i].type)) != 0) continue;
					filtered_messages.emplace(m_messages[i]);
					m_new_message_count[m_messages[i].type] = 0;
				}
				m_messages.swap(filtered_messages);
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}


} // namespace Lumix
