#include <imgui/imgui.h>

#include "settings.h"
#include "engine/debug.h"
#include "engine/file_system.h"
#include "engine/engine.h"
#include "engine/geometry.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/os.h"
#include "editor/gizmo.h"
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
	char button_label[64];
	action->shortcutText(Span(button_label));

	bool res = false;
	ImGui::SetNextItemWidth(-30);
	ImGui::InputText("", button_label, sizeof(button_label), ImGuiInputTextFlags_ReadOnly);
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
	
	OutputMemoryStream buf(m_app.getAllocator());
	if (!fs.getContentSync(Path(path), Ref(buf))) {
		logError("Editor") << "Failed to open " << path;
		return false;
	}

	Span<const char> content((const char*)buf.data(), (u32)buf.size());
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

		Span<const char> content((const char*)buf.data(), (u32)buf.size());
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
	m_app.getGizmoConfig().scale = getFloat(L, "gizmo_scale", 1.f);
	m_mouse_sensitivity.x = getFloat(L, "mouse_sensitivity_x", 200.f);
	m_mouse_sensitivity.y = getFloat(L, "mouse_sensitivity_y", 200.f);
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
	file << "gizmo_scale = " << m_app.getGizmoConfig().scale << "\n";
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
	const float w = ImGui::CalcTextSize(ICON_FA_TIMES).x + ImGui::GetStyle().ItemSpacing.x * 2;
	ImGui::SetNextItemWidth(-w);
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

static void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void NodeFont(ImFont* font)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    bool font_details_opened = ImGui::TreeNode(font, "Font: \"%s\"\n%.2f px, %d glyphs, %d file(s)",
        font->ConfigData ? font->ConfigData[0].Name : "", font->FontSize, font->Glyphs.Size, font->ConfigDataCount);
    ImGui::SameLine(); if (ImGui::SmallButton("Set as default")) { io.FontDefault = font; }
    if (!font_details_opened)
        return;

    ImGui::PushFont(font);
    ImGui::Text("The quick brown fox jumps over the lazy dog");
    ImGui::PopFont();
    ImGui::DragFloat("Font scale", &font->Scale, 0.005f, 0.3f, 2.0f, "%.1f");   // Scale only this font
    ImGui::SameLine(); HelpMarker(
        "Note than the default embedded font is NOT meant to be scaled.\n\n"
        "Font are currently rendered into bitmaps at a given size at the time of building the atlas. "
        "You may oversample them to get some flexibility with scaling. "
        "You can also render at multiple sizes and select which one to use at runtime.\n\n"
        "(Glimmer of hope: the atlas system will be rewritten in the future to make scaling more flexible.)");
    ImGui::Text("Ascent: %f, Descent: %f, Height: %f", font->Ascent, font->Descent, font->Ascent - font->Descent);
    ImGui::Text("Fallback character: '%c' (U+%04X)", font->FallbackChar, font->FallbackChar);
    ImGui::Text("Ellipsis character: '%c' (U+%04X)", font->EllipsisChar, font->EllipsisChar);
    const int surface_sqrt = (int)sqrtf((float)font->MetricsTotalSurface);
    ImGui::Text("Texture Area: about %d px ~%dx%d px", font->MetricsTotalSurface, surface_sqrt, surface_sqrt);
    for (int config_i = 0; config_i < font->ConfigDataCount; config_i++)
        if (font->ConfigData)
            if (const ImFontConfig* cfg = &font->ConfigData[config_i])
                ImGui::BulletText("Input %d: \'%s\', Oversample: (%d,%d), PixelSnapH: %d, Offset: (%.1f,%.1f)",
                    config_i, cfg->Name, cfg->OversampleH, cfg->OversampleV, cfg->PixelSnapH, cfg->GlyphOffset.x, cfg->GlyphOffset.y);
    if (ImGui::TreeNode("Glyphs", "Glyphs (%d)", font->Glyphs.Size))
    {
        // Display all glyphs of the fonts in separate pages of 256 characters
        const ImU32 glyph_col = ImGui::GetColorU32(ImGuiCol_Text);
        for (unsigned int base = 0; base <= IM_UNICODE_CODEPOINT_MAX; base += 256)
        {
            // Skip ahead if a large bunch of glyphs are not present in the font (test in chunks of 4k)
            // This is only a small optimization to reduce the number of iterations when IM_UNICODE_MAX_CODEPOINT
            // is large // (if ImWchar==ImWchar32 we will do at least about 272 queries here)
            if (!(base & 4095) && font->IsGlyphRangeUnused(base, base + 4095))
            {
                base += 4096 - 256;
                continue;
            }

            int count = 0;
            for (unsigned int n = 0; n < 256; n++)
                if (font->FindGlyphNoFallback((ImWchar)(base + n)))
                    count++;
            if (count <= 0)
                continue;
            if (!ImGui::TreeNode((void*)(intptr_t)base, "U+%04X..U+%04X (%d %s)", base, base + 255, count, count > 1 ? "glyphs" : "glyph"))
                continue;
            float cell_size = font->FontSize * 1;
            float cell_spacing = style.ItemSpacing.y;
            ImVec2 base_pos = ImGui::GetCursorScreenPos();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            for (unsigned int n = 0; n < 256; n++)
            {
                // We use ImFont::RenderChar as a shortcut because we don't have UTF-8 conversion functions
                // available here and thus cannot easily generate a zero-terminated UTF-8 encoded string.
                ImVec2 cell_p1(base_pos.x + (n % 16) * (cell_size + cell_spacing), base_pos.y + (n / 16) * (cell_size + cell_spacing));
                ImVec2 cell_p2(cell_p1.x + cell_size, cell_p1.y + cell_size);
                const ImFontGlyph* glyph = font->FindGlyphNoFallback((ImWchar)(base + n));
                draw_list->AddRect(cell_p1, cell_p2, glyph ? IM_COL32(255, 255, 255, 100) : IM_COL32(255, 255, 255, 50));
                if (glyph)
                    font->RenderChar(draw_list, cell_size, cell_p1, glyph_col, (ImWchar)(base + n));
                if (glyph && ImGui::IsMouseHoveringRect(cell_p1, cell_p2))
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Codepoint: U+%04X", base + n);
                    ImGui::Separator();
                    ImGui::Text("Visible: %d", glyph->Visible);
                    ImGui::Text("AdvanceX: %.1f", glyph->AdvanceX);
                    ImGui::Text("Pos: (%.2f,%.2f)->(%.2f,%.2f)", glyph->X0, glyph->Y0, glyph->X1, glyph->Y1);
                    ImGui::Text("UV: (%.3f,%.3f)->(%.3f,%.3f)", glyph->U0, glyph->V0, glyph->U1, glyph->V1);
                    ImGui::EndTooltip();
                }
            }
            ImGui::Dummy(ImVec2((cell_size + cell_spacing) * 16, (cell_size + cell_spacing) * 16));
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    ImGui::TreePop();
}

bool ShowStyleSelector(const char* label)
{
    static int style_idx = -1;
    if (ImGui::Combo(label, &style_idx, "Classic\0Dark\0Light\0"))
    {
        switch (style_idx)
        {
        case 0: ImGui::StyleColorsClassic(); break;
        case 1: ImGui::StyleColorsDark(); break;
        case 2: ImGui::StyleColorsLight(); break;
        }
        return true;
    }
    return false;
}

// Demo helper function to select among loaded fonts.
// Here we use the regular BeginCombo()/EndCombo() api which is more the more flexible one.
void ShowFontSelector(const char* label)
{
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font_current = ImGui::GetFont();
    if (ImGui::BeginCombo(label, font_current->GetDebugName()))
    {
        for (int n = 0; n < io.Fonts->Fonts.Size; n++)
        {
            ImFont* font = io.Fonts->Fonts[n];
            ImGui::PushID((void*)font);
            if (ImGui::Selectable(font->GetDebugName(), font == font_current))
                io.FontDefault = font;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    HelpMarker(
        "- Load additional fonts with io.Fonts->AddFontFromFileTTF().\n"
        "- The font atlas is built when calling io.Fonts->GetTexDataAsXXXX() or io.Fonts->Build().\n"
        "- Read FAQ and docs/FONTS.md for more details.\n"
        "- If you need to add/remove fonts at runtime (e.g. for DPI change), do it before calling NewFrame().");
}


// copy-pasted from imgui
void Settings::showStyleEditor() const {
	#ifdef _WIN32
		#define IM_NEWLINE  "\r\n"
	#else
		#define IM_NEWLINE  "\n"
	#endif

	ImGuiStyle* ref = &ImGui::GetStyle();
	// You can pass in a reference ImGuiStyle structure to compare to, revert to and save to
    // (without a reference style pointer, we will use one compared locally as a reference)
    ImGuiStyle& style = ImGui::GetStyle();
    static ImGuiStyle ref_saved_style;

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.50f);

    if (ShowStyleSelector("Colors##Selector"))
        ref_saved_style = style;
    ShowFontSelector("Fonts##Selector");

    // Simplified Settings (expose floating-pointer border sizes as boolean representing 0.0f or 1.0f)
    if (ImGui::SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f"))
        style.GrabRounding = style.FrameRounding; // Make GrabRounding always the same value as FrameRounding
    { bool border = (style.WindowBorderSize > 0.0f); if (ImGui::Checkbox("WindowBorder", &border)) { style.WindowBorderSize = border ? 1.0f : 0.0f; } }
    ImGui::SameLine();
    { bool border = (style.FrameBorderSize > 0.0f);  if (ImGui::Checkbox("FrameBorder",  &border)) { style.FrameBorderSize  = border ? 1.0f : 0.0f; } }
    ImGui::SameLine();
    { bool border = (style.PopupBorderSize > 0.0f);  if (ImGui::Checkbox("PopupBorder",  &border)) { style.PopupBorderSize  = border ? 1.0f : 0.0f; } }

    // Save/Revert button
    if (ImGui::Button("Save Ref"))
        *ref = ref_saved_style = style;
    ImGui::SameLine();
    if (ImGui::Button("Revert Ref"))
        style = *ref;
    ImGui::SameLine();
    HelpMarker(
        "Save/Revert in local non-persistent storage. Default Colors definition are not affected. "
        "Use \"Export\" below to save them somewhere.");

    ImGui::Separator();

    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Sizes"))
        {
            ImGui::Text("Main");
            ImGui::SliderFloat2("WindowPadding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
            ImGui::SliderFloat2("FramePadding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
            ImGui::SliderFloat2("ItemSpacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
            ImGui::SliderFloat2("ItemInnerSpacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
            ImGui::SliderFloat2("TouchExtraPadding", (float*)&style.TouchExtraPadding, 0.0f, 10.0f, "%.0f");
            ImGui::SliderFloat("IndentSpacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
            ImGui::SliderFloat("ScrollbarSize", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
            ImGui::SliderFloat("GrabMinSize", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");
            ImGui::Text("Borders");
            ImGui::SliderFloat("WindowBorderSize", &style.WindowBorderSize, 0.0f, 1.0f, "%.0f");
            ImGui::SliderFloat("ChildBorderSize", &style.ChildBorderSize, 0.0f, 1.0f, "%.0f");
            ImGui::SliderFloat("PopupBorderSize", &style.PopupBorderSize, 0.0f, 1.0f, "%.0f");
            ImGui::SliderFloat("FrameBorderSize", &style.FrameBorderSize, 0.0f, 1.0f, "%.0f");
            ImGui::SliderFloat("TabBorderSize", &style.TabBorderSize, 0.0f, 1.0f, "%.0f");
            ImGui::Text("Rounding");
            ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0f, 12.0f, "%.0f");
            ImGui::SliderFloat("ChildRounding", &style.ChildRounding, 0.0f, 12.0f, "%.0f");
            ImGui::SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f");
            ImGui::SliderFloat("PopupRounding", &style.PopupRounding, 0.0f, 12.0f, "%.0f");
            ImGui::SliderFloat("ScrollbarRounding", &style.ScrollbarRounding, 0.0f, 12.0f, "%.0f");
            ImGui::SliderFloat("GrabRounding", &style.GrabRounding, 0.0f, 12.0f, "%.0f");
            ImGui::SliderFloat("LogSliderDeadzone", &style.LogSliderDeadzone, 0.0f, 12.0f, "%.0f");
            ImGui::SliderFloat("TabRounding", &style.TabRounding, 0.0f, 12.0f, "%.0f");
            ImGui::Text("Alignment");
            ImGui::SliderFloat2("WindowTitleAlign", (float*)&style.WindowTitleAlign, 0.0f, 1.0f, "%.2f");
            int window_menu_button_position = style.WindowMenuButtonPosition + 1;
            if (ImGui::Combo("WindowMenuButtonPosition", (int*)&window_menu_button_position, "None\0Left\0Right\0"))
                style.WindowMenuButtonPosition = window_menu_button_position - 1;
            ImGui::Combo("ColorButtonPosition", (int*)&style.ColorButtonPosition, "Left\0Right\0");
            ImGui::SliderFloat2("ButtonTextAlign", (float*)&style.ButtonTextAlign, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine(); HelpMarker("Alignment applies when a button is larger than its text content.");
            ImGui::SliderFloat2("SelectableTextAlign", (float*)&style.SelectableTextAlign, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine(); HelpMarker("Alignment applies when a selectable is larger than its text content.");
            ImGui::Text("Safe Area Padding");
            ImGui::SameLine(); HelpMarker("Adjust if you cannot see the edges of your screen (e.g. on a TV where scaling has not been configured).");
            ImGui::SliderFloat2("DisplaySafeAreaPadding", (float*)&style.DisplaySafeAreaPadding, 0.0f, 30.0f, "%.0f");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Colors"))
        {
            static int output_dest = 0;
            static bool output_only_modified = true;
            if (ImGui::Button("Export"))
            {
                if (output_dest == 0)
                    ImGui::LogToClipboard();
                else
                    ImGui::LogToTTY();
                ImGui::LogText("ImVec4* colors = ImGui::GetStyle().Colors;" IM_NEWLINE);
                for (int i = 0; i < ImGuiCol_COUNT; i++)
                {
                    const ImVec4& col = style.Colors[i];
                    const char* name = ImGui::GetStyleColorName(i);
                    if (!output_only_modified || memcmp(&col, &ref->Colors[i], sizeof(ImVec4)) != 0)
                        ImGui::LogText("colors[ImGuiCol_%s]%*s= ImVec4(%.2ff, %.2ff, %.2ff, %.2ff);" IM_NEWLINE,
                            name, 23 - (int)strlen(name), "", col.x, col.y, col.z, col.w);
                }
                ImGui::LogFinish();
            }
            ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::Combo("##output_type", &output_dest, "To Clipboard\0To TTY\0");
            ImGui::SameLine(); ImGui::Checkbox("Only Modified Colors", &output_only_modified);

            static ImGuiTextFilter filter;
            filter.Draw("Filter colors", ImGui::GetFontSize() * 16);

            static ImGuiColorEditFlags alpha_flags = 0;
            if (ImGui::RadioButton("Opaque", alpha_flags == ImGuiColorEditFlags_None))             { alpha_flags = ImGuiColorEditFlags_None; } ImGui::SameLine();
            if (ImGui::RadioButton("Alpha",  alpha_flags == ImGuiColorEditFlags_AlphaPreview))     { alpha_flags = ImGuiColorEditFlags_AlphaPreview; } ImGui::SameLine();
            if (ImGui::RadioButton("Both",   alpha_flags == ImGuiColorEditFlags_AlphaPreviewHalf)) { alpha_flags = ImGuiColorEditFlags_AlphaPreviewHalf; } ImGui::SameLine();
            HelpMarker(
                "In the color list:\n"
                "Left-click on colored square to open color picker,\n"
                "Right-click to open edit options menu.");

            ImGui::BeginChild("##colors", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NavFlattened);
            ImGui::PushItemWidth(-160);
            for (int i = 0; i < ImGuiCol_COUNT; i++)
            {
                const char* name = ImGui::GetStyleColorName(i);
                if (!filter.PassFilter(name))
                    continue;
                ImGui::PushID(i);
                ImGui::ColorEdit4("##color", (float*)&style.Colors[i], ImGuiColorEditFlags_AlphaBar | alpha_flags);
                if (memcmp(&style.Colors[i], &ref->Colors[i], sizeof(ImVec4)) != 0)
                {
                    // Tips: in a real user application, you may want to merge and use an icon font into the main font,
                    // so instead of "Save"/"Revert" you'd use icons!
                    // Read the FAQ and docs/FONTS.md about using icon fonts. It's really easy and super convenient!
                    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x); if (ImGui::Button("Save")) { ref->Colors[i] = style.Colors[i]; }
                    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x); if (ImGui::Button("Revert")) { style.Colors[i] = ref->Colors[i]; }
                }
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                ImGui::TextUnformatted(name);
                ImGui::PopID();
            }
            ImGui::PopItemWidth();
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Fonts"))
        {
            ImGuiIO& io = ImGui::GetIO();
            ImFontAtlas* atlas = io.Fonts;
            HelpMarker("Read FAQ and docs/FONTS.md for details on font loading.");
            ImGui::PushItemWidth(120);
            for (int i = 0; i < atlas->Fonts.Size; i++)
            {
                ImFont* font = atlas->Fonts[i];
                ImGui::PushID(font);
				NodeFont(font);
                ImGui::PopID();
            }
            if (ImGui::TreeNode("Atlas texture", "Atlas texture (%dx%d pixels)", atlas->TexWidth, atlas->TexHeight))
            {
                ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                ImVec4 border_col = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
                ImGui::Image(atlas->TexID, ImVec2((float)atlas->TexWidth, (float)atlas->TexHeight), ImVec2(0, 0), ImVec2(1, 1), tint_col, border_col);
                ImGui::TreePop();
            }

            // Post-baking font scaling. Note that this is NOT the nice way of scaling fonts, read below.
            // (we enforce hard clamping manually as by default DragFloat/SliderFloat allows CTRL+Click text to get out of bounds).
            const float MIN_SCALE = 0.3f;
            const float MAX_SCALE = 2.0f;
            HelpMarker(
                "Those are old settings provided for convenience.\n"
                "However, the _correct_ way of scaling your UI is currently to reload your font at the designed size, "
                "rebuild the font atlas, and call style.ScaleAllSizes() on a reference ImGuiStyle structure.\n"
                "Using those settings here will give you poor quality results.");
            static float window_scale = 1.0f;
            if (ImGui::DragFloat("window scale", &window_scale, 0.005f, MIN_SCALE, MAX_SCALE, "%.2f", ImGuiSliderFlags_AlwaysClamp)) // Scale only this window
                ImGui::SetWindowFontScale(window_scale);
            ImGui::DragFloat("global scale", &io.FontGlobalScale, 0.005f, MIN_SCALE, MAX_SCALE, "%.2f", ImGuiSliderFlags_AlwaysClamp); // Scale everything
            ImGui::PopItemWidth();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rendering"))
        {
            ImGui::Checkbox("Anti-aliased lines", &style.AntiAliasedLines);
            ImGui::SameLine();
            HelpMarker("When disabling anti-aliasing lines, you'll probably want to disable borders in your style as well.");

            ImGui::Checkbox("Anti-aliased lines use texture", &style.AntiAliasedLinesUseTex);
            ImGui::SameLine();
            HelpMarker("Faster lines using texture data. Require back-end to render with bilinear filtering (not point/nearest filtering).");

            ImGui::Checkbox("Anti-aliased fill", &style.AntiAliasedFill);
            ImGui::PushItemWidth(100);
            ImGui::DragFloat("Curve Tessellation Tolerance", &style.CurveTessellationTol, 0.02f, 0.10f, 10.0f, "%.2f");
            if (style.CurveTessellationTol < 0.10f) style.CurveTessellationTol = 0.10f;

            // When editing the "Circle Segment Max Error" value, draw a preview of its effect on auto-tessellated circles.
            ImGui::DragFloat("Circle Segment Max Error", &style.CircleSegmentMaxError, 0.01f, 0.10f, 10.0f, "%.2f");
            if (ImGui::IsItemActive())
            {
                ImGui::SetNextWindowPos(ImGui::GetCursorScreenPos());
                ImGui::BeginTooltip();
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                float RAD_MIN = 10.0f, RAD_MAX = 80.0f;
                float off_x = 10.0f;
                for (int n = 0; n < 7; n++)
                {
                    const float rad = RAD_MIN + (RAD_MAX - RAD_MIN) * (float)n / (7.0f - 1.0f);
                    draw_list->AddCircle(ImVec2(p.x + off_x + rad, p.y + RAD_MAX), rad, ImGui::GetColorU32(ImGuiCol_Text), 0);
                    off_x += 10.0f + rad * 2.0f;
                }
                ImGui::Dummy(ImVec2(off_x, RAD_MAX * 2.0f));
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
            HelpMarker("When drawing circle primitives with \"num_segments == 0\" tesselation will be calculated automatically.");

            ImGui::DragFloat("Global Alpha", &style.Alpha, 0.005f, 0.20f, 1.0f, "%.2f"); // Not exposing zero here so user doesn't "lose" the UI (zero alpha clips all widgets). But application code could have a toggle to switch between zero and non-zero.
            ImGui::PopItemWidth();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopItemWidth();
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
		if (ImGui::IsItemHovered()) {
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
				ImGui::DragFloat2("Mouse sensitivity", &m_mouse_sensitivity.x, 1.f, 0.1f, 500.0f);
				float fov = radiansToDegrees(m_app.getFOV());
				if (ImGui::SliderFloat("FOV", &fov, 0.1f, 180)) {
					fov = degreesToRadians(fov);
					m_app.setFOV(fov);
				}
				ImGui::DragFloat("Gizmo scale", &m_app.getGizmoConfig().scale, 0.1f);
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
				showStyleEditor();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}	
	}
	ImGui::End();
}


} // namespace Lumix