#include <imgui/imgui.h>

#include "settings.h"
#include "engine/debug.h"
#include "engine/file_system.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/os.h"
#include "editor/studio_app.h"
#include "editor/world_editor.h"
#include "utils.h"
#include <lua.hpp>


namespace Lumix
{


static const char DEFAULT_SETTINGS_PATH[] = "studio_default.ini";
static const char SETTINGS_PATH[] = "studio.ini";


static void loadStyle(lua_State* L)
{
	lua_getglobal(L, "style");
	if (lua_type(L, -1) == LUA_TTABLE)
	{
		auto& style = ImGui::GetStyle();
		for (int i = 0; i < ImGuiCol_COUNT; ++i)
		{
			const char* name = ImGui::GetStyleColorName(i);
			lua_getfield(L, -1, name);
			if (lua_type(L, -1) == LUA_TTABLE)
			{
				lua_rawgeti(L, -1, 1);
				if (lua_type(L, -1) == LUA_TNUMBER) style.Colors[i].x = (float)lua_tonumber(L, -1);
				lua_rawgeti(L, -2, 2);
				if (lua_type(L, -1) == LUA_TNUMBER) style.Colors[i].y = (float)lua_tonumber(L, -1);
				lua_rawgeti(L, -3, 3);
				if (lua_type(L, -1) == LUA_TNUMBER) style.Colors[i].z = (float)lua_tonumber(L, -1);
				lua_rawgeti(L, -4, 4);
				if (lua_type(L, -1) == LUA_TNUMBER) style.Colors[i].w = (float)lua_tonumber(L, -1);
				lua_pop(L, 4);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}


static void saveStyle(OS::OutputFile& file)
{
	auto& style = ImGui::GetStyle();
	file << "style = {";
	for (int i = 0; i < ImGuiCol_COUNT; ++i)
	{
		file << ImGui::GetStyleColorName(i) << " = {" << style.Colors[i].x
			<< ", " << style.Colors[i].y
			<< ", " << style.Colors[i].z
			<< ", " << style.Colors[i].w << "},\n";
	}
	file << "}\n";
}


static void shortcutInput(OS::Keycode& shortcut)
{
	StaticString<50> popup_name("");
	popup_name << (i64)&shortcut;

	char tmp[32];
	OS::getKeyName(shortcut, Span(tmp));
	StaticString<50> button_label(tmp[0] || shortcut == OS::Keycode::INVALID ? tmp : "Unknown");
	button_label << "###" << (i64)&shortcut;

	if (ImGui::Button(button_label, ImVec2(65, 0))) shortcut = OS::Keycode::INVALID;

	if (ImGui::IsItemHovered()) {
		for (int i = 0; i < (int)OS::Keycode::MAX; ++i) {
			const auto kc= (OS::Keycode)i;
			const bool is_mouse = kc == OS::Keycode::LBUTTON || kc == OS::Keycode::RBUTTON || kc == OS::Keycode::MBUTTON;
			if (OS::isKeyDown(kc) && !is_mouse) {
				shortcut = (OS::Keycode)i;
				break;
			}
		}
	}
}


static int getIntegerField(lua_State* L, const char* name, int default_value)
{
	int value = default_value;
	lua_getfield(L, -1, name);
	if (lua_type(L, -1) == LUA_TNUMBER)
	{
		value = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);
	return value;
}


static float getFloat(lua_State* L, const char* name, float default_value)
{
	float value = default_value;
	lua_getglobal(L, name);
	if (lua_type(L, -1) == LUA_TNUMBER)
	{
		value = (float)lua_tonumber(L, -1);
	}
	lua_pop(L, 1);
	return value;
}


static bool getBoolean(lua_State* L, const char* name, bool default_value)
{
	bool value = default_value;
	lua_getglobal(L, name);
	if (lua_type(L, -1) == LUA_TBOOLEAN)
	{
		value = lua_toboolean(L, -1) != 0;
	}
	lua_pop(L, 1);
	return value;
}


static int getInteger(lua_State* L, const char* name, int default_value)
{
	int value = default_value;
	lua_getglobal(L, name);
	if (lua_type(L, -1) == LUA_TNUMBER)
	{
		value = (int)lua_tointeger(L, -1);
	}
	lua_pop(L, 1);
	return value;
}


Settings::Settings(StudioApp& app)
	: m_app(app)
	, m_is_open(false)
	, m_is_maximized(true)
	, m_is_entity_list_open(false)
	, m_is_entity_template_list_open(false)
	, m_is_asset_browser_open(false)
	, m_is_log_open(false)
	, m_is_profiler_open(false)
	, m_is_properties_open(false)
	, m_is_crash_reporting_enabled(true)
	, m_force_no_crash_report(false)
	, m_mouse_sensitivity(80.0f, 80.0f)
	, m_font_size(13)
{
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
	OS::InputFile file;
	FileSystem& fs = m_app.getEngine().getFileSystem();
	const bool has_settings = fs.fileExists(SETTINGS_PATH);
	const char* path = has_settings ? SETTINGS_PATH : DEFAULT_SETTINGS_PATH;
	
	Array<u8> buf(m_app.getAllocator());
	if (!fs.getContentSync(Path(path), Ref(buf))) {
		logError("Editor") << "Failed to open " << path;
		return false;
	}

	Span<const char> content((const char*)buf.begin(), buf.size());
	if (!LuaWrapper::execute(L, content, "settings", 0)) {
		return false;
	}

	lua_getglobal(L, "window");
	if (lua_type(L, -1) == LUA_TTABLE)
	{
		m_window.x = getIntegerField(L, "x", 0);
		m_window.y = getIntegerField(L, "y", 0);
		m_window.w = getIntegerField(L, "w", -1);
		m_window.h = getIntegerField(L, "h", -1);
	}
	lua_pop(L, 1);

	loadStyle(L);

	m_is_maximized = getBoolean(L, "maximized", true);
	
	m_is_open = getBoolean(L, "settings_opened", false);
	m_is_asset_browser_open = getBoolean(L, "asset_browser_opened", false);
	m_asset_browser_left_column_width = getFloat(L, "asset_browser_left_column_width", 100.f);
	m_is_entity_list_open = getBoolean(L, "entity_list_opened", false);
	m_is_entity_template_list_open = getBoolean(L, "entity_template_list_opened", false);
	m_is_log_open = getBoolean(L, "log_opened", false);
	m_is_profiler_open = getBoolean(L, "profiler_opened", false);
	m_is_properties_open = getBoolean(L, "properties_opened", false);
	m_is_crash_reporting_enabled = getBoolean(L, "error_reporting_enabled", true);
	enableCrashReporting(m_is_crash_reporting_enabled && !m_force_no_crash_report);
	m_mouse_sensitivity.x = getFloat(L, "mouse_sensitivity_x", 200.0f);
	m_mouse_sensitivity.y = getFloat(L, "mouse_sensitivity_y", 200.0f);
	m_app.setFOV(degreesToRadians(getFloat(L, "fov", 60)));
	m_font_size = getInteger(L, "font_size", 13);

	auto& actions = m_app.getActions();
	lua_getglobal(L, "actions");
	if (lua_type(L, -1) == LUA_TTABLE)
	{
		for (int i = 0; i < actions.size(); ++i)
		{
			lua_getfield(L, -1, actions[i]->name);
			if (lua_type(L, -1) == LUA_TTABLE)
			{
				for (u32 j = 0; j < lengthOf(actions[i]->shortcut); ++j)
				{
					lua_rawgeti(L, -1, 1 + j);
					if (lua_type(L, -1) == LUA_TNUMBER)
					{
						actions[i]->shortcut[j] = (OS::Keycode)lua_tointeger(L, -1);
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	m_app.getToolbarActions().clear();
	lua_getglobal(L, "toolbar");
	if (lua_type(L, -1) == LUA_TTABLE)
	{
		LuaWrapper::forEachArrayItem<const char*>(L, -1, nullptr, [this](const char* action_name){
			Action* action = m_app.getAction(action_name);
			if(action) m_app.getToolbarActions().push(action);
		});
	}
	lua_pop(L, 1);

	return true;
}


void Settings::setValue(const char* name, bool value) const
{
	lua_getglobal(m_state, "custom");
	lua_pushboolean(m_state, value);
	lua_setfield(m_state, -2, name);
	lua_pop(m_state, 1);
}


void Settings::setValue(const char* name, int value) const
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
	lua_getfield(m_state, -1, name);
	if (lua_type(m_state, -1) == LUA_TBOOLEAN)
	{
		v = lua_toboolean(m_state, -1) != 0;
	}
	lua_pop(m_state, 2);
	return v;
}


bool Settings::save()
{
	auto& actions = m_app.getActions();
	OS::OutputFile file;
	FileSystem& fs = m_app.getEngine().getFileSystem();
	if (!fs.open(SETTINGS_PATH, Ref(file))) return false;

	file << "window = { x = " << m_window.x 
		<< ", y = " << m_window.y 
		<< ", w = " << m_window.w
		<< ", h = " << m_window.h << " }\n";

	file << "maximized = " << (m_is_maximized ? "true" : "false") << "\n";
	file << "fov = " << radiansToDegrees(m_app.getFOV()) << "\n";

	auto writeBool = [&file](const char* name, bool value) {
		file << name << " = " << (value ? "true\n" : "false\n");
	};

	writeBool("settings_opened", m_is_open);
	writeBool("asset_browser_opened", m_is_asset_browser_open);
	writeBool("entity_list_opened", m_is_entity_list_open);
	writeBool("entity_template_list_opened", m_is_entity_template_list_open);
	writeBool("log_opened", m_is_log_open);
	writeBool("profiler_opened", m_is_profiler_open);
	writeBool("properties_opened", m_is_properties_open);
	writeBool("error_reporting_enabled", m_is_crash_reporting_enabled);
	file << "mouse_sensitivity_x = " << m_mouse_sensitivity.x << "\n";
	file << "mouse_sensitivity_y = " << m_mouse_sensitivity.y << "\n";
	file << "font_size = " << m_font_size << "\n";
	file << "asset_browser_left_column_width = " << m_asset_browser_left_column_width << "\n";
	
	saveStyle(file);

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
			<< (int)actions[i]->shortcut[0] << ", "
			<< (int)actions[i]->shortcut[1] << ", " 
			<< (int)actions[i]->shortcut[2] << "},\n";
	}
	file << "}\n";

	file << "toolbar = {\n";
	for (auto* action : m_app.getToolbarActions())
	{
		file << "\t\"" << action->name << "\",\n";
	}
	file << "}\n";

	file.close();

	return true;
}



void Settings::showToolbarSettings() const
{
	auto& actions = m_app.getToolbarActions();
	static Action* dragged = nullptr;
	
	ImVec4 tint_color = ImGui::GetStyle().Colors[ImGuiCol_Text];
	for (auto* action : actions)
	{
		int* t = (int*)action->icon;
		ImGui::ImageButton((void*)(uintptr_t)*t, ImVec2(24, 24), ImVec2(0, 0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), tint_color);
		if (dragged && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0))
		{
			actions.insert(actions.indexOf(action), dragged);
			dragged = nullptr;
			break;
		}
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			dragged = action;
			actions.eraseItem(action);
			break;
		}
		ImGui::SameLine();
	}
	ImGui::NewLine();

	if (dragged) ImGui::SetTooltip("%s", dragged->label_long.data);
	if (ImGui::IsMouseReleased(0)) dragged = nullptr;

	static int tmp = 0;
	auto getter = [](void* data, int idx, const char** out) -> bool {
		Action** tools = (Action**)data;
		*out = tools[idx]->label_long;
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
	ImGui::Combo("##tool_combo", &tmp, getter, tools, count);
	ImGui::SameLine();
	if (ImGui::Button("Add"))
	{
		actions.push(tools[tmp]);
	}
}


void Settings::showShortcutSettings()
{
	auto& actions = m_app.getActions();
	ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));
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
		if (m_filter[0] == 0 || stristr(a.label_long, m_filter) != 0)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%s", a.label_long.data);
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
	if (!m_is_open) return;
	if (ImGui::Begin("Settings", &m_is_open))
	{
		if (ImGui::Button("Save")) save();
		ImGui::SameLine();
		if (ImGui::Button("Reload")) load();
		ImGui::SameLine();
		ImGui::Text("Settings are saved when the application closes");

		if (ImGui::CollapsingHeader("General"))
		{
			if (m_force_no_crash_report)
			{
				ImGui::Text("Crash reporting disabled from command line");
			}
			else
			{
				if (ImGui::Checkbox("Crash reporting", &m_is_crash_reporting_enabled))
				{
					enableCrashReporting(m_is_crash_reporting_enabled);
				}
			}
			ImGui::DragFloat2("Mouse sensitivity", &m_mouse_sensitivity.x, 0.1f, 500.0f);
			float fov = radiansToDegrees(m_app.getFOV());
			if (ImGui::SliderFloat("FOV", &fov, 0.1f, 180)) {
				fov = degreesToRadians(fov);
				m_app.setFOV(fov);
			}
		}

		if (ImGui::CollapsingHeader("Shortcuts")) showShortcutSettings();
		if (ImGui::CollapsingHeader("Toolbar")) showToolbarSettings();
		if (ImGui::CollapsingHeader("Style"))
		{
			ImGui::InputInt("Font size (needs restart)", &m_font_size);
			ImGui::ShowStyleEditor();
		}
	}
	ImGui::End();
}


} // namespace Lumix