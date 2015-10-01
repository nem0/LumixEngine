#include "settings.h"
#include "core/log.h"
#include "ocornut-imgui/imgui.h"
#include "utils.h"
#include <cstdio>
#include <lua.hpp>


static const char SETTINGS_PATH[] = "studio.ini";


static void shortcutInput(int& shortcut)
{
	StringBuilder<50> popup_name("");
	popup_name << (int64_t)&shortcut;

	char key_string[30];
	getKeyName(shortcut, key_string, 30);

	StringBuilder<50> button_label(key_string);
	button_label << "##" << (int64_t)&shortcut;

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


Settings::Settings()
{
	m_filter[0] = 0;
	m_is_maximized = true;
	m_window.x = m_window.y = 0;
	m_window.w = m_window.h = -1;
	m_is_gameview_opened = false;
	m_is_entity_list_opened = false;
	m_is_entity_template_list_opened = false;
	m_is_style_editor_opened = false;
	m_is_asset_browser_opened = false;
	m_is_hierarchy_opened = false;
	m_is_log_opened = false;
	m_is_profiler_opened = false;
	m_is_properties_opened = false;

	m_autosave_time = 300;
}


bool Settings::load(Action** actions, int actions_count)
{
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

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
	
	m_is_asset_browser_opened = getBoolean(L, "asset_browser_opened", false);
	m_is_entity_list_opened = getBoolean(L, "entity_list_opened", false);
	m_is_entity_template_list_opened = getBoolean(L, "entity_template_list_opened", false);
	m_is_gameview_opened = getBoolean(L, "gameview_opened", false);
	m_is_hierarchy_opened = getBoolean(L, "hierarchy_opened", false);
	m_is_log_opened = getBoolean(L, "log_opened", false);
	m_is_profiler_opened = getBoolean(L, "profiler_opened", false);
	m_is_properties_opened = getBoolean(L, "properties_opened", false);
	m_is_style_editor_opened = getBoolean(L, "style_editor_opened", false);
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

	lua_close(L);
	return true;
}


bool Settings::save(Action** actions, int actions_count)
{
	FILE* fp = fopen(SETTINGS_PATH, "wb");
	if (!fp) return false;

	fprintf(fp,
		"window = { x = %d, y = %d, w = %d, h = %d }\n",
		m_window.x,
		m_window.y,
		m_window.w,
		m_window.h);

	fprintf(fp, "maximized = %s\n", m_is_maximized ? "true" : "false");

	auto writeBool = [fp](const char* name, bool value) {
		fputs(name, fp);
		fputs(" = ", fp);
		fputs(value ? "true\n" : "false\n", fp);
	};

	writeBool("asset_browser_opened", m_is_asset_browser_opened);
	writeBool("entity_list_opened", m_is_entity_list_opened);
	writeBool("entity_template_list_opened", m_is_entity_template_list_opened);
	writeBool("gameview_opened", m_is_gameview_opened);
	writeBool("hierarchy_opened", m_is_hierarchy_opened);
	writeBool("log_opened", m_is_log_opened);
	writeBool("profiler_opened", m_is_profiler_opened);
	writeBool("properties_opened", m_is_properties_opened);
	writeBool("style_editor_opened", m_is_style_editor_opened);
	fprintf(fp, "autosave_time = %d\n", m_autosave_time);

	fputs("actions = {\n", fp);
	for (int i = 0; i < actions_count; ++i)
	{
		fputs("\t", fp);
		fputs(actions[i]->name, fp);
		fputs(" = {", fp);
		fprintf(fp,
			"%d, %d, %d",
			actions[i]->shortcut[0],
			actions[i]->shortcut[1],
			actions[i]->shortcut[2]);
		fputs("},\n", fp);
	}
	fputs("}\n", fp);
	fclose(fp);

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


void Settings::onGui(Action** actions, int actions_count)
{
	if (!m_is_opened) return;

	if (ImGui::Begin("Settings", &m_is_opened))
	{
		if (ImGui::Button("Save")) save(actions, actions_count);
		ImGui::SameLine();
		if (ImGui::Button("Reload")) load(actions, actions_count);
		ImGui::SameLine();
		ImGui::Text("Settings are saved when the application closes");

		ImGui::DragInt("Autosave time (seconds)", &m_autosave_time);

		if (ImGui::CollapsingHeader("Shortcuts")) showShortcutSettings(actions, actions_count);
	}
	ImGui::End();
}
