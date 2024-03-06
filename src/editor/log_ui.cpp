#include <imgui/imgui.h>

#include "core/delegate_list.h"
#include "core/log.h"
#include "core/os.h"

#include "log_ui.h"
#include "editor/settings.h"


namespace Lumix
{


LogUI::LogUI(StudioApp& app, IAllocator& allocator)
	: m_allocator(allocator, "log ui")
	, m_app(app)
	, m_messages(m_allocator)
	, m_level_filter(2 | 4)
	, m_notifications(m_allocator)
	, m_last_uid(1)
	, m_is_open(false)
	, m_are_notifications_hovered(false)
	, m_move_notifications_to_front(false)
{
	registerLogCallback<&LogUI::onLog>(this);

	for (int i = 0; i < (int)LogLevel::COUNT; ++i)
	{
		m_new_message_count[i] = 0;
	}

	m_toggle_ui.init("Log", "Toggle Log UI", "log", "", Action::IMGUI_PRIORITY);
	m_toggle_ui.func.bind<&LogUI::toggleUI>(this);
	m_toggle_ui.is_selected.bind<&LogUI::isOpen>(this);

	m_app.addWindowAction(&m_toggle_ui);
}


LogUI::~LogUI()
{
	m_app.removeAction(&m_toggle_ui);
	unregisterLogCallback<&LogUI::onLog>(this);
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
	float w = maximum(vp->Size.x * 0.25f, 300.f);
	float h = maximum(vp->Size.y * 0.15f, 100.f);
	ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x - 30 - w, vp->Pos.y + vp->Size.y - 30 - h});
	ImGui::SetNextWindowSize(ImVec2(w, h));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
							 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
	if (!ImGui::Begin("Notifications", nullptr, flags)) goto end;

	m_are_notifications_hovered = ImGui::IsWindowHovered();

	ImGui::PushFont(m_app.getBigIconFont());
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored(ImColor(255, 0, 0, 255), ICON_FA_EXCLAMATION_TRIANGLE);
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TIMES, nullptr)) m_notifications.clear();
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_SQUARE_ALT, nullptr)) m_focus_request = true;
	ImGui::PopFont();
	if (ImGui::BeginChild("scrollarea", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground)) {
		if (m_move_notifications_to_front) ImGuiEx::BringToFront();
		m_move_notifications_to_front = false;
		for (int i = 0; i < m_notifications.size(); ++i)
		{
			if (i > 0) ImGui::Separator();
			ImGuiEx::TextUnformatted(m_notifications[i].message);
		}
	}
	ImGui::EndChild();

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

void LogUI::onSettingsLoaded() { m_is_open = m_app.getSettings().m_is_log_open; }
void LogUI::onBeforeSettingsSaved() { m_app.getSettings().m_is_log_open  = m_is_open; }

void LogUI::onGUI()
{
	MutexGuard lock(m_guard);
	showNotifications();

	if (m_focus_request) {
		ImGui::SetNextWindowFocus();
		m_is_open = true;
		m_focus_request = false;
	}
	if (!m_is_open) return;
	if (ImGui::Begin(ICON_FA_COMMENT_ALT "Log##log", &m_is_open)) {
		if (ImGuiEx::IconButton(ICON_FA_COG, "Settings")) ImGui::OpenPopup("Settings");
		if (ImGui::BeginPopup("Settings")) {
			const char* labels[] = {"Info", "Warning", "Error"};
			for (u32 i = 0; i < lengthOf(labels); ++i)
			{
				char label[40];
				fillLabel(Span(label), labels[i], m_new_message_count[i]);
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
			ImGui::Checkbox("Autoscroll", &m_autoscroll);
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		m_filter.gui(ICON_FA_SEARCH " Filter");
		int len = 0;

		if (ImGui::BeginChild("log_messages", ImVec2(0, 0), true))
		{
			for (int i = 0; i < m_messages.size(); ++i)
			{
				if ((m_level_filter & (1 << (int)m_messages[i].level)) == 0) continue;
				const char* msg = m_messages[i].text.c_str();
				if (m_filter.pass(msg)) {
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
					if (m_filter.pass(msg)) {
						len += stringLength(msg);
						len += sizeof("\n");
					}
				}

				if (len > 0) {
					OutputMemoryStream mem(m_allocator);
					mem.resize(len);
					mem[0] = '\0';
					const Span<char> memspan((char*)mem.getMutableData(), len);
					for (int i = 0; i < m_messages.size(); ++i) {
						const char* msg = m_messages[i].text.c_str();
						if (m_filter.pass(msg)) {
							catString(memspan, msg);
							catString(memspan, "\n");
						}
					}

					os::copyToClipboard((const char*)mem.data());
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
