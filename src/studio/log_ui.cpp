#include "log_ui.h"
#include "core/log.h"
#include "ocornut-imgui/imgui.h"


LogUI::LogUI(Lumix::IAllocator& allocator)
	: m_allocator(allocator)
	, m_messages(allocator)
	, m_current_tab(0)
	, m_notifications(allocator)
	, m_last_uid(1)
{
	m_is_opened = false;
	Lumix::g_log_info.getCallback().bind<LogUI, &LogUI::onInfo>(this);
	Lumix::g_log_error.getCallback().bind<LogUI, &LogUI::onError>(this);
	Lumix::g_log_warning.getCallback().bind<LogUI, &LogUI::onWarning>(this);

	for (int i = 0; i < Count; ++i)
	{
		m_new_message_count[i] = 0;
		m_messages.emplace(allocator);
	}
}


LogUI::~LogUI()
{
	Lumix::g_log_info.getCallback().unbind<LogUI, &LogUI::onInfo>(this);
	Lumix::g_log_error.getCallback().unbind<LogUI, &LogUI::onError>(this);
	Lumix::g_log_warning.getCallback().unbind<LogUI, &LogUI::onWarning>(this);
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
	auto& notif = m_notifications.emplace(m_allocator);
	notif.time = 10.0f;
	notif.message = text;
	notif.uid = ++m_last_uid;
	return notif.uid;
}


void LogUI::push(Type type, const char* message)
{
	++m_new_message_count[type];
	m_messages[type].push(Lumix::string(message, m_allocator));

	if (type == Error || type == Warning)
	{
		addNotification(message);
	}
}


void LogUI::onInfo(const char* system, const char* message)
{
	push(strcmp(system, "bgfx") == 0 ? BGFX : Info, message);
}


void LogUI::onWarning(const char* system, const char* message)
{
	push(strcmp(system, "bgfx") == 0 ? BGFX : Warning, message);
}


void LogUI::onError(const char* system, const char* message)
{
	push(strcmp(system, "bgfx") == 0 ? BGFX : Error, message);
}


void fillLabel(char* output, int max_size, const char* label, int count)
{
	Lumix::copyString(output, max_size, label);
	Lumix::catString(output, max_size, "(");
	int len = (int)strlen(output);
	Lumix::toCString(count, output + len, max_size - len);
	Lumix::catString(output, max_size, ")");
}


void LogUI::showNotifications()
{
	if (m_notifications.empty()) return;

	ImGui::SetNextWindowPos(ImVec2(10, 30));
	bool opened;
	if (!ImGui::Begin("Notifications",
					  &opened,
					  ImVec2(200, 0),
					  0.3f,
					  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
						  ImGuiWindowFlags_NoMove |
						  ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		return;
	}
	for (int i = 0; i < m_notifications.size(); ++i)
	{
		if (i > 0) ImGui::Separator();
		ImGui::Text(m_notifications[i].message.c_str());
	}
	ImGui::End();
}


void LogUI::update(float time_delta)
{
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


void LogUI::onGUI()
{
	showNotifications();

	if (!m_is_opened) return;

	if (ImGui::Begin("Log", &m_is_opened))
	{
		const char* labels[] = { "Info", "Warning", "Error", "BGFX" };
		for (int i = 0; i < Lumix::lengthOf(labels); ++i)
		{
			char label[20];
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
				m_messages[i].clear();
				m_new_message_count[i] = 0;
			}
		}

		ImGui::SameLine();
		char filter[128] = "";
		ImGui::InputText("Filter", filter, sizeof(filter));

		for (int i = 0; i < messages->size(); ++i)
		{
			const char* msg = (*messages)[i].c_str();
			if (filter[0] == '\0' || strstr(msg, filter) != nullptr)
			{
				ImGui::Text(msg);
			}
		}
	}
	ImGui::End();
}
