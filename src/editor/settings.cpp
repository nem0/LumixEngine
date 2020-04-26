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


namespace LuaWrapper {
	void getOptionalField(lua_State* L, int idx, const char* field_name, ImVec2* out) {
		if (LuaWrapper::getField(L, idx, field_name) != LUA_TNIL && isType<Vec2>(L, -1)) {
			const Vec2 tmp = toType<Vec2>(L, -1);
			out->x = tmp.x; 
			out->y = tmp.y; 
		}
		lua_pop(L, 1);
	}
}

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

		char tmp[32];
		if (LuaWrapper::getOptionalStringField(L, -1, "WindowMenuButtonPosition", Span(tmp))) {
			if (equalIStrings(tmp, "left")) style.WindowMenuButtonPosition = ImGuiDir_Left;
			if (equalIStrings(tmp, "right")) style.WindowMenuButtonPosition = ImGuiDir_Right;
			if (equalIStrings(tmp, "up")) style.WindowMenuButtonPosition = ImGuiDir_Up;
			if (equalIStrings(tmp, "down")) style.WindowMenuButtonPosition = ImGuiDir_Down;
			if (equalIStrings(tmp, "none")) style.WindowMenuButtonPosition = ImGuiDir_None;
		}

		#define LOAD_FLOAT(name) LuaWrapper::getOptionalField(L, -1, #name, &style.name);
		#define LOAD_BOOL(name) LuaWrapper::getOptionalField(L, -1, #name, &style.name);
		#define LOAD_VEC2(name) LuaWrapper::getOptionalField(L, -1, #name, &style.name);

		LOAD_FLOAT(Alpha);
		LOAD_VEC2(WindowPadding);
		LOAD_FLOAT(WindowRounding);
		LOAD_FLOAT(WindowBorderSize);
		LOAD_VEC2(WindowMinSize);
		LOAD_VEC2(WindowTitleAlign);
		LOAD_FLOAT(ChildRounding);
		LOAD_FLOAT(ChildBorderSize);
		LOAD_FLOAT(PopupRounding);
		LOAD_FLOAT(PopupBorderSize);
		LOAD_VEC2(FramePadding);
		LOAD_FLOAT(FrameRounding);
		LOAD_FLOAT(FrameBorderSize);
		LOAD_VEC2(ItemSpacing);
		LOAD_VEC2(ItemInnerSpacing);
		LOAD_VEC2(TouchExtraPadding);
		LOAD_FLOAT(IndentSpacing);
		LOAD_FLOAT(ColumnsMinSpacing);
		LOAD_FLOAT(ScrollbarSize);
		LOAD_FLOAT(ScrollbarRounding);
		LOAD_FLOAT(GrabMinSize);
		LOAD_FLOAT(GrabRounding);
		LOAD_FLOAT(TabRounding);
		LOAD_FLOAT(TabBorderSize);
		LOAD_VEC2(ButtonTextAlign);
		LOAD_VEC2(SelectableTextAlign);
		LOAD_VEC2(DisplayWindowPadding);
		LOAD_VEC2(DisplaySafeAreaPadding);
		LOAD_FLOAT(MouseCursorScale);
		LOAD_BOOL(AntiAliasedLines);           
		LOAD_BOOL(AntiAliasedFill);            
		LOAD_FLOAT(CurveTessellationTol);
		LOAD_FLOAT(CircleSegmentMaxError);

		#undef LOAD_FLOAT
		#undef LOAD_BOOL
		#undef LOAD_VEC2
		style.ScaleAllSizes(OS::getDPI() / 96.f);
	}
	lua_pop(L, 1);

}

static const char* toString(ImGuiDir dir)
{
	switch(dir) {
		case ImGuiDir_Up: return "up";
		case ImGuiDir_Down: return "down";
		case ImGuiDir_Left: return "left";
		case ImGuiDir_Right: return "right";
		case ImGuiDir_None: return "none";
		default: return "N/A";
	}
}

static void saveStyle(OS::OutputFile& file)
{
	auto& style = ImGui::GetStyle();
	file << "style = {";
	for (int i = 0; i < ImGuiCol_COUNT; ++i)
	{
		file << "\t" << ImGui::GetStyleColorName(i) << " = {" << style.Colors[i].x
			<< ", " << style.Colors[i].y
			<< ", " << style.Colors[i].z
			<< ", " << style.Colors[i].w << "},\n";
	}

	file << "\tWindowMenuButtonPosition = \"" << toString(style.WindowMenuButtonPosition) << "\",\n";

	#define SAVE_FLOAT(name) do { file << "\t" #name " = " << style.name << ",\n"; } while(false)
	#define SAVE_BOOL(name) do { file << "\t" #name " = " << (style.name ? "true" : "false") << ",\n"; } while(false)
	#define SAVE_VEC2(name) do { file << "\t" #name " = {" << style.name.x << ", " << style.name.y << "},\n"; } while(false)

    SAVE_FLOAT(Alpha);
    SAVE_VEC2(WindowPadding);
    SAVE_FLOAT(WindowRounding);
    SAVE_FLOAT(WindowBorderSize);
    SAVE_VEC2(WindowMinSize);
    SAVE_VEC2(WindowTitleAlign);
    SAVE_FLOAT(ChildRounding);
    SAVE_FLOAT(ChildBorderSize);
    SAVE_FLOAT(PopupRounding);
    SAVE_FLOAT(PopupBorderSize);
    SAVE_VEC2(FramePadding);
    SAVE_FLOAT(FrameRounding);
    SAVE_FLOAT(FrameBorderSize);
    SAVE_VEC2(ItemSpacing);
    SAVE_VEC2(ItemInnerSpacing);
    SAVE_VEC2(TouchExtraPadding);
    SAVE_FLOAT(IndentSpacing);
    SAVE_FLOAT(ColumnsMinSpacing);
    SAVE_FLOAT(ScrollbarSize);
    SAVE_FLOAT(ScrollbarRounding);
    SAVE_FLOAT(GrabMinSize);
    SAVE_FLOAT(GrabRounding);
    SAVE_FLOAT(TabRounding);
    SAVE_FLOAT(TabBorderSize);
    SAVE_VEC2(ButtonTextAlign);
    SAVE_VEC2(SelectableTextAlign);
    SAVE_VEC2(DisplayWindowPadding);
    SAVE_VEC2(DisplaySafeAreaPadding);
    SAVE_FLOAT(MouseCursorScale);
    SAVE_BOOL(AntiAliasedLines);           
    SAVE_BOOL(AntiAliasedFill);            
    SAVE_FLOAT(CurveTessellationTol);
    SAVE_FLOAT(CircleSegmentMaxError);

	#undef SAVE_BOOL
	#undef SAVE_FLOAT
	#undef SAVE_VEC2

	file << "}\n";
}


static bool shortcutInput(Ref<Action> action, bool edit)
{
	char tmp[32];
	OS::getKeyName(action->shortcut, Span(tmp));
	
	const char* mod0 = (action->modifiers & (u8)Action::Modifiers::CTRL) ? "CTRL " : "";
	const char* mod1 = (action->modifiers & (u8)Action::Modifiers::SHIFT) ? "SHIFT " : "";
	const char* mod2 = (action->modifiers & (u8)Action::Modifiers::ALT) ? "ALT " : "";

	StaticString<64> button_label(mod0, mod1, mod2, action->shortcut == OS::Keycode::INVALID ? "" : tmp, "");

	bool res = false;
	ImGui::SetNextItemWidth(-30);
	ImGui::InputText("", button_label.data, sizeof(button_label.data), ImGuiInputTextFlags_ReadOnly);
	if (ImGui::IsItemActive()) {
		if (OS::isKeyDown(OS::Keycode::SHIFT)) action->modifiers |= (u8)Action::Modifiers::SHIFT;
		if (OS::isKeyDown(OS::Keycode::MENU)) action->modifiers |= (u8)Action::Modifiers::ALT;
		if (OS::isKeyDown(OS::Keycode::CTRL)) action->modifiers |= (u8)Action::Modifiers::CTRL;

		for (int i = 0; i < (int)OS::Keycode::MAX; ++i) {
			const auto kc= (OS::Keycode)i;
			const bool is_mouse = kc == OS::Keycode::LBUTTON || kc == OS::Keycode::RBUTTON || kc == OS::Keycode::MBUTTON;
			const bool is_modifier = kc == OS::Keycode::SHIFT 
				|| kc == OS::Keycode::LSHIFT 
				|| kc == OS::Keycode::RSHIFT 
				|| kc == OS::Keycode::MENU 
				|| kc == OS::Keycode::LMENU 
				|| kc == OS::Keycode::RMENU 
				|| kc == OS::Keycode::CTRL 
				|| kc == OS::Keycode::LCTRL 
				|| kc == OS::Keycode::RCTRL;
			if (OS::isKeyDown(kc) && !is_mouse && !is_modifier) {
				action->shortcut = (OS::Keycode)i;
				break;
			}
		}
	}
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TRASH, "Clear")) {
		action->modifiers = 0;
		action->shortcut = OS::Keycode::INVALID;
	}
	
	return res;
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
	, m_imgui_state(app.getAllocator())
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

	bool valid_version = false;
	lua_getglobal(L, "version");
	if (lua_type(L, -1) == LUA_TNUMBER) {
		const int version = (int)lua_tonumber(L, -1);
		valid_version = version == 1;
	}
	lua_pop(L, 1);

	if (!valid_version) {
		if (!fs.getContentSync(Path(DEFAULT_SETTINGS_PATH), Ref(buf))) {
			logError("Editor") << "Failed to open " << DEFAULT_SETTINGS_PATH;
			return false;
		}

		Span<const char> content((const char*)buf.begin(), buf.size());
		if (!LuaWrapper::execute(L, content, "settings", 0)) {
			return false;
		}
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

	lua_getglobal(L, "imgui");
	if (lua_type(L, -1) == LUA_TSTRING) {
		m_imgui_state = lua_tostring(L, -1);
	}
	lua_pop(L, 1);

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
				if (LuaWrapper::getField(L, -1, "key") == LUA_TNUMBER) {
					actions[i]->shortcut = (OS::Keycode)lua_tointeger(L, -1);
				}
				lua_pop(L, 1);
				if (LuaWrapper::getField(L, -1, "modifiers") == LUA_TNUMBER) {
					actions[i]->modifiers = (u8)lua_tointeger(L, -1);
				}
				lua_pop(L, 1);
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

	file << "version = 1\n";

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

	file << "imgui = [[" << m_imgui_state.c_str() << "]]\n";

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
		file << "\t" << actions[i]->name << " = { key = " 
			<< (int)actions[i]->shortcut << ", modifiers = "
			<< (int)actions[i]->modifiers << "},\n";
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
	
	ImGui::PushFont(m_app.getBigIconFont());
	for (auto* action : actions)
	{
		ImGui::Button(action->font_icon);
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
	ImGui::PopFont();

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
		if (action->font_icon[0] && action->is_global)
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
	ImGui::SetNextItemWidth(-20);
	ImGui::InputTextWithHint("##filter", "Filter", m_filter, sizeof(m_filter));
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TIMES, "Clear filter")) {
		m_filter[0] = '\0';
	}

	for (int i = 0; i < actions.size(); ++i)
	{
		Action& a = *actions[i];
		if (m_filter[0] == 0 || stristr(a.label_long, m_filter) != 0)
		{
			ImGui::PushID(&a);
			ImGuiEx::Label(a.label_long.data);
			if (shortcutInput(Ref(a), &a == m_edit_action)) {
				m_edit_action = &a;
			}
			ImGui::PopID();
		}
	}
}


void Settings::onGUI()
{
	if (!m_is_open) return;
	if (ImGui::Begin(ICON_FA_COG "Settings##settings", &m_is_open))
	{
		if (ImGui::Button(ICON_FA_SAVE "Save")) m_app.saveSettings();
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_REDO_ALT "Reload")) load();
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", "Settings are saved in studio.ini when the application closes");
		}

		if (ImGui::BeginTabBar("tabs")) {

			if (ImGui::BeginTabItem("General")) {
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
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Shortcuts")) {
				showShortcutSettings();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Toolbar")) {
				showToolbarSettings();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Style"))
			{
				ImGui::InputInt("Font size (needs restart)", &m_font_size);
				ImGui::ShowStyleEditor();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}	
	}
	ImGui::End();
}


} // namespace Lumix