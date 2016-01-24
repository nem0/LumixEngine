#include "settings.h"
#include "core/fs/os_file.h"
#include "core/log.h"
#include "debug/debug.h"
#include "imgui/imgui.h"
#include "platform_interface.h"
#include "utils.h"
#include <cstdio>
#include <lua.hpp>


static const char SETTINGS_PATH[] = "studio.ini";
static Settings* g_instance = nullptr;


static void shortcutInput(int& shortcut)
{
	StringBuilder<50> popup_name("");
	popup_name << (Lumix::int64)&shortcut;

	char key_string[30];
	PlatformInterface::getKeyName(shortcut, key_string, 30);

	StringBuilder<50> button_label(key_string);
	button_label << "###" << (Lumix::int64)&shortcut;

	if (ImGui::Button(button_label, ImVec2(50, 0))) shortcut = -1;

	auto& io = ImGui::GetIO();
	if (ImGui::IsItemHovered())
	{
		for (int i = 0; i < Lumix::lengthOf(ImGui::GetIO().KeysDown); ++i)
		{
			if (io.KeysDown[i])
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


Settings::Settings(Lumix::IAllocator& allocator)
	: m_allocator(allocator)
{
	ASSERT(!g_instance);
	g_instance = this;
	m_filter[0] = 0;
	m_is_maximized = true;
	m_window.x = m_window.y = 0;
	m_window.w = m_window.h = -1;
	m_is_entity_list_opened = false;
	m_is_entity_template_list_opened = false;
	m_is_asset_browser_opened = false;
	m_is_log_opened = false;
	m_is_profiler_opened = false;
	m_is_properties_opened = false;
	m_is_crash_reporting_enabled = true;

	m_autosave_time = 300;

	m_state = luaL_newstate();
	luaL_openlibs(m_state);
	lua_newtable(m_state);
	lua_setglobal(m_state, "custom");
}


Settings::~Settings()
{
	g_instance = nullptr;
	lua_close(m_state);
}


bool Settings::load(Action** actions, int actions_count)
{
	auto L = m_state;
	bool errors = luaL_loadfile(L, SETTINGS_PATH) != LUA_OK;
	errors = errors || lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK;
	if (errors)
	{
		Lumix::g_log_error.log("lua") << SETTINGS_PATH << ": " << lua_tostring(L, -1);
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

	m_is_maximized = getBoolean(L, "maximized", true);
	
	m_is_opened = getBoolean(L, "settings_opened", false);
	m_is_asset_browser_opened = getBoolean(L, "asset_browser_opened", false);
	m_is_entity_list_opened = getBoolean(L, "entity_list_opened", false);
	m_is_entity_template_list_opened = getBoolean(L, "entity_template_list_opened", false);
	m_is_log_opened = getBoolean(L, "log_opened", false);
	m_is_profiler_opened = getBoolean(L, "profiler_opened", false);
	m_is_properties_opened = getBoolean(L, "properties_opened", false);
	m_is_crash_reporting_enabled = getBoolean(L, "error_reporting_enabled", true);
	Lumix::enableCrashReporting(m_is_crash_reporting_enabled);
	m_autosave_time = getInteger(L, "autosave_time", 300);

	if (lua_getglobal(L, "actions") == LUA_TTABLE)
	{
		for (int i = 0; i < actions_count; ++i)
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

	ImGui::LoadDock(L);
	return true;
}


void Settings::setValue(const char* name, bool value)
{
	lua_getglobal(m_state, "custom");
	lua_pushboolean(m_state, value);
	lua_setfield(m_state, -2, name);
}


void Settings::setValue(const char* name, int value)
{
	lua_getglobal(m_state, "custom");
	lua_pushinteger(m_state, value);
	lua_setfield(m_state, -2, name);
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


Settings* Settings::getInstance()
{
	return g_instance;
}


bool Settings::save(Action** actions, int actions_count)
{
	Lumix::FS::OsFile file;
	if (!file.open(SETTINGS_PATH, Lumix::FS::Mode::WRITE | Lumix::FS::Mode::CREATE, m_allocator)) return false;

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
	file << "autosave_time = " << m_autosave_time << "\n";

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
	for (int i = 0; i < actions_count; ++i)
	{
		file << "\t" << actions[i]->name << " = {" 
			<< actions[i]->shortcut[0] << ", "
			<< actions[i]->shortcut[1] << ", " 
			<< actions[i]->shortcut[2] << "},\n";
	}
	file << "}\n";

	ImGui::SaveDock(file);

	file.close();

	return true;
}



void Settings::showShortcutSettings(Action** actions, int actions_count)
{
	ImGui::InputText("Filter", m_filter, Lumix::lengthOf(m_filter));
	ImGui::Columns(4);
	for (int i = 0; i < actions_count; ++i)
	{
		Action& a = *actions[i];
		if (m_filter[0] == 0 || Lumix::stristr(a.label, m_filter) != 0)
		{
			ImGui::Text(a.label);
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


void Settings::onGUI(Action** actions, int actions_count)
{
	if (ImGui::BeginDock("Settings", &m_is_opened))
	{
		if (ImGui::Button("Save")) save(actions, actions_count);
		ImGui::SameLine();
		if (ImGui::Button("Reload")) load(actions, actions_count);
		ImGui::SameLine();
		ImGui::Text("Settings are saved when the application closes");

		ImGui::DragInt("Autosave time (seconds)", &m_autosave_time);
		if (ImGui::Checkbox("Crash reporting", &m_is_crash_reporting_enabled))
		{
			Lumix::enableCrashReporting(m_is_crash_reporting_enabled);
		}

		if (ImGui::CollapsingHeader("Shortcuts")) showShortcutSettings(actions, actions_count);

		ImGui::ShowStyleEditor();
	}
	ImGui::EndDock();
}
