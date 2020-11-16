#include <imgui/imgui.h>

#include "log_ui.h"
#include "engine/delegate_list.h"
#include "engine/log.h"
#include "engine/os.h"


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
	getLogCallback().bind<&LogUI::onLog>(this);

	for (int i = 0; i < (int)LogLevel::COUNT; ++i)
	{
		m_new_message_count[i] = 0;
	}
}


LogUI::~LogUI()
{
	getLogCallback().unbind<&LogUI::onLog>(this);
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


void LogUI::push(LogLevel level, const char* message)
{
	MutexGuard lock(m_guard);
	++m_new_message_count[(int)level];
	Message& msg = m_messages.emplace(m_allocator);
	msg.text = message;
	msg.level = level;

	if (m_autoscroll) m_scroll_to_bottom = true;
	if (level == LogLevel::ERROR) addNotification(message);
}


void LogUI::onLog(LogLevel level, const char* message)
{
	push(level, message);
}


void fillLabel(Span<char> output, const char* label, int count)
{
	copyString(output, label);
	catString(output, "(");
	int len = stringLength(output.begin());
	toCString(count, output.fromLeft(len));
	catString(output, ")###");
	catString(output, label);
}


void LogUI::showNotifications()
{
	m_are_notifications_hovered = false;
	if (m_notifications.empty()) return;

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos({vp->Pos.x + 10, vp->Pos.y + 30});
	ImGui::SetNextWindowSizeConstraints(ImVec2(-FLT_MAX, 0), ImVec2(FLT_MAX, 200));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
							 ImGuiWindowFlags_NoSavedSettings;
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
	return m_new_message_count[(int)LogLevel::ERROR];
}


void LogUI::onGUI()
{
	MutexGuard lock(m_guard);
	showNotifications();

	if (!m_is_open) return;
	if (ImGui::Begin(ICON_FA_COMMENT_ALT "Log##log", &m_is_open))
	{
		const char* labels[] = {"Info", "Warning", "Error"};
		for (u32 i = 0; i < lengthOf(labels); ++i)
		{
			char label[40];
			fillLabel(Span(label), labels[i], m_new_message_count[i]);
			if (i > 0) ImGui::SameLine();
			bool b = m_level_filter & (1 << i);
			if (ImGui::Checkbox(label, &b))
			{
				if (b)
					m_level_filter |= 1 << i;
				else
					m_level_filter &= ~(1 << i);
				m_new_message_count[i] = 0;
			}
		}

		ImGui::SameLine();
		char filter[128] = "";
		ImGui::Checkbox("Autoscroll", &m_autoscroll);
		const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
		ImGui::SetNextItemWidth(-w);
		ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));
		ImGui::SameLine();
		if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
			filter[0] = '\0';
		}
		int len = 0;

		if (ImGui::BeginChild("log_messages", ImVec2(0, 0), true))
		{
			for (int i = 0; i < m_messages.size(); ++i)
			{
				if ((m_level_filter & (1 << (int)m_messages[i].level)) == 0) continue;
				const char* msg = m_messages[i].text.c_str();
				if (filter[0] == '\0' || strstr(msg, filter) != nullptr)
				{
					ImGui::TextUnformatted(msg);
				}
			}
			if (m_scroll_to_bottom) {
				m_scroll_to_bottom = false;
				ImGui::SetScrollHereY();
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
					const Span<char> memspan(mem, len);
					for (int i = 0; i < m_messages.size(); ++i)
					{
						const char* msg = m_messages[i].text.c_str();
						if (filter[0] == '\0' || strstr(msg, filter) != nullptr)
						{
							catString(memspan, msg);
							catString(memspan, "\n");
						}
					}

					OS::copyToClipboard(mem);
					m_allocator.deallocate(mem);
				}
			}
			if (ImGui::Selectable("Clear"))
			{
				Array<Message> filtered_messages(m_allocator);
				for (int i = 0; i < m_messages.size(); ++i)
				{
					if ((m_level_filter & (1 << (int)m_messages[i].level)) == 0) {
						filtered_messages.emplace(m_messages[i]);
					}
					else {
						m_new_message_count[(int)m_messages[i].level] = 0;
					}
				}
				m_messages.swap(filtered_messages);
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}


} // namespace Lumix
