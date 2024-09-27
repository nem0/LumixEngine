#include <imgui/imgui.h>
#include "core/crt.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/tokenizer.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "settings.h"

namespace Lumix {

static const char SETTINGS_PATH[] = "studio.ini";
static const char DEFAULT_SETTINGS_PATH[] = "studio_default.ini";

template<>
struct HashFunc<String> {
	static u32 get(const String& key) {
		RuntimeHash32 h(key.c_str(), key.length());
		return h.getHashValue();
	}
};

template<>
struct HashFunc<StringView> {
	static u32 get(const StringView& key) {
		if (key.size() == 0) return 0;
		RuntimeHash32 h(key.begin, key.size());
		return h.getHashValue();
	}
};

static bool shortcutInput(char* button_label, Action& action, bool edit) {
	bool res = false;
	ImGui::SetNextItemWidth(-30);
	ImGui::InputText("", button_label, sizeof(button_label), ImGuiInputTextFlags_ReadOnly);
	if (ImGui::IsItemActive()) {
		if (os::isKeyDown(os::Keycode::SHIFT)) action.modifiers |= Action::Modifiers::SHIFT;
		if (os::isKeyDown(os::Keycode::ALT)) action.modifiers |= Action::Modifiers::ALT;
		if (os::isKeyDown(os::Keycode::CTRL)) action.modifiers |= Action::Modifiers::CTRL;

		for (int i = 0; i < (int)os::Keycode::MAX; ++i) {
			const auto kc = (os::Keycode)i;
			const bool is_mouse = kc == os::Keycode::LBUTTON || kc == os::Keycode::RBUTTON || kc == os::Keycode::MBUTTON;
			const bool is_modifier = kc == os::Keycode::SHIFT || kc == os::Keycode::LSHIFT || kc == os::Keycode::RSHIFT || kc == os::Keycode::ALT || kc == os::Keycode::LALT ||
									 kc == os::Keycode::RALT || kc == os::Keycode::CTRL || kc == os::Keycode::LCTRL || kc == os::Keycode::RCTRL;
			if (os::isKeyDown(kc) && !is_mouse && !is_modifier) {
				action.shortcut = (os::Keycode)i;
				break;
			}
		}
	}
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TRASH, "Clear")) {
		action.modifiers = Action::Modifiers::NONE;
		action.shortcut = os::Keycode::INVALID;
	}

	return res;
}

MouseSensitivity::MouseSensitivity(IAllocator& allocator)
	: values(allocator) {
	values.push({0, 0.5f});
	values.push({16, 16.f});
}

bool MouseSensitivity::load(Tokenizer& tokenizer) {
	values.clear();
	if (!tokenizer.consume("[")) return false;
	for (;;) {
		Tokenizer::Token token = tokenizer.nextToken();
		if (!token) return false;
		if (token == "]") break;
		if (token.type != Tokenizer::Token::NUMBER) return false;
		float x = tokenizer.toFloat(token);
		float y;
		if (!tokenizer.consume(",", y)) return false;
		values.push({x, y});

		token = tokenizer.nextToken();
		if (token == "]") break;
		if (token != ",") {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',' or ']' got ", token.value);
			tokenizer.logErrorPosition(token.value.begin);
			return false;
		}
	}
	return true;
}

void MouseSensitivity::save(const char* name, OutputMemoryStream& blob) {
	blob << name << " = [";
	for (const Vec2& v : values) {
		blob << v.x << ", " << v.y << ", ";
	}
	blob << "]\n";
}

void MouseSensitivity::gui(const char* label) {
	ImGui::PushID(this);
	ImGuiEx::Label(label);
	if (ImGuiEx::CurvePreviewButton("curve", &values.begin()->x, &values.begin()->y, values.size(), ImVec2(130, ImGui::GetTextLineHeight()), sizeof(values[0]))) ImGui::OpenPopup(label);
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_PENCIL_ALT)) ImGui::OpenPopup(label);
	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
	if (ImGui::BeginPopupModal(label, &open)) {
		i32 new_count;
		ImU32 flags = ImU32(ImGuiEx::CurveEditorFlags::NO_TANGENTS | ImGuiEx::CurveEditorFlags::SHOW_GRID);
		if (ImGuiEx::IconButton(ICON_FA_EXPAND, "Fit to view")) flags |= (ImU32)ImGuiEx::CurveEditorFlags::RESET;
		ImGuiEx::CurveEditor("##curve", &values.begin()->x, values.size(), values.capacity(), ImGui::GetContentRegionAvail(), flags, &new_count);
		values.resize(new_count);
		if (new_count == values.capacity()) values.reserve(values.capacity() + 1);
		ImGui::EndPopup();
	}

	ImGui::PopID();
}

float MouseSensitivity::eval(float value) {
	float M = signum(value) / 500.f; // so `value` ~ 1 is a reasonable setting
	value = fabsf(value);
	if (values.empty()) return M * value;
	if (value < values[0].x) return M * values[0].y;
	for (i32 i = 1; i < values.size(); ++i) {
		if (value < values[i].x) {
			const float t = (value - values[i - 1].x) / (values[i].x - values[i - 1].x);
			return M * (values[i - 1].y + t * (values[i].y - values[i - 1].y));
		}
	}
	return M * values.last().y;
}

Settings::Settings(StudioApp& app)
	: m_app(app)
	, m_allocator(app.getAllocator(), "settings")
	, m_categories(m_allocator)
	, m_imgui_state(m_allocator)
	, m_mouse_sensitivity_x(m_allocator)
	, m_mouse_sensitivity_y(m_allocator)
	, m_variables(m_allocator)
	, m_app_data_path(m_allocator)
{
	char tmp[MAX_PATH];
	if (!os::getAppDataDir(Span(tmp))) {
		m_app_data_path = "";
		logError("Could not get app data path");
	}
	else {
		m_app_data_path = tmp;
		m_app_data_path.append("\\lumixengine\\studio\\");
		if (!os::makePath(m_app_data_path.c_str())) {
			logError("Could not create ", m_app_data_path);
		}
		m_app_data_path.append("studio.ini");
	}

	m_last_save_time = os::Timer::getRawTimestamp();
	registerPtr("imgui_state", &m_imgui_state);
	registerPtr("settings_open", &m_is_open);
}

float Settings::getTimeSinceLastSave() const {
	const u64 now = os::Timer::getRawTimestamp();
	return os::Timer::rawToSeconds(now - m_last_save_time);
}

static Settings::Variable& createVar(Settings& settings, StringView name) {
	Settings::Variable& var = settings.m_variables.insert(String(name, settings.m_allocator));
	return var;
}

static Settings::Variable* findVar(Settings& settings, StringView name) {
	auto iter = settings.m_variables.find(name);
	if (iter.isValid()) return &iter.value();
	return nullptr;
}

static ImGuiDir toImGuiDir(StringView str) {
	if (equalStrings(str, "up")) return ImGuiDir_Up;
	if (equalStrings(str, "down")) return ImGuiDir_Down;
	if (equalStrings(str, "left")) return ImGuiDir_Left;
	if (equalStrings(str, "right")) return ImGuiDir_Right;
	return ImGuiDir_None;
}

static const char* toString(ImGuiDir dir) {
	switch (dir) {
		case ImGuiDir_Up: return "up";
		case ImGuiDir_Down: return "down";
		case ImGuiDir_Left: return "left";
		case ImGuiDir_Right: return "right";
		case ImGuiDir_None: return "none";
		default: return "N/A";
	}
}

static void saveStyle(OutputMemoryStream& blob) {
	blob << "style = {\n";
	ImGuiStyle& style = ImGui::GetStyle();
	for (int i = 0; i < ImGuiCol_COUNT; ++i) {
		blob << "\t" << ImGui::GetStyleColorName(i) << " = {" << style.Colors[i].x << ", " << style.Colors[i].y << ", " << style.Colors[i].z << ", " << style.Colors[i].w << "},\n";
	}

	blob << "\tWindowMenuButtonPosition = \"" << toString(style.WindowMenuButtonPosition) << "\",\n";

	#define SAVE_FLOAT(name)                                 \
		do {                                                 \
			blob << "\t" #name " = " << style.name << ",\n"; \
		} while (false)
	#define SAVE_BOOL(name)                                                       \
		do {                                                                      \
			blob << "\t" #name " = " << (style.name ? "true" : "false") << ",\n"; \
		} while (false)
	#define SAVE_VEC2(name)                                                              \
		do {                                                                             \
			blob << "\t" #name " = {" << style.name.x << ", " << style.name.y << "},\n"; \
		} while (false)
	
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
	SAVE_FLOAT(CircleTessellationMaxError);

	#undef SAVE_BOOL
	#undef SAVE_FLOAT
	#undef SAVE_VEC2

	blob << "\tdpi = " << os::getDPI() << "\n";

	blob << "}\n";
}

static ImGuiStyle g_imgui_style;

static bool loadShortcuts(Settings& settings, Tokenizer& tokenizer) {
	if (!tokenizer.consume("{")) return false;
	auto& actions = settings.m_app.getActions();
	// TODO O(n^2)
	for (;;) {
		Tokenizer::Token token = tokenizer.nextToken();
		if (token == "}") return true;
		if (!token) return false;

		if (token.type != Tokenizer::Token::IDENTIFIER) {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): unexpected token in settings: ", token.value);
			tokenizer.logErrorPosition(token.value.begin);
			return false;
		}

		i32 key, modifiers;
		if (!tokenizer.consume("=", "{", "key", "=", key, ",", "modifiers", "=", modifiers,"}")) return	false;

		Action* action = nullptr;
		for (int i = 0; i < actions.size(); ++i) {
			if (actions[i]->name == token) {
				action = actions[i];
				break;
			}
		}

		if (action) {
			// settings can contain unknown keybindings (for disabled plugins, removed options, etc.)
			action->shortcut = (os::Keycode)key;
			action->modifiers = (Action::Modifiers)modifiers;
		}

		Tokenizer::Token next_token = tokenizer.nextToken();
		if (next_token == "}") return true;
		if (next_token != ",") {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): unexpected token in settings: ", next_token.value);
			tokenizer.logErrorPosition(next_token.value.begin);
			return false;
		}
	}
}

static bool loadStyle(Tokenizer& tokenizer) {
	if (!tokenizer.consume("{")) return false;

	ImGuiStyle& style = g_imgui_style;
	// TODO O(n^2)
	i32 dpi = os::getDPI();
	for (;;) {
		Tokenizer::Token token = tokenizer.nextToken();
		if (token == "}") break;

		if (token.type != Tokenizer::Token::IDENTIFIER) {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): unexpected token in settings: ", token.value);
			tokenizer.logErrorPosition(token.value.begin);
			return false;
		}

		for (int i = 0; i < ImGuiCol_COUNT; ++i) {
			if (token == ImGui::GetStyleColorName(i)) {
				if (!tokenizer.consume("=", "{")) return false;
				u32 vec_size;
				if (!tokenizer.consumeVector(&style.Colors[i].x, vec_size)) return false;
				if (vec_size != 4) {
					logError(tokenizer.filename, "(", tokenizer.getLine(), "): expected 4 numbers");
					tokenizer.logErrorPosition(token.value.begin);
				}
				break;
			}
		}

		if (token == "WindowMenuButtonPosition") {
			StringView str;
			if (!tokenizer.consume("=", str)) return false;
			style.WindowMenuButtonPosition = toImGuiDir(str);
		}

		#define LOAD_FLOAT(name) \
			else if (token == #name) { if (!tokenizer.consume("=", style.name)) return false; }
		#define LOAD_BOOL(name) \
			else if (token == #name) { if (!tokenizer.consume("=", style.name)) return false; }
		#define LOAD_VEC2(name) \
			else if (token == #name) { if (!tokenizer.consume("=", "{", style.name.x, ",", style.name.y, "}")) return false; }

		if (false) {}
		LOAD_FLOAT(Alpha)
		LOAD_VEC2(WindowPadding)
		LOAD_FLOAT(WindowRounding)
		LOAD_FLOAT(WindowBorderSize)
		LOAD_VEC2(WindowMinSize)
		LOAD_VEC2(WindowTitleAlign)
		LOAD_FLOAT(ChildRounding)
		LOAD_FLOAT(ChildBorderSize)
		LOAD_FLOAT(PopupRounding)
		LOAD_FLOAT(PopupBorderSize)
		LOAD_VEC2(FramePadding)
		LOAD_FLOAT(FrameRounding)
		LOAD_FLOAT(FrameBorderSize)
		LOAD_VEC2(ItemSpacing)
		LOAD_VEC2(ItemInnerSpacing)
		LOAD_VEC2(TouchExtraPadding)
		LOAD_FLOAT(IndentSpacing)
		LOAD_FLOAT(ColumnsMinSpacing)
		LOAD_FLOAT(ScrollbarSize)
		LOAD_FLOAT(ScrollbarRounding)
		LOAD_FLOAT(GrabMinSize)
		LOAD_FLOAT(GrabRounding)
		LOAD_FLOAT(TabRounding)
		LOAD_FLOAT(TabBorderSize)
		LOAD_VEC2(ButtonTextAlign)
		LOAD_VEC2(SelectableTextAlign)
		LOAD_VEC2(DisplayWindowPadding)
		LOAD_VEC2(DisplaySafeAreaPadding)
		LOAD_FLOAT(MouseCursorScale)
		LOAD_BOOL(AntiAliasedLines)
		LOAD_BOOL(AntiAliasedFill)
		LOAD_FLOAT(CurveTessellationTol)
		LOAD_FLOAT(CircleTessellationMaxError)
		else if (token == "dpi") {
			if (!tokenizer.consume("=", dpi)) return false;
		}

		#undef LOAD_BOOL
		#undef LOAD_FLOAT
		#undef LOAD_VEC2

		Tokenizer::Token next_token = tokenizer.nextToken();
		if (next_token == "}") break;
		if (next_token != ",") {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): unexpected token in settings: ", next_token.value);
			tokenizer.logErrorPosition(next_token.value.begin);
			return false;
		}
	}
	
	if (dpi != os::getDPI()) style.ScaleAllSizes(os::getDPI() / float(dpi));
	if (ImGui::GetCurrentContext()) ImGui::GetStyle() = g_imgui_style;
	return true;
}

void Settings::load() {
	FileSystem& fs = m_app.getEngine().getFileSystem();
	const bool has_settings = fs.fileExists(SETTINGS_PATH);
	const char* path = has_settings ? SETTINGS_PATH : DEFAULT_SETTINGS_PATH;

	auto parse = [&](OutputMemoryStream& buf, const char* path, Storage storage) -> bool {
		Tokenizer tokenizer(StringView((const char*)buf.data(), (u32)buf.size()), path);
		for (;;) {
			Tokenizer::Token var_name = tokenizer.tryNextToken(Tokenizer::Token::IDENTIFIER);
			switch (var_name.type) {
				case Tokenizer::Token::EOF: return true;
				case Tokenizer::Token::ERROR: return false;
				default: ASSERT(var_name.type == Tokenizer::Token::IDENTIFIER); break;
			}
			if (!tokenizer.consume("=")) return false;
			
			if (var_name == "mouse_sensitivity_x") {
				m_mouse_sensitivity_x.load(tokenizer);
				continue;
			}
			else if (var_name == "mouse_sensitivity_y") {
				m_mouse_sensitivity_y.load(tokenizer);
				continue;
			}
			else if (var_name == "style") {
				if (!loadStyle(tokenizer)) return false;
				continue;
			}
			else if (var_name == "keybindings") {
				if (!loadShortcuts(*this, tokenizer)) return false;
				continue;
			}

			Variable* var = findVar(*this, var_name.value);
			if (var) {
				var->storage = storage;
				switch (var->type) {
					case Variable::BOOL_PTR: if (!tokenizer.consume(*var->bool_ptr)) return false; break;
					case Variable::BOOL: if (!tokenizer.consume(var->bool_value)) return false; break;
					case Variable::I32_PTR: if (!tokenizer.consume(*var->i32_ptr)) return false; break;
					case Variable::I32: if (!tokenizer.consume(var->i32_value)) return false; break;
					case Variable::FLOAT_PTR: if (!tokenizer.consume(*var->float_ptr)) return false; break;
					case Variable::FLOAT: if (!tokenizer.consume(var->float_value)) return false; break;
					case Variable::STRING: {
						StringView str;
						if (!tokenizer.consume(str)) return false;
						var->string_value = str;
						break;
					}
					case Variable::STRING_PTR: {
						StringView str;
						if (!tokenizer.consume(str)) return false;
						*var->string_ptr = str;
						break;
					}
				}
				if (var->set_callback.isValid()) var->set_callback.invoke();
			}
			else {
				var = &createVar(*this, var_name.value);
				var->storage = storage;
				Tokenizer::Token value = tokenizer.nextToken();
				switch (value.type) {
					case Tokenizer::Token::NUMBER: fromCString(value.value, var->i32_value); var->type = Variable::I32; break;
					case Tokenizer::Token::STRING: var->string_value = value.value; break;
					case Tokenizer::Token::IDENTIFIER:
						if (value == "true") {
							var->bool_value = true;
							var->type = Variable::BOOL;
						} else if (value == "false") {
							var->bool_value = false;
							var->type = Variable::BOOL;
						} else {
							m_variables.erase(var_name.value);
							logError(tokenizer.filename, "(", tokenizer.getLine(), "): Unexpected token in settings: ", value.value);
							tokenizer.logErrorPosition(value.value.begin);
							return false;
						}
						break;
					default: 
						m_variables.erase(var_name.value);
						logError(tokenizer.filename, "(", tokenizer.getLine(), "): Unexpected token in settings: ", value.value);
						tokenizer.logErrorPosition(value.value.begin);
						return false;
				}
			}
		}
		return true;
	};

	OutputMemoryStream buf(m_app.getAllocator());
	if (fs.getContentSync(Path(path), buf)) {
		parse(buf, path, WORKSPACE);
	}
	else {
		logError("Failed to read ", path);
	}

	buf.clear();
	os::InputFile file;
	if (file.open(m_app_data_path.c_str())) {
		buf.resize(file.size());
		if (file.read(buf.getMutableData(), buf.size())) {
			parse(buf, m_app_data_path.c_str(), USER);
		}
		else {
			logError("Failed to read ", m_app_data_path);
		}
		file.close();
	}
}

static void saveShortcuts(Settings& settings, OutputMemoryStream& blob) {
	auto& actions = settings.m_app.getActions();
	blob << "keybindings = {\n";
	for (int i = 0; i < actions.size(); ++i)
	{
		blob << "\t" << actions[i]->name << " = { key = " 
			<< (int)actions[i]->shortcut << ", modifiers = "
			<< (int)actions[i]->modifiers << "},\n";
	}
	blob << "}\n";

}

void Settings::save() {
	auto serialize = [&](Storage storage, OutputMemoryStream& blob){
		for (auto iter = m_variables.begin(), end = m_variables.end(); iter != end; ++iter) {
			Variable& var = iter.value();
			if (var.storage != storage) continue;
			blob << iter.key() << " = ";
			switch (var.type) {
				case Variable::BOOL: blob << (var.bool_value ? "true" : "false"); break;
				case Variable::BOOL_PTR: blob << (*var.bool_ptr ? "true" : "false"); break;
				case Variable::I32: blob << var.i32_value; break;
				case Variable::I32_PTR: blob << *var.i32_ptr; break;
				case Variable::FLOAT: blob << var.float_value; break;
				case Variable::FLOAT_PTR: blob << *var.float_ptr; break;
				case Variable::STRING_PTR: blob << "`" << var.string_ptr->c_str() << "`"; break;
				case Variable::STRING: blob << "`" << var.string_value.c_str() << "`"; break;
			}
			blob << "\n";
		}
		if (storage == USER) {
			saveStyle(blob);
			saveShortcuts(*this, blob);
			m_mouse_sensitivity_x.save("mouse_sensitivity_x", blob);
			m_mouse_sensitivity_y.save("mouse_sensitivity_y", blob);
		}
	};

	OutputMemoryStream blob(m_allocator);
	serialize(WORKSPACE, blob);
	FileSystem& fs = m_app.getEngine().getFileSystem();
	if (!fs.saveContentSync(Path(SETTINGS_PATH), blob)) {
		logError("Failed to save workspace settings in ", SETTINGS_PATH);
	}

	blob.clear();
	serialize(USER, blob);
	os::OutputFile file;
	if (file.open(m_app_data_path.c_str())) {
		if (!file.write(blob.data(), blob.size())) {
			logError("Failed to save user settings in ", m_app_data_path);
		}
		file.close();
	}
	else {
		logError("Failed to save user settings in ", m_app_data_path);
	}

	m_last_save_time = os::Timer::getRawTimestamp();
}

static void HelpMarker(const char* desc) {
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

// following two themes are from https://github.com/Raais/ImguiCandy/blob/main/ImCandy/candy.h
// MIT License, Copyright (c) 2021 Raais N.
static void Theme_Blender(ImGuiStyle* dst = NULL) {
	// 'Blender Dark' theme from v3.0.0 [Improvised]
	// Colors grabbed using X11 Soft/xcolor
	ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
	ImVec4* colors = style->Colors;
	ImGui::StyleColorsDark(style); // Reset to base/dark theme
	colors[ImGuiCol_Text] = ImVec4(0.84f, 0.84f, 0.84f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
	colors[ImGuiCol_Border] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.10f, 0.10f, 0.10f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.54f, 0.54f, 0.54f, 1.00f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.19f, 0.39f, 0.69f, 1.00f);
	colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.20f, 0.39f, 0.69f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
	style->WindowPadding = ImVec2(12.00f, 8.00f);
	style->ItemSpacing = ImVec2(7.00f, 3.00f);
	style->GrabMinSize = 20.00f;
	style->WindowRounding = 8.00f;
	style->FrameBorderSize = 0.00f;
	style->FrameRounding = 4.00f;
	style->GrabRounding = 12.00f;
}

static void Theme_Nord(ImGuiStyle* dst = NULL) {
	// Nord/Nordic GTK [Improvised]
	// https://github.com/EliverLara/Nordic
	ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
	ImVec4* colors = style->Colors;
	ImGui::StyleColorsDark(style); // Reset to base/dark theme
	colors[ImGuiCol_Text] = ImVec4(0.85f, 0.87f, 0.91f, 0.88f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.49f, 0.50f, 0.53f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_Border] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.09f, 0.09f, 0.09f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.23f, 0.26f, 0.32f, 0.60f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.51f, 0.63f, 0.76f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.53f, 0.75f, 0.82f, 0.86f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.61f, 0.74f, 0.87f, 1.00f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.24f, 0.31f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.23f, 0.26f, 0.32f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.17f, 0.19f, 0.23f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.56f, 0.74f, 0.73f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.53f, 0.75f, 0.82f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.37f, 0.51f, 0.67f, 1.00f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.53f, 0.75f, 0.82f, 0.86f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.37f, 0.51f, 0.67f, 0.65f);
	style->WindowBorderSize = 1.00f;
	style->ChildBorderSize = 1.00f;
	style->PopupBorderSize = 1.00f;
	style->FrameBorderSize = 1.00f;
}

static void Theme_Lumix(ImGuiStyle* dst = NULL) {
	ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
	ImVec4* colors = style->Colors;
	ImGui::StyleColorsDark(style); // Reset to base/dark theme

	colors[ImGuiCol_Text] = {1.000000f, 1.000000f, 1.000000f, 1.000000f};
	colors[ImGuiCol_TextDisabled] = {0.500000f, 0.500000f, 0.500000f, 1.000000f};
	colors[ImGuiCol_WindowBg] = {0.160000f, 0.160000f, 0.160000f, 1.000000f};
	colors[ImGuiCol_ChildBg] = {0.160000f, 0.160000f, 0.160000f, 1.000000f};
	colors[ImGuiCol_PopupBg] = {0.140000f, 0.140000f, 0.140000f, 1.000000f};
	colors[ImGuiCol_Border] = {0.240000f, 0.240000f, 0.240000f, 1.000000f};
	colors[ImGuiCol_BorderShadow] = {0.000000f, 0.000000f, 0.000000f, 0.000000f};
	colors[ImGuiCol_FrameBg] = {0.260000f, 0.260000f, 0.260000f, 1.000000f};
	colors[ImGuiCol_FrameBgHovered] = {0.320000f, 0.320000f, 0.320000f, 1.000000f};
	colors[ImGuiCol_FrameBgActive] = {0.370000f, 0.370000f, 0.370000f, 1.000000f};
	colors[ImGuiCol_TitleBg] = {0.156863f, 0.156863f, 0.156863f, 1.000000f};
	colors[ImGuiCol_TitleBgActive] = {0.156863f, 0.156863f, 0.156863f, 1.000000f};
	colors[ImGuiCol_TitleBgCollapsed] = {0.160000f, 0.160000f, 0.160000f, 1.000000f};
	colors[ImGuiCol_MenuBarBg] = {0.140000f, 0.140000f, 0.140000f, 1.000000f};
	colors[ImGuiCol_ScrollbarBg] = {0.020000f, 0.020000f, 0.020000f, 0.000000f};
	colors[ImGuiCol_ScrollbarGrab] = {0.310000f, 0.310000f, 0.310000f, 1.000000f};
	colors[ImGuiCol_ScrollbarGrabHovered] = {0.410000f, 0.410000f, 0.410000f, 1.000000f};
	colors[ImGuiCol_ScrollbarGrabActive] = {0.510000f, 0.510000f, 0.510000f, 1.000000f};
	colors[ImGuiCol_CheckMark] = {0.510000f, 0.510000f, 0.510000f, 1.000000f};
	colors[ImGuiCol_SliderGrab] = {0.510000f, 0.510000f, 0.510000f, 1.000000f};
	colors[ImGuiCol_SliderGrabActive] = {0.560000f, 0.560000f, 0.560000f, 1.000000f};
	colors[ImGuiCol_Button] = {0.270000f, 0.270000f, 0.270000f, 1.000000f};
	colors[ImGuiCol_ButtonHovered] = {0.340000f, 0.340000f, 0.340000f, 1.000000f};
	colors[ImGuiCol_ButtonActive] = {1.000000f, 0.501961f, 0.000000f, 1.000000f};
	colors[ImGuiCol_Header] = {0.350000f, 0.350000f, 0.350000f, 1.000000f};
	colors[ImGuiCol_HeaderHovered] = {0.390000f, 0.390000f, 0.390000f, 1.000000f};
	colors[ImGuiCol_HeaderActive] = {0.440000f, 0.440000f, 0.440000f, 1.000000f};
	colors[ImGuiCol_Separator] = {0.240000f, 0.240000f, 0.240000f, 1.000000f};
	colors[ImGuiCol_SeparatorHovered] = {0.310000f, 0.310000f, 0.310000f, 1.000000f};
	colors[ImGuiCol_SeparatorActive] = {0.340000f, 0.340000f, 0.340000f, 1.000000f};
	colors[ImGuiCol_ResizeGrip] = {0.240000f, 0.240000f, 0.240000f, 1.000000f};
	colors[ImGuiCol_ResizeGripHovered] = {0.310000f, 0.310000f, 0.310000f, 1.000000f};
	colors[ImGuiCol_ResizeGripActive] = {0.370000f, 0.370000f, 0.370000f, 1.000000f};
	colors[ImGuiCol_Tab] = {0.313726f, 0.313726f, 0.313726f, 1.000000f};
	colors[ImGuiCol_TabHovered] = {0.579487f, 0.579487f, 0.579487f, 1.000000f};
	colors[ImGuiCol_TabActive] = {0.501961f, 0.501961f, 0.501961f, 1.000000f};
	colors[ImGuiCol_TabUnfocused] = {0.313726f, 0.313726f, 0.313726f, 1.000000f};
	colors[ImGuiCol_TabUnfocusedActive] = {0.376471f, 0.376471f, 0.376471f, 1.000000f};
	colors[ImGuiCol_DockingPreview] = {0.550000f, 0.550000f, 0.550000f, 1.000000f};
	colors[ImGuiCol_DockingEmptyBg] = {0.200000f, 0.200000f, 0.200000f, 1.000000f};
	colors[ImGuiCol_PlotLines] = {0.610000f, 0.610000f, 0.610000f, 1.000000f};
	colors[ImGuiCol_PlotLinesHovered] = {1.000000f, 0.430000f, 0.350000f, 1.000000f};
	colors[ImGuiCol_PlotHistogram] = {0.900000f, 0.700000f, 0.000000f, 1.000000f};
	colors[ImGuiCol_PlotHistogramHovered] = {1.000000f, 0.600000f, 0.000000f, 1.000000f};
	colors[ImGuiCol_TableHeaderBg] = {0.190000f, 0.190000f, 0.200000f, 1.000000f};
	colors[ImGuiCol_TableBorderStrong] = {0.310000f, 0.310000f, 0.350000f, 1.000000f};
	colors[ImGuiCol_TableBorderLight] = {0.230000f, 0.230000f, 0.250000f, 1.000000f};
	colors[ImGuiCol_TableRowBg] = {0.000000f, 0.000000f, 0.000000f, 0.000000f};
	colors[ImGuiCol_TableRowBgAlt] = {1.000000f, 1.000000f, 1.000000f, 0.060000f};
	colors[ImGuiCol_TextSelectedBg] = {0.260000f, 0.590000f, 0.980000f, 0.350000f};
	colors[ImGuiCol_DragDropTarget] = {1.000000f, 1.000000f, 0.000000f, 0.900000f};
	colors[ImGuiCol_NavHighlight] = {0.780000f, 0.880000f, 1.000000f, 1.000000f};
	colors[ImGuiCol_NavWindowingHighlight] = {1.000000f, 1.000000f, 1.000000f, 0.700000f};
	colors[ImGuiCol_NavWindowingDimBg] = {0.800000f, 0.800000f, 0.800000f, 0.200000f};
	colors[ImGuiCol_ModalWindowDimBg] = {0.440000f, 0.440000f, 0.440000f, 0.650000f};
	style->WindowMenuButtonPosition = ImGuiDir_Right;
	style->Alpha = 1.000000;
	style->WindowPadding = {4.000000, 4.000000};
	style->WindowRounding = 2.000000;
	style->WindowBorderSize = 1.000000;
	style->WindowMinSize = {32.000000, 32.000000};
	style->WindowTitleAlign = {0.000000, 0.500000};
	style->ChildRounding = 2.000000;
	style->ChildBorderSize = 1.000000;
	style->PopupRounding = 2.000000;
	style->PopupBorderSize = 1.000000;
	style->FramePadding = {8.000000, 2.000000};
	style->FrameRounding = 2.000000;
	style->FrameBorderSize = 0.000000;
	style->ItemSpacing = {4.000000, 2.000000};
	style->ItemInnerSpacing = {2.000000, 4.000000};
	style->TouchExtraPadding = {0.000000, 0.000000};
	style->IndentSpacing = 21.000000;
	style->ColumnsMinSpacing = 6.000000;
	style->ScrollbarSize = 14.000000;
	style->ScrollbarRounding = 2.000000;
	style->GrabMinSize = 10.000000;
	style->GrabRounding = 2.000000;
	style->TabRounding = 2.000000;
	style->TabBorderSize = 0.000000;
	style->DisplayWindowPadding = {19.000000, 19.000000};
	style->DisplaySafeAreaPadding = {3.000000, 3.000000};
}

bool ShowStyleSelector(const char* label) {
	static int style_idx = -1;
	ImGuiEx::Label(label);
	if (ImGui::Combo("##themes", &style_idx, "Classic\0Dark\0Light\0Blender\0Nord\0Lumix\0")) {
		switch (style_idx) {
			case 0: ImGui::StyleColorsClassic(); break;
			case 1: ImGui::StyleColorsDark(); break;
			case 2: ImGui::StyleColorsLight(); break;
			case 3: Theme_Blender(); break;
			case 4: Theme_Nord(); break;
			case 5: Theme_Lumix(); break;
		}
		return true;
	}
	return false;
}

// Demo helper function to select among loaded fonts.
// Here we use the regular BeginCombo()/EndCombo() api which is more the more flexible one.
static void ShowFontSelector(const char* label)
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

void Settings::shortcutsGUI() {
	if (!ImGui::BeginTabItem("Shortcuts")) return;

	auto& actions = m_app.getActions();
	static TextFilter filter;
	filter.gui("Filter");

	for (int i = 0; i < actions.size(); ++i)
	{
		Action& a = *actions[i];
		char button_label[64];
		a.shortcutText(Span(button_label));
		if (filter.pass(a.label_long) || filter.pass(button_label)) {
			ImGui::PushID(&a);
			ImGuiEx::Label(a.label_long);
			if (shortcutInput(button_label, a, &a == m_edit_action)) {
				m_edit_action = &a;
			}
			ImGui::PopID();
		}
	}

	ImGui::EndTabItem();
}

// copy-pasted from imgui + minor changes
static void styleGUI() {
	ImGuiStyle& style = ImGui::GetStyle();
	if (ImGui::BeginTabItem("Sizes")) {
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
			style.WindowMenuButtonPosition = ImGuiDir(window_menu_button_position - 1);
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
	
	if (ImGui::BeginTabItem("Colors")) {
		static TextFilter filter;
		filter.gui("Filter");
		ImGui::BeginChild("##colors", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NavFlattened);
		ImGui::PushItemWidth(-160);
		for (int i = 0; i < ImGuiCol_COUNT; i++)
		{
			const char* name = ImGui::GetStyleColorName(i);
			if (!filter.pass(name)) continue;
			ImGui::PushID(i);
			ImGui::ColorEdit4("##color", (float*)&style.Colors[i], ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::TextUnformatted(name);
			ImGui::PopID();
		}
		ImGui::PopItemWidth();
		ImGui::EndChild();

		ImGui::EndTabItem();
	}
}

static void generalGUI(Settings& settings) {
	ShowStyleSelector("Themes");
	if (ImGui::Button(ICON_FA_FOLDER "##open global")) {
		os::openExplorer(settings.m_app.getEngine().getFileSystem().getBasePath());
	}
	ImGui::SameLine();
	ImGui::Text("Global settings path: %s", SETTINGS_PATH);

	if (ImGui::Button(ICON_FA_FOLDER "##open_local")) {
		os::openExplorer(Path::getDir(settings.m_app_data_path.c_str()));
	}
	ImGui::SameLine();
	ImGui::Text("Local settings path: %s", settings.m_app_data_path.c_str());

	settings.m_mouse_sensitivity_x.gui("Mouse sensitivity X");
	settings.m_mouse_sensitivity_y.gui("Mouse sensitivity Y");
}

void Settings::gui() {
	if (!m_is_open) return;
	if (ImGui::Begin(ICON_FA_COG "Settings##settings", &m_is_open)) {
		if (ImGui::BeginTabBar("tabs")) {
			styleGUI();
			shortcutsGUI();

			u32 cat_idx = -1;
			for (Category& cat : m_categories) {
				++cat_idx;
				if (!ImGui::BeginTabItem(cat.name.c_str())) continue;

				if (cat.name == "General") generalGUI(*this);

				for (auto iter = m_variables.begin(), end = m_variables.end(); iter != end; ++iter) {
					Variable& var = iter.value();
					if (var.category != cat_idx) continue;
					
					ImGuiEx::Label(iter.key().c_str());
					ImGui::PushID(&var);
					auto CB = [&](bool changed) { if (changed && var.set_callback.isValid()) var.set_callback.invoke(); };
					switch (var.type) {
						case Variable::BOOL: CB(ImGui::Checkbox("##var", &var.bool_value)); break;
						case Variable::BOOL_PTR: CB(ImGui::Checkbox("##var", var.bool_ptr)); break;
						case Variable::I32: CB(ImGui::InputInt("##var", &var.i32_value)); break;
						case Variable::I32_PTR: CB(ImGui::InputInt("##var", var.i32_ptr)); break;
						case Variable::FLOAT: CB(ImGui::DragFloat("##var", &var.float_value)); break;
						case Variable::FLOAT_PTR: {
							if (var.is_angle) {
								float deg = radiansToDegrees(*var.float_ptr);
								if (ImGui::DragFloat("##var", &deg)) {
									*var.float_ptr = degreesToRadians(deg);
									if (var.set_callback.isValid()) var.set_callback.invoke();
								}
							}
							else {
								CB(ImGui::DragFloat("##var", var.float_ptr));
							}
							break;
						}
						case Variable::STRING_PTR: CB(inputString("##var", var.string_ptr)); break;
						case Variable::STRING: CB(inputString("##var", &var.string_value)); break;
					}
					ImGui::PopID();
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

bool Settings::getBool(const char* var_name, bool default_value) {
	Settings::Variable* var = findVar(*this, var_name);
	if (!var) return default_value;

	switch(var->type) {
		case Variable::BOOL: return var->bool_value;
		case Variable::BOOL_PTR:
			// use direct access through pointer, not getBool
			ASSERT(false);
			return *var->bool_ptr;
		default:
			logError("Variable ", var_name, " in settings is not a bool");
			return default_value;
	}
}

i32 Settings::getI32(const char* var_name, i32 default_value) {
	Settings::Variable* var = findVar(*this, var_name);
	if (!var) return default_value;

	switch(var->type) {
		case Variable::I32: return var->i32_value;
		case Variable::I32_PTR:
			// use direct access through pointer, not getBool
			ASSERT(false);
			return *var->i32_ptr;
		default:
			logError("Variable ", var_name, " in settings is not integer");
			return default_value;
	}
}

void Settings::setBool(const char* var_name, bool value, Storage storage) {
	// find existing
	Variable* var =  findVar(*this, var_name);
	if (var) {
		var->storage = storage;
		switch(var->type) {
			case Variable::BOOL: var->bool_value = value; return;
			case Variable::BOOL_PTR: 
				// use direct access through pointer, not setBool
				ASSERT(false);
				return;
			default:
				logError("Variable ", var_name, " in settings is not a bool");
				return;
		}
		return;
	}

	// create new
	Variable& new_var = m_variables.insert(String(var_name, m_allocator));
	new_var.bool_value = value;
	new_var.type = Variable::BOOL;
	new_var.storage = storage;
}

void Settings::setI32(const char* var_name, i32 value, Storage storage) {
	// find existing
	Variable* var =  findVar(*this, var_name);
	if (var) {
		var->storage = storage;
		switch(var->type) {
			case Variable::I32: var->i32_value = value; return;
			case Variable::I32_PTR: 
				// use direct access through pointer, not setBool
				ASSERT(false);
				return;
			default:
				logError("Variable ", var_name, " in settings is not integer");
				return;
		}
		return;
	}

	// create new
	Variable& new_var = m_variables.insert(String(var_name, m_allocator));
	new_var.i32_value = value;
	new_var.type = Variable::I32;
	new_var.storage = storage;
}

static u32 getCategory(Settings& settings, const char* category) {
	if (!category) return Settings::INVALID_CATEGORY;
	for (u32 i = 0; i < (u32)settings.m_categories.size(); ++i) {
		if (settings.m_categories[i].name == category) return i;
	}
	Settings::Category& cat = settings.m_categories.emplace(settings.m_allocator);
	cat.name = category;
	return settings.m_categories.size() - 1;
}

void Settings::registerPtr(const char* name, String* value, const char* category) {
	// if variable already exists
	Variable* var = findVar(*this, name);
	if (var) {
		var->category = getCategory(*this, category);
		if (var->type != Variable::STRING) {
			logError("Setting ", name, " already exists but is not a string");
			return;
		}
		*value = var->string_value;
		var->string_ptr = value;
		var->type = Variable::STRING_PTR;
		return;
	}

	// create variable
	Variable& new_var = m_variables.insert(String(name, m_allocator));
	new_var.string_ptr = value;
	new_var.type = Variable::STRING_PTR;
	new_var.storage = WORKSPACE;
	new_var.category = getCategory(*this, category);
}

void Settings::registerPtr(const char* name, bool* value, const char* category, Delegate<void()>* callback) {
	// if variable already exists
	Variable* var = findVar(*this, name);
	if (var) {
		var->category = getCategory(*this, category);
		if (var->type != Variable::BOOL) {
			logError("Setting ", name, " already exists but is not a bool");
			return;
		}
		*value = var->bool_value;
		var->bool_ptr = value;
		var->type = Variable::BOOL_PTR;
		if (callback) var->set_callback = *callback;
		return;
	}

	// create variable
	Variable& new_var = m_variables.insert(String(name, m_allocator));
	new_var.bool_ptr = value;
	new_var.type = Variable::BOOL_PTR;
	new_var.storage = WORKSPACE;
	if (callback) new_var.set_callback = *callback;
	new_var.category = getCategory(*this, category);
}

void Settings::registerPtr(const char* name, i32* value, const char* category) {
	// if variable already exists
	Variable* var = findVar(*this, name);
	if (var) {
		var->category = getCategory(*this, category);
		if (var->type != Variable::I32) {
			logError("Setting ", name, " already exists but is not a bool");
			return;
		}
		*value = var->bool_value;
		var->i32_ptr = value;
		var->type = Variable::I32_PTR;
		return;
	}

	// create variable
	Variable& new_var = m_variables.insert(String(name, m_allocator));
	new_var.i32_ptr = value;
	new_var.type = Variable::I32_PTR;
	new_var.storage = WORKSPACE;
	new_var.category = getCategory(*this, category);
}

void Settings::registerPtr(const char* name, float* value, const char* category, bool is_angle) {
	// if variable already exists
	Variable* var = findVar(*this, name);
	if (var) {
		var->is_angle = is_angle;
		var->category = getCategory(*this, category);
		if (var->type != Variable::FLOAT) {
			logError("Setting ", name, " already exists but is not float");
			return;
		}
		*value = var->float_value;
		var->float_ptr = value;
		var->type = Variable::FLOAT_PTR;
		return;
	}

	// create variable
	Variable& new_var = m_variables.insert(String(name, m_allocator));
	new_var.is_angle = is_angle;
	new_var.float_ptr = value;
	new_var.type = Variable::FLOAT_PTR;
	new_var.storage = WORKSPACE;
	new_var.category = getCategory(*this, category);
}

} // namespace Lumix