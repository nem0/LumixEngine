#include "settings.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/log.h"
#include "engine/debug/debug.h"
#include "imgui/imgui.h"
#include "platform_interface.h"
#include "utils.h"
#include <cstdio>
#include <lua.hpp>
#include <SDL.h>


static const char DEFAULT_SETTINGS_PATH[] = "studio_default.ini";
static const char SETTINGS_PATH[] = "studio.ini";


static void loadStyle(lua_State* L)
{
	if (lua_getglobal(L, "style") == LUA_TTABLE)
	{
		auto& style = ImGui::GetStyle();
		#define LUMIX_READ_STYLE(name) \
			if (lua_getfield(L, -1, #name) == LUA_TTABLE) { \
				if (lua_rawgeti(L, -1, 1) == LUA_TNUMBER) style.Colors[ImGuiCol_##name].x = (float)lua_tonumber(L, -1); \
				if (lua_rawgeti(L, -2, 2) == LUA_TNUMBER) style.Colors[ImGuiCol_##name].y = (float)lua_tonumber(L, -1); \
				if (lua_rawgeti(L, -3, 3) == LUA_TNUMBER) style.Colors[ImGuiCol_##name].z = (float)lua_tonumber(L, -1); \
				if (lua_rawgeti(L, -4, 4) == LUA_TNUMBER) style.Colors[ImGuiCol_##name].w = (float)lua_tonumber(L, -1); \
				lua_pop(L, 4); \
			} \
			lua_pop(L, 1);

		LUMIX_READ_STYLE(Text);
		LUMIX_READ_STYLE(TextDisabled);
		LUMIX_READ_STYLE(WindowBg);
		LUMIX_READ_STYLE(ChildWindowBg);
		LUMIX_READ_STYLE(Border);
		LUMIX_READ_STYLE(BorderShadow);
		LUMIX_READ_STYLE(FrameBg);
		LUMIX_READ_STYLE(FrameBgHovered);
		LUMIX_READ_STYLE(FrameBgActive);
		LUMIX_READ_STYLE(TitleBg);
		LUMIX_READ_STYLE(TitleBgCollapsed);
		LUMIX_READ_STYLE(TitleBgActive);
		LUMIX_READ_STYLE(MenuBarBg);
		LUMIX_READ_STYLE(ScrollbarBg);
		LUMIX_READ_STYLE(ScrollbarGrab);
		LUMIX_READ_STYLE(ScrollbarGrabHovered);
		LUMIX_READ_STYLE(ScrollbarGrabActive);
		LUMIX_READ_STYLE(ComboBg);
		LUMIX_READ_STYLE(CheckMark);
		LUMIX_READ_STYLE(SliderGrab);
		LUMIX_READ_STYLE(SliderGrabActive);
		LUMIX_READ_STYLE(Button);
		LUMIX_READ_STYLE(ButtonHovered);
		LUMIX_READ_STYLE(ButtonActive);
		LUMIX_READ_STYLE(Header);
		LUMIX_READ_STYLE(HeaderHovered);
		LUMIX_READ_STYLE(HeaderActive);
		LUMIX_READ_STYLE(Column);
		LUMIX_READ_STYLE(ColumnHovered);
		LUMIX_READ_STYLE(ColumnActive);
		LUMIX_READ_STYLE(ResizeGrip);
		LUMIX_READ_STYLE(ResizeGripHovered);
		LUMIX_READ_STYLE(ResizeGripActive);
		LUMIX_READ_STYLE(CloseButton);
		LUMIX_READ_STYLE(CloseButtonHovered);
		LUMIX_READ_STYLE(CloseButtonActive);
		LUMIX_READ_STYLE(PlotLines);
		LUMIX_READ_STYLE(PlotLinesHovered);
		LUMIX_READ_STYLE(PlotHistogram);
		LUMIX_READ_STYLE(PlotHistogramHovered);
		LUMIX_READ_STYLE(TextSelectedBg);
		LUMIX_READ_STYLE(PopupBg);
		LUMIX_READ_STYLE(ModalWindowDarkening);

		#undef LUMIX_READ_STYLE
	}
	lua_pop(L, 1);
}


static void saveStyle(Lumix::FS::OsFile& file)
{
	auto& style = ImGui::GetStyle();
	file << "style = {";
	#define LUMIX_WRITE_STYLE(name) \
		file << #name << " = {" << style.Colors[ImGuiCol_##name].x \
			<< ", " << style.Colors[ImGuiCol_##name].y \
			<< ", " << style.Colors[ImGuiCol_##name].z \
			<< ", " << style.Colors[ImGuiCol_##name].w << "},\n"

	LUMIX_WRITE_STYLE(Text);
	LUMIX_WRITE_STYLE(TextDisabled);
	LUMIX_WRITE_STYLE(WindowBg);
	LUMIX_WRITE_STYLE(ChildWindowBg);
	LUMIX_WRITE_STYLE(Border);
	LUMIX_WRITE_STYLE(BorderShadow);
	LUMIX_WRITE_STYLE(FrameBg);
	LUMIX_WRITE_STYLE(FrameBgHovered);
	LUMIX_WRITE_STYLE(FrameBgActive);
	LUMIX_WRITE_STYLE(TitleBg);
	LUMIX_WRITE_STYLE(TitleBgCollapsed);
	LUMIX_WRITE_STYLE(TitleBgActive);
	LUMIX_WRITE_STYLE(MenuBarBg);
	LUMIX_WRITE_STYLE(ScrollbarBg);
	LUMIX_WRITE_STYLE(ScrollbarGrab);
	LUMIX_WRITE_STYLE(ScrollbarGrabHovered);
	LUMIX_WRITE_STYLE(ScrollbarGrabActive);
	LUMIX_WRITE_STYLE(ComboBg);
	LUMIX_WRITE_STYLE(CheckMark);
	LUMIX_WRITE_STYLE(SliderGrab);
	LUMIX_WRITE_STYLE(SliderGrabActive);
	LUMIX_WRITE_STYLE(Button);
	LUMIX_WRITE_STYLE(ButtonHovered);
	LUMIX_WRITE_STYLE(ButtonActive);
	LUMIX_WRITE_STYLE(Header);
	LUMIX_WRITE_STYLE(HeaderHovered);
	LUMIX_WRITE_STYLE(HeaderActive);
	LUMIX_WRITE_STYLE(Column);
	LUMIX_WRITE_STYLE(ColumnHovered);
	LUMIX_WRITE_STYLE(ColumnActive);
	LUMIX_WRITE_STYLE(ResizeGrip);
	LUMIX_WRITE_STYLE(ResizeGripHovered);
	LUMIX_WRITE_STYLE(ResizeGripActive);
	LUMIX_WRITE_STYLE(CloseButton);
	LUMIX_WRITE_STYLE(CloseButtonHovered);
	LUMIX_WRITE_STYLE(CloseButtonActive);
	LUMIX_WRITE_STYLE(PlotLines);
	LUMIX_WRITE_STYLE(PlotLinesHovered);
	LUMIX_WRITE_STYLE(PlotHistogram);
	LUMIX_WRITE_STYLE(PlotHistogramHovered);
	LUMIX_WRITE_STYLE(TextSelectedBg);
	LUMIX_WRITE_STYLE(PopupBg);
	LUMIX_WRITE_STYLE(ModalWindowDarkening);

	#undef LUMIX_WRITE_STYLE
	file << "}\n";
}


static void shortcutInput(int& shortcut)
{
	Lumix::StaticString<50> popup_name("");
	popup_name << (Lumix::i64)&shortcut;

	Lumix::StaticString<50> button_label(SDL_GetKeyName(SDL_GetKeyFromScancode((SDL_Scancode)shortcut)));
	button_label << "###" << (Lumix::i64)&shortcut;

	if (ImGui::Button(button_label, ImVec2(65, 0))) shortcut = -1;

	auto& io = ImGui::GetIO();
	int key_count;
	auto* state = SDL_GetKeyboardState(&key_count);
	if (ImGui::IsItemHovered())
	{
		for (int i = 0; i < key_count; ++i)
		{
			if (state[i])
			{
				shortcut = i;
				break;
			}
		}
	}
}


static int getIntegerField(lua_State* L, const char* name, int default_value)
{
	int value = default_value;
	if (lua_getfield(L, -1, name) == LUA_TNUMBER)
	{
		value = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);
	return value;
}


static float getFloat(lua_State* L, const char* name, float default_value)
{
	float value = default_value;
	if (lua_getglobal(L, name) == LUA_TNUMBER)
	{
		value = (float)lua_tonumber(L, -1);
	}
	lua_pop(L, 1);
	return value;
}


static bool getBoolean(lua_State* L, const char* name, bool default_value)
{
	bool value = default_value;
	if (lua_getglobal(L, name) == LUA_TBOOLEAN)
	{
		value = lua_toboolean(L, -1) != 0;
	}
	lua_pop(L, 1);
	return value;
}


static int getInteger(lua_State* L, const char* name, int default_value)
{
	int value = default_value;
	if (lua_getglobal(L, name) == LUA_TNUMBER)
	{
		value = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);
	return value;
}


Settings::Settings(StudioApp& app)
	: m_app(app)
	, m_is_opened(false)
	, m_editor(nullptr)
	, m_is_maximized(true)
	, m_is_entity_list_opened(false)
	, m_is_entity_template_list_opened(false)
	, m_is_asset_browser_opened(false)
	, m_is_log_opened(false)
	, m_is_profiler_opened(false)
	, m_is_properties_opened(false)
	, m_is_crash_reporting_enabled(true)
	, m_force_no_crash_report(false)
	, m_mouse_sensitivity_x(1000.0f)
	, m_mouse_sensitivity_y(1000.0f)
	, m_autosave_time(300)
{
	m_data_dir[0] = '\0';
	m_filter[0] = 0;
	m_window.x = m_window.y = 0;
	m_window.w = m_window.h = -1;

	m_state = luaL_newstate();
	luaL_openlibs(m_state);
	lua_newtable(m_state);
	lua_setglobal(m_state, "custom");
}


Settings::~Settings()
{
	lua_close(m_state);
}


bool Settings::load()
{
	auto L = m_state;
	bool has_settings = PlatformInterface::fileExists(SETTINGS_PATH);
	bool errors = luaL_loadfile(L, has_settings ? SETTINGS_PATH : DEFAULT_SETTINGS_PATH) != LUA_OK;
	errors = errors || lua_pcall(L, 0, 0, 0) != LUA_OK;
	if (errors)
	{
		Lumix::g_log_error.log("Editor") << SETTINGS_PATH << ": " << lua_tostring(L, -1);
		lua_pop(L, 1);
		return false;
	}

	if (lua_getglobal(L, "window") == LUA_TTABLE)
	{
		m_window.x = getIntegerField(L, "x", 0);
		m_window.y = getIntegerField(L, "y", 0);
		m_window.w = getIntegerField(L, "w", -1);
		m_window.h = getIntegerField(L, "h", -1);
	}
	lua_pop(L, 1);

	loadStyle(L);

	m_is_maximized = getBoolean(L, "maximized", true);
	
	m_is_opened = getBoolean(L, "settings_opened", false);
	m_is_asset_browser_opened = getBoolean(L, "asset_browser_opened", false);
	m_is_entity_list_opened = getBoolean(L, "entity_list_opened", false);
	m_is_entity_template_list_opened = getBoolean(L, "entity_template_list_opened", false);
	m_is_log_opened = getBoolean(L, "log_opened", false);
	m_is_profiler_opened = getBoolean(L, "profiler_opened", false);
	m_is_properties_opened = getBoolean(L, "properties_opened", false);
	m_is_crash_reporting_enabled = getBoolean(L, "error_reporting_enabled", true);
	Lumix::enableCrashReporting(m_is_crash_reporting_enabled && !m_force_no_crash_report);
	m_autosave_time = getInteger(L, "autosave_time", 300);
	m_mouse_sensitivity_x = getFloat(L, "mouse_sensitivity_x", 200.0f);
	m_mouse_sensitivity_y = getFloat(L, "mouse_sensitivity_y", 200.0f);

	if (!m_editor->getEngine().getPatchFileDevice())
	{
		if (lua_getglobal(L, "data_dir") == LUA_TSTRING) Lumix::copyString(m_data_dir, lua_tostring(L, -1));
		lua_pop(L, 1);
		m_editor->getEngine().setPatchPath(m_data_dir);
	}

	auto& actions = m_app.getActions();
	if (lua_getglobal(L, "actions") == LUA_TTABLE)
	{
		for (int i = 0; i < actions.size(); ++i)
		{
			if (lua_getfield(L, -1, actions[i]->name) == LUA_TTABLE)
			{
				for (int j = 0; j < Lumix::lengthOf(actions[i]->shortcut); ++j)
				{
					if (lua_rawgeti(L, -1, 1 + j) == LUA_TNUMBER)
					{
						actions[i]->shortcut[j] = (int)lua_tointeger(L, -1);
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	m_app.getToolbarActions().clear();
	if (lua_getglobal(L, "toolbar") == LUA_TTABLE)
	{
		int len = (int)lua_rawlen(L, -1);
		for (int i = 0; i < len; ++i)
		{
			if (lua_rawgeti(L, -1, i + 1) == LUA_TSTRING)
			{
				const char* action_name = lua_tostring(L, -1);
				Action* action = m_app.getAction(action_name);
				if(action) m_app.getToolbarActions().push(action);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	ImGui::LoadDock(L);
	return true;
}


void Settings::setValue(const char* name, bool value)
{
	lua_getglobal(m_state, "custom");
	lua_pushboolean(m_state, value);
	lua_setfield(m_state, -2, name);
	lua_pop(m_state, 1);
}


void Settings::setValue(const char* name, int value)
{
	lua_getglobal(m_state, "custom");
	lua_pushinteger(m_state, value);
	lua_setfield(m_state, -2, name);
	lua_pop(m_state, 1);
}


int Settings::getValue(const char* name, int default_value) const
{
	lua_getglobal(m_state, "custom");
	int v = getIntegerField(m_state, name, default_value);
	lua_pop(m_state, 1);
	return v;
}


bool Settings::getValue(const char* name, bool default_value) const
{
	bool v = default_value;
	lua_getglobal(m_state, "custom");
	if (lua_getfield(m_state, -1, name) == LUA_TBOOLEAN)
	{
		v = lua_toboolean(m_state, -1) != 0;
	}
	lua_pop(m_state, 2);
	return v;
}


bool Settings::save()
{
	auto& actions = m_app.getActions();
	Lumix::FS::OsFile file;
	auto& allocator = m_app.getWorldEditor()->getAllocator();
	if (!file.open(SETTINGS_PATH, Lumix::FS::Mode::CREATE_AND_WRITE, allocator)) return false;

	file << "window = { x = " << m_window.x 
		<< ", y = " << m_window.y 
		<< ", w = " << m_window.w
		<< ", h = " << m_window.h << " }\n";

	file << "maximized = " << (m_is_maximized ? "true" : "false") << "\n";

	auto writeBool = [&file](const char* name, bool value) {
		file << name << " = " << (value ? "true\n" : "false\n");
	};

	writeBool("settings_opened", m_is_opened);
	writeBool("asset_browser_opened", m_is_asset_browser_opened);
	writeBool("entity_list_opened", m_is_entity_list_opened);
	writeBool("entity_template_list_opened", m_is_entity_template_list_opened);
	writeBool("log_opened", m_is_log_opened);
	writeBool("profiler_opened", m_is_profiler_opened);
	writeBool("properties_opened", m_is_properties_opened);
	writeBool("error_reporting_enabled", m_is_crash_reporting_enabled);
	file << "mouse_sensitivity_x = " << m_mouse_sensitivity_x << "\n";
	file << "mouse_sensitivity_y = " << m_mouse_sensitivity_y << "\n";
	file << "autosave_time = " << m_autosave_time << "\n";
	
	saveStyle(file);

	file << "data_dir = \"";
	const char* c = m_data_dir;
	while (*c)
	{
		if (*c == '\\') file << "\\\\";
		else file << *c;
		++c;
	}
	file << "\"\n";

	file << "custom = {\n";
	lua_getglobal(m_state, "custom");
	lua_pushnil(m_state);
	bool first = true;
	while (lua_next(m_state, -2))
	{
		if (!first) file << ",\n";
		const char* name = lua_tostring(m_state, -2);
		switch (lua_type(m_state, -1))
		{
			case LUA_TBOOLEAN:
				file << name << " = " << (lua_toboolean(m_state, -1) != 0 ? "true" : "false");
				break;
			case LUA_TNUMBER:
				file << name << " = " << (int)lua_tonumber(m_state, -1);
				break;
			default:
				ASSERT(false);
				break;
		}
		lua_pop(m_state, 1);
		first = false;
	}
	lua_pop(m_state, 1);
	file << "}\n";

	file << "actions = {\n";
	for (int i = 0; i < actions.size(); ++i)
	{
		file << "\t" << actions[i]->name << " = {" 
			<< actions[i]->shortcut[0] << ", "
			<< actions[i]->shortcut[1] << ", " 
			<< actions[i]->shortcut[2] << "},\n";
	}
	file << "}\n";

	file << "toolbar = {\n";
	for (auto* action : m_app.getToolbarActions())
	{
		file << "\t\"" << action->name << "\",\n";
	}
	file << "}\n";

	ImGui::SaveDock(file);

	file.close();

	return true;
}



void Settings::showToolbarSettings()
{
	auto& actions = m_app.getToolbarActions();
	static Action* dragged = nullptr;
	
	ImVec4 tint_color = ImGui::GetStyle().Colors[ImGuiCol_Text];
	for (auto* action : actions)
	{
		ImGui::ImageButton(action->icon, ImVec2(24, 24), ImVec2(0, 0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), tint_color);
		if (dragged && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
		{
			actions.insert(actions.indexOf(action), dragged);
			dragged = nullptr;
			break;
		}
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging())
		{
			dragged = action;
			actions.eraseItem(action);
			break;
		}
		ImGui::SameLine();
	}
	ImGui::NewLine();

	if (dragged) ImGui::SetTooltip("%s", dragged->label);
	if (ImGui::IsMouseReleased(0)) dragged = nullptr;

	static int tmp = 0;
	auto getter = [](void* data, int idx, const char** out) -> bool {
		Action** tools = (Action**)data;
		*out = tools[idx]->label;
		return true;
	};
	Action* tools[1024];
	int count = 0;
	for (auto* action : m_app.getActions())
	{
		if (action->icon && action->is_global)
		{
			tools[count] = action;
			++count;
		}
	}
	ImGui::Combo("", &tmp, getter, tools, count);
	ImGui::SameLine();
	if (ImGui::Button("Add"))
	{
		actions.push(tools[tmp]);
	}
}


void Settings::showShortcutSettings()
{
	auto& actions = m_app.getActions();
	ImGui::FilterInput("Filter", m_filter, Lumix::lengthOf(m_filter));
	ImGui::Columns(4);
	ImGui::Text("Label");
	ImGui::NextColumn();
	ImGui::Text("Shortcut key 1");
	ImGui::NextColumn();
	ImGui::Text("Shortcut key 2");
	ImGui::NextColumn();
	ImGui::Text("Shortcut key 3");
	ImGui::NextColumn();
	ImGui::Separator();

	for (int i = 0; i < actions.size(); ++i)
	{
		Action& a = *actions[i];
		if (m_filter[0] == 0 || Lumix::stristr(a.label, m_filter) != 0)
		{
			ImGui::AlignFirstTextHeightToWidgets();
			ImGui::Text("%s", a.label);
			ImGui::NextColumn();
			shortcutInput(a.shortcut[0]);
			ImGui::NextColumn();
			shortcutInput(a.shortcut[1]);
			ImGui::NextColumn();
			shortcutInput(a.shortcut[2]);
			ImGui::NextColumn();
		}
	}
	ImGui::Columns(1);

}


void Settings::onGUI()
{
	if (ImGui::BeginDock("Settings", &m_is_opened))
	{
		if (ImGui::Button("Save")) save();
		ImGui::SameLine();
		if (ImGui::Button("Reload")) load();
		ImGui::SameLine();
		ImGui::Text("Settings are saved when the application closes");

		if (ImGui::CollapsingHeader("General"))
		{
			ImGui::DragInt("Autosave time (seconds)", &m_autosave_time);
			if (m_force_no_crash_report)
			{
				ImGui::Text("Crash reporting disabled from command line");
			}
			else
			{
				if (ImGui::Checkbox("Crash reporting", &m_is_crash_reporting_enabled))
				{
					Lumix::enableCrashReporting(m_is_crash_reporting_enabled);
				}
			}
			ImGui::DragFloat2("Mouse sensitivity", &m_mouse_sensitivity_x, 0.1f, 500.0f);

			ImGui::AlignFirstTextHeightToWidgets();
			ImGui::Text("%s", m_data_dir[0] != '\0' ? m_data_dir : "Not set");
			ImGui::SameLine();
			if (m_data_dir[0] != '\0')
			{
				if (ImGui::Button("Clear"))
				{
					m_data_dir[0] = '\0';
					m_editor->getEngine().setPatchPath(nullptr);
				}
				ImGui::SameLine();
			}
			if (ImGui::Button("Set data directory"))
			{
				if (PlatformInterface::getOpenDirectory(m_data_dir, sizeof(m_data_dir), nullptr))
				{
					m_editor->getEngine().setPatchPath(m_data_dir);
				}
			}
		}

		if (ImGui::CollapsingHeader("Shortcuts")) showShortcutSettings();
		if (ImGui::CollapsingHeader("Toolbar")) showToolbarSettings();
		if (ImGui::CollapsingHeader("Style"))
		{
			static int selected = 0;
			ImGui::Combo("Skin", &selected, "Light\0Dark\0");
			ImGui::SameLine();
			if (ImGui::Button("Apply"))
			{
				auto& style = ImGui::GetStyle();
				switch (selected)
				{
					case 0:
						style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
						style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
						style.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
						style.Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
						style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
						style.Colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
						style.Colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
						style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
						style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
						style.Colors[ImGuiCol_TitleBg] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
						style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);
						style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
						style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
						style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.98f, 0.98f, 0.98f, 0.53f);
						style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.69f, 0.69f, 0.69f, 0.80f);
						style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.49f, 0.49f, 0.49f, 0.80f);
						style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
						style.Colors[ImGuiCol_ComboBg] = ImVec4(0.86f, 0.86f, 0.86f, 0.99f);
						style.Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
						style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
						style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
						style.Colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
						style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
						style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
						style.Colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
						style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
						style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
						style.Colors[ImGuiCol_Column] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
						style.Colors[ImGuiCol_ColumnHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
						style.Colors[ImGuiCol_ColumnActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
						style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
						style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
						style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
						style.Colors[ImGuiCol_CloseButton] = ImVec4(0.59f, 0.59f, 0.59f, 0.50f);
						style.Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
						style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
						style.Colors[ImGuiCol_PlotLines] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
						style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
						style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
						style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
						style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
						style.Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.94f);
						style.Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
						break;
					case 1:
						style.Colors[ImGuiCol_Text] = ImVec4(0.91f, 0.91f, 0.91f, 1.00f);
						style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
						style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
						style.Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
						style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
						style.Colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
						style.Colors[ImGuiCol_FrameBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
						style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.75f, 0.42f, 0.02f, 0.40f);
						style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.75f, 0.42f, 0.02f, 0.67f);
						style.Colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
						style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
						style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
						style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
						style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
						style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 0.80f);
						style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.49f, 0.49f, 0.49f, 0.80f);
						style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
						style.Colors[ImGuiCol_ComboBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.99f);
						style.Colors[ImGuiCol_CheckMark] = ImVec4(0.75f, 0.42f, 0.02f, 1.00f);
						style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.75f, 0.42f, 0.02f, 0.78f);
						style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.75f, 0.42f, 0.02f, 1.00f);
						style.Colors[ImGuiCol_Button] = ImVec4(0.75f, 0.42f, 0.02f, 0.40f);
						style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.75f, 0.42f, 0.02f, 1.00f);
						style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.94f, 0.47f, 0.02f, 1.00f);
						style.Colors[ImGuiCol_Header] = ImVec4(0.75f, 0.42f, 0.02f, 0.31f);
						style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.75f, 0.42f, 0.02f, 0.80f);
						style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.75f, 0.42f, 0.02f, 1.00f);
						style.Colors[ImGuiCol_Column] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
						style.Colors[ImGuiCol_ColumnHovered] = ImVec4(0.75f, 0.42f, 0.02f, 0.78f);
						style.Colors[ImGuiCol_ColumnActive] = ImVec4(0.75f, 0.42f, 0.02f, 1.00f);
						style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
						style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.75f, 0.42f, 0.02f, 0.67f);
						style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.75f, 0.42f, 0.02f, 0.95f);
						style.Colors[ImGuiCol_CloseButton] = ImVec4(0.42f, 0.42f, 0.42f, 0.50f);
						style.Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.02f, 0.61f, 0.64f, 1.00f);
						style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.02f, 0.61f, 0.64f, 1.00f);
						style.Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
						style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.00f, 0.57f, 0.65f, 1.00f);
						style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.10f, 0.30f, 1.00f, 1.00f);
						style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.00f, 0.40f, 1.00f, 1.00f);
						style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.75f, 0.42f, 0.02f, 0.35f);
						style.Colors[ImGuiCol_PopupBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.94f);
						style.Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.06f, 0.06f, 0.06f, 0.35f);
						break;
				}
			}

			ImGui::ShowStyleEditor();
		}
	}
	ImGui::EndDock();
}
