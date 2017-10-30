#include "log_ui.h"
#include "editor/platform_interface.h"
#include "engine/log.h"
#include "imgui/imgui.h"


namespace Lumix
{


LogUI::LogUI(IAllocator& allocator)
	: m_allocator(allocator)
	, m_messages(allocator)
	, m_current_tab(Error)
	, m_notifications(allocator)
	, m_last_uid(1)
	, m_guard(false)
	, m_is_open(false)
	, m_are_notifications_hovered(false)
	, m_move_notifications_to_front(false)
{
	g_log_info.getCallback().bind<LogUI, &LogUI::onInfo>(this);
	g_log_error.getCallback().bind<LogUI, &LogUI::onError>(this);
	g_log_warning.getCallback().bind<LogUI, &LogUI::onWarning>(this);

	for (int i = 0; i < Count; ++i)
	{
		m_new_message_count[i] = 0;
		m_messages.emplace(allocator);
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
	m_messages[type].push(string(message, m_allocator));

	if (type == Error)
	{
		addNotification(message);
	}
}


void LogUI::onInfo(const char* system, const char* message)
{
	push(Info, message);
}


void LogUI::onWarning(const char* system, const char* message)
{
	push(Warning, message);
}


void LogUI::onError(const char* system, const char* message)
{
	push(Error, message);
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
	bool open;
	if (!ImGui::Begin("Notifications",
			&open,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_ShowBorders))
	{
		ImGui::End();
		return;
	}

	m_are_notifications_hovered = ImGui::IsWindowHovered();

	if (ImGui::Button("Close")) m_notifications.clear();

	if (m_move_notifications_to_front) ImGui::BringToFront();
	m_move_notifications_to_front = false;
	for (int i = 0; i < m_notifications.size(); ++i)
	{
		if (i > 0) ImGui::Separator();
		ImGui::Text("%s", m_notifications[i].message.c_str());
	}
	ImGui::End();
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
	return m_new_message_count[Error];
}


void LogUI::onGUI()
{
	MT::SpinLock lock(m_guard);
	showNotifications();

	if (ImGui::BeginDock("Log", &m_is_open))
	{
		const char* labels[] = { "Info", "Warning", "Error" };
		for (int i = 0; i < lengthOf(labels); ++i)
		{
			char label[40];
			fillLabel(label, sizeof(label), labels[i], m_new_message_count[i]);
			if(i > 0) ImGui::SameLine();
			if (ImGui::Button(label))
			{
				m_current_tab = i;
				m_new_message_count[i] = 0;
			}
		}
		
		auto* messages = &m_messages[m_current_tab];

		if (ImGui::Button("Clear"))
		{
			for (int i = 0; i < m_messages.size(); ++i)
			{
				m_messages[m_current_tab].clear();
				m_new_message_count[m_current_tab] = 0;
			}
		}

		ImGui::SameLine();
		char filter[128] = "";
		ImGui::LabellessInputText("Filter", filter, sizeof(filter));
		int len = 0;

		if (ImGui::BeginChild("log_messages", ImVec2(0, 0), true))
		{
			for (int i = 0; i < messages->size(); ++i)
			{
				const char* msg = (*messages)[i].c_str();
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
				for (int i = 0; i < messages->size(); ++i)
				{
					const char* msg = (*messages)[i].c_str();
					if (filter[0] == '\0' || strstr(msg, filter) != nullptr)
					{
						len += stringLength(msg);
					}
				}

				if (len > 0)
				{
					char* mem = (char*)m_allocator.allocate(len);
					mem[0] = '\0';
					for (int i = 0; i < messages->size(); ++i)
					{
						const char* msg = (*messages)[i].c_str();
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
			ImGui::EndPopup();
		}
	}
	ImGui::EndDock();
}


} // namespace Lumix
