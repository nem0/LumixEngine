#include <imgui/imgui.h>
#include "core/crt.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/tokenizer.h"
#include "editor/studio_app.h"
#include "editor/action.h"
#include "editor/text_filter.h"
#include "editor/utils.h"
#include "engine/engine.h"
#include "engine/engine_hash_funcs.h"
#include "engine/file_system.h"
#include "settings.h"

namespace Lumix {

static const char SETTINGS_PATH[] = "studio.ini";
static const char DEFAULT_SETTINGS_PATH[] = "studio_default.ini";

static bool shortcutInput(char* button_label, Action& action, bool edit, StudioApp& app) {
	bool res = false;
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec2 prev = style.ButtonTextAlign;
	style.ButtonTextAlign.x = 0;
	static Action editing{"Temporary", "Temporary action", "Temporary action", "temporary_action", nullptr, Action::TEMPORARY };
	if (ImGui::Button(button_label, ImVec2(-30, 0))) {
		openCenterStrip("edit_shortcut_popup");
		editing.shortcut = action.shortcut;
		editing.modifiers = action.modifiers;
		app.setCaptureInput(true);
	}
	style.ButtonTextAlign = prev;
	if (beginCenterStrip("edit_shortcut_popup")) {
		ImGui::NewLine();
		alignGUICenter([&](){ ImGui::TextUnformatted("Press a key combination and then press Enter"); });
		char shortcut_text[64];
		editing.shortcutText(Span(shortcut_text));
		alignGUICenter([&](){
			ImGui::TextUnformatted(shortcut_text);
		});

		if (editing.modifiers != Action::NONE || editing.shortcut != os::Keycode::INVALID) {
			u32 num_collisions = 0;
			const Action* first_collision = nullptr;
			for (const Action* a = Action::first_action; a; a = a->next) {
				if (a == &action) continue;
				if (a->modifiers == editing.modifiers && a->shortcut == editing.shortcut && a->type != Action::TEMPORARY) {
					first_collision = a;
					++num_collisions;
				}
			}
			if (num_collisions > 0) {
				alignGUICenter([&](){ 
					ImGui::Text("This shortcut is already used by %s > %s and %d other(s)", first_collision->group.data, first_collision->label_long.data, num_collisions - 1);
				});
			}
		}

		Span<const os::Event> events = app.getEvents();
		for (const os::Event& event : events) {
			if (event.type != os::Event::Type::KEY) continue;
			if (!event.key.down) continue;

			switch (event.key.keycode) {
				case os::Keycode::ESCAPE: 
					if (editing.modifiers == Action::NONE && editing.shortcut == os::Keycode::INVALID) {
						app.setCaptureInput(false);
						ImGui::CloseCurrentPopup();
						break;
					}
					editing.modifiers = Action::NONE;
					editing.shortcut = os::Keycode::INVALID;
					break;
				case os::Keycode::RETURN:
					action.shortcut = editing.shortcut;
					action.modifiers = editing.modifiers;
					app.setCaptureInput(false);
					ImGui::CloseCurrentPopup();
					break;
				case os::Keycode::SHIFT:
					editing.modifiers |= Action::Modifiers::SHIFT;
					break;
				case os::Keycode::ALT:
					editing.modifiers |= Action::Modifiers::ALT;
					break;
				case os::Keycode::CTRL:
					editing.modifiers |= Action::Modifiers::CTRL;
					break;
				default:
					os::Keycode kc = event.key.keycode;
					const bool is_mouse = kc == os::Keycode::LBUTTON || kc == os::Keycode::RBUTTON || kc == os::Keycode::MBUTTON;
					const bool is_modifier = kc == os::Keycode::SHIFT || kc == os::Keycode::LSHIFT || kc == os::Keycode::RSHIFT || kc == os::Keycode::ALT || kc == os::Keycode::LALT ||
											kc == os::Keycode::RALT || kc == os::Keycode::CTRL || kc == os::Keycode::LCTRL || kc == os::Keycode::RCTRL;
					if (!is_mouse && !is_modifier) {
						editing.shortcut = kc;
					}
					break;
			}
		}
		endCenterStrip();
	}
	ImGui::SameLine();
	if (ImGuiEx::IconButton(ICON_FA_TRASH, "Clear")) {
		action.modifiers = Action::Modifiers::NONE;
		action.shortcut = os::Keycode::INVALID;
	}

	return res;
}

Settings::Variable::Variable(IAllocator& allocator)
	: string_value(allocator)
	, min(-FLT_MAX)
	, max(FLT_MAX)
{}


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

void MouseSensitivity::gui() {
	ImGui::PushID(this);
	if (ImGuiEx::CurvePreviewButton("curve", &values.begin()->x, &values.begin()->y, values.size(), ImVec2(130, ImGui::GetTextLineHeight()), sizeof(values[0]))) ImGui::OpenPopup("sensitivity_popup");
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_PENCIL_ALT)) ImGui::OpenPopup("sensitivity_popup");
	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
	if (ImGui::BeginPopupModal("sensitivity_popup", &open)) {
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
	registerOption("imgui_state", &m_imgui_state);
	registerOption("settings_open", &m_is_open);
	registerOption("show_settings_as_popup", &m_show_settings_as_popup, "General", "Show settings as popup");
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
	SAVE_FLOAT(TabBarOverlineSize);
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
		for (Action* a = Action::first_action; a; a = a->next) {
			if (a->name == token) {
				action = a;
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
		LOAD_FLOAT(TabBarOverlineSize)
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
					case Tokenizer::Token::STRING: var->string_value = value.value; var->type = Variable::STRING; break;
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
	m_dirty = false;
}

static void saveShortcuts(Settings& settings, OutputMemoryStream& blob) {
	blob << "keybindings = {\n";
	for (Action* a = Action::first_action; a; a = a->next) {
		blob << "\t" << a->name << " = { key = " 
			<< (int)a->shortcut << ", modifiers = "
			<< (int)a->modifiers << "},\n";
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
	m_dirty = false;
}

static const char* GetTreeLinesFlagsName(ImGuiTreeNodeFlags flags)
{
	if (flags == ImGuiTreeNodeFlags_DrawLinesNone) return "DrawLinesNone";
	if (flags == ImGuiTreeNodeFlags_DrawLinesFull) return "DrawLinesFull";
	if (flags == ImGuiTreeNodeFlags_DrawLinesToNodes) return "DrawLinesToNodes";
	return "";
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

bool styleSelectorGUI() {
	static int style_idx = -1;
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

static void sortActions() {
	PROFILE_FUNCTION();
	if (!Action::first_action) return;
	for (;;) {
		bool sorted = true;
		for (Action* a = Action::first_action; a->next; a = a->next) {
			i32 group_cmp = compareString(a->group, a->next->group);
			if (group_cmp < 0) continue;
			if (group_cmp == 0 && compareString(a->label_long, a->next->label_long) <= 0) continue;

			sorted = false;
			if (a == Action::first_action) Action::first_action = a->next;

			Action* tmp = a->next;
			// remove a from the list
			if (a->prev) a->prev->next = a->next;
			a->next->prev = a->prev;
			// push it in new position
			a->next = tmp->next;
			a->prev = tmp;
			a->prev->next = a;
			if (a->next) a->next->prev = a;

			a = a->prev;
		}
		if (sorted) break;
	}
}

static void shortcutsGUI(const TextFilter& filter, Settings& settings) {
	PROFILE_FUNCTION();
	sortActions();

	if (filter.isActive()) {
		for (Action* a = Action::first_action; a; a = a->next) {
			char button_label[64];
			a->shortcutText(Span(button_label));
			if (filter.pass(a->label_long) || filter.pass(button_label) || filter.pass(a->group)) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(a);
				ImGui::TextUnformatted("Shortcuts > ");
				ImGui::SameLine();
				ImGui::TextUnformatted(a->group);
				ImGui::SameLine();
				ImGui::TextUnformatted(" > ");
				ImGui::SameLine();
				ImGui::TextUnformatted(a->label_long);
				ImGui::TableNextColumn();
				if (shortcutInput(button_label, *a, a == settings.m_edit_action, settings.m_app)) {
					settings.m_edit_action = a;
					settings.m_dirty = true;
				}
				ImGui::PopID();
			}
		}
		return;
	}

	if (ImGui::BeginTable("shortcuts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
		for (Action* a = Action::first_action; a; a = a->next) {
			char button_label[64];
			a->shortcutText(Span(button_label));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID(a);
			ImGui::TextUnformatted(a->group);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(a->label_long);
			ImGui::TableNextColumn();
			if (shortcutInput(button_label, *a, a == settings.m_edit_action, settings.m_app)) {
				settings.m_edit_action = a;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}

// copy-pasted from imgui + minor changes
[[nodiscard]] static bool styleGUI(const TextFilter& filter) {
	ImGuiStyle& style = ImGui::GetStyle();
	bool changed = false;

	if (!filter.isActive()) {
		ImGuiEx::Label("Themes");
		if (styleSelectorGUI()) {
			changed = true;
		}
	}

	if (!filter.isActive()) ImGui::SeparatorText("Colors");
	for (int i = 0; i < ImGuiCol_COUNT; ++i) {
		const char* name = ImGui::GetStyleColorName(i);
		if (!filter.pass(name)) continue;
		if (filter.isActive()) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Style");
			ImGui::SameLine();
			ImGui::TextUnformatted(" > ");
			ImGui::SameLine();
			ImGui::TextUnformatted(name);
			ImGui::TableNextColumn();
		}
		else {
			ImGuiEx::Label(name);
		}
		
		ImGui::PushID(i);
		if (ImGui::ColorEdit4("##color", (float*)&style.Colors[i], ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
			changed = true;
		}
		ImGui::PopID();
	}

	auto labelUI = [&](const char* label){
		if (filter.pass(label)) {
			if (filter.isActive()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Style");
				ImGui::SameLine();
				ImGui::TextUnformatted(" > ");
				ImGui::SameLine();
				ImGui::TextUnformatted(label);
				ImGui::TableNextColumn();
			} else {
				ImGuiEx::Label(label);
			}
			return true;
		}
		return false;
	};

	#define dragFloat(label, ...) \
		if (labelUI(label) && ImGui::DragFloat("##" label, __VA_ARGS__)) changed = true; 

	#define combo(label, ...) \
		if (labelUI(label) && ImGui::Combo("##" label, __VA_ARGS__)) changed = true;

	#define sliderFloat2(label, ...) \
		if (labelUI(label) && ImGui::SliderFloat2("##" label, __VA_ARGS__)) changed = true;

	#define sliderFloat(label, ...) \
		if (labelUI(label) && ImGui::SliderFloat("##" label, __VA_ARGS__)) changed = true;

	if (!filter.isActive()) ImGui::SeparatorText("Main");
	sliderFloat2("Window Padding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
	sliderFloat2("Frame Padding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
	sliderFloat2("Item Spacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
	sliderFloat2("Item Inner Spacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
	sliderFloat2("Touch Extra Padding", (float*)&style.TouchExtraPadding, 0.0f, 10.0f, "%.0f");
	sliderFloat("Indent Spacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
	sliderFloat("Scrollbar Size", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
	sliderFloat("Grab Min Size", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");

	if (!filter.isActive()) ImGui::SeparatorText("Borders");
	sliderFloat("Window Border Size", &style.WindowBorderSize, 0.0f, 1.0f, "%.0f");
	sliderFloat("Child Border Size", &style.ChildBorderSize, 0.0f, 1.0f, "%.0f");
	sliderFloat("Popup Border Size", &style.PopupBorderSize, 0.0f, 1.0f, "%.0f");
	sliderFloat("Frame Border Size", &style.FrameBorderSize, 0.0f, 1.0f, "%.0f");

	if (!filter.isActive()) ImGui::SeparatorText("Rounding");
	sliderFloat("Window Rounding", &style.WindowRounding, 0.0f, 12.0f, "%.0f");
	sliderFloat("Child Rounding", &style.ChildRounding, 0.0f, 12.0f, "%.0f");
	sliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f");
	sliderFloat("Popup Rounding", &style.PopupRounding, 0.0f, 12.0f, "%.0f");
	sliderFloat("Scrollbar Rounding", &style.ScrollbarRounding, 0.0f, 12.0f, "%.0f");
	sliderFloat("Grab Rounding", &style.GrabRounding, 0.0f, 12.0f, "%.0f");

	if (!filter.isActive()) ImGui::SeparatorText("Tabs");
	sliderFloat("Tab Border Size", &style.TabBorderSize, 0.0f, 1.0f, "%.0f");
	sliderFloat("TabBar Border Size", &style.TabBarBorderSize, 0.0f, 2.0f, "%.0f");
	sliderFloat("TabBar Overline Size", &style.TabBarOverlineSize, 0.0f, 3.0f, "%.0f");
	dragFloat("Tab Close Button Min Width Selected", &style.TabCloseButtonMinWidthSelected, 0.1f, -1.0f, 100.0f, (style.TabCloseButtonMinWidthSelected < 0.0f) ? "%.0f (Always)" : "%.0f");
	dragFloat("Tab Close Button Min Width Unselected", &style.TabCloseButtonMinWidthUnselected, 0.1f, -1.0f, 100.0f, (style.TabCloseButtonMinWidthUnselected < 0.0f) ? "%.0f (Always)" : "%.0f");
	sliderFloat("Tab Rounding", &style.TabRounding, 0.0f, 12.0f, "%.0f");

	if (!filter.isActive()) ImGui::SeparatorText("Tables");
	sliderFloat2("Cell Padding", (float*)&style.CellPadding, 0.0f, 20.0f, "%.0f");
	if (labelUI("Table Angled Headers Angle")) {
		if (ImGui::SliderAngle("##Table Angled Headers Angle", &style.TableAngledHeadersAngle, -50.0f, +50.0f)) {
			changed = true;
		}
	}
	sliderFloat2("Table Angled Headers Text Align", (float*)&style.TableAngledHeadersTextAlign, 0.0f, 1.0f, "%.2f");

	if (!filter.isActive()) ImGui::SeparatorText("Trees");
	if (labelUI("Tree Lines Flags")) {
		bool combo_open = ImGui::BeginCombo("##TreeLinesFlags", GetTreeLinesFlagsName(style.TreeLinesFlags));
		if (combo_open)
		{
			const ImGuiTreeNodeFlags options[] = { ImGuiTreeNodeFlags_DrawLinesNone, ImGuiTreeNodeFlags_DrawLinesFull, ImGuiTreeNodeFlags_DrawLinesToNodes };
			for (ImGuiTreeNodeFlags option : options)
				if (ImGui::Selectable(GetTreeLinesFlagsName(option), style.TreeLinesFlags == option)) {
					style.TreeLinesFlags = option;
					changed = true;
				}
			ImGui::EndCombo();
		}
	}
	sliderFloat("Tree Lines Size", &style.TreeLinesSize, 0.0f, 2.0f, "%.0f");
	sliderFloat("Tree Lines Rounding", &style.TreeLinesRounding, 0.0f, 12.0f, "%.0f");

	if (!filter.isActive()) ImGui::SeparatorText("Windows");
	sliderFloat2("Window Title Align", (float*)&style.WindowTitleAlign, 0.0f, 1.0f, "%.2f");
	sliderFloat("Window Border Hover Padding", &style.WindowBorderHoverPadding, 1.0f, 20.0f, "%.0f");
	int window_menu_button_position = style.WindowMenuButtonPosition + 1;
	if (labelUI("Window Menu Button Position")) {
		if (ImGui::Combo("##WindowMenuButtonPosition", (int*)&window_menu_button_position, "None\0Left\0Right\0")) {
			style.WindowMenuButtonPosition = (ImGuiDir)(window_menu_button_position - 1);
			changed = true;
		}
	}

	if (!filter.isActive()) ImGui::SeparatorText("Widgets");
	if (labelUI("Color Button Position")) {
		if (ImGui::Combo("##ColorButtonPosition", (int*)&style.ColorButtonPosition, "Left\0Right\0")) {
			changed = true;
		}
	}
	sliderFloat2("Button Text Align", (float*)&style.ButtonTextAlign, 0.0f, 1.0f, "%.2f");
	sliderFloat2("Selectable Text Align", (float*)&style.SelectableTextAlign, 0.0f, 1.0f, "%.2f");
	sliderFloat("Separator Text Border Size", &style.SeparatorTextBorderSize, 0.0f, 10.0f, "%.0f");
	sliderFloat2("Separator Text Align", (float*)&style.SeparatorTextAlign, 0.0f, 1.0f, "%.2f");
	sliderFloat2("Separator Text Padding", (float*)&style.SeparatorTextPadding, 0.0f, 40.0f, "%.0f");
	sliderFloat("Log Slider Deadzone", &style.LogSliderDeadzone, 0.0f, 12.0f, "%.0f");
	sliderFloat("Image Border Size", &style.ImageBorderSize, 0.0f, 1.0f, "%.0f");

	if (!filter.isActive()) {
		ImGui::SeparatorText("Tooltips");
		for (int n = 0; n < 2; n++) {
			if (ImGui::TreeNodeEx(n == 0 ? "HoverFlagsForTooltipMouse" : "HoverFlagsForTooltipNav")) {
				ImGuiHoveredFlags* p = (n == 0) ? &style.HoverFlagsForTooltipMouse : &style.HoverFlagsForTooltipNav;
				ImGui::CheckboxFlags("ImGuiHoveredFlags_DelayNone", p, ImGuiHoveredFlags_DelayNone);
				ImGui::CheckboxFlags("ImGuiHoveredFlags_DelayShort", p, ImGuiHoveredFlags_DelayShort);
				ImGui::CheckboxFlags("ImGuiHoveredFlags_DelayNormal", p, ImGuiHoveredFlags_DelayNormal);
				ImGui::CheckboxFlags("ImGuiHoveredFlags_Stationary", p, ImGuiHoveredFlags_Stationary);
				ImGui::CheckboxFlags("ImGuiHoveredFlags_NoSharedDelay", p, ImGuiHoveredFlags_NoSharedDelay);
				ImGui::TreePop();
			}
		}
	}

	if (!filter.isActive()) ImGui::SeparatorText("Misc");
	sliderFloat2("Display Window Padding", (float*)&style.DisplayWindowPadding, 0.0f, 30.0f, "%.0f"); 
	sliderFloat2("Display Safe Area Padding", (float*)&style.DisplaySafeAreaPadding, 0.0f, 30.0f, "%.0f"); 
	sliderFloat("Docking Separator Size", &style.DockingSeparatorSize, 0.0f, 12.0f, "%.0f");

	#undef dragFloat
	#undef sliderFloat
	#undef sliderFloat2
	return changed;
}

static void generalGUI(Settings& settings) {
	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGuiEx::Label("Global settings path");
	ImGui::TableNextColumn();
	if (ImGui::Button(ICON_FA_FOLDER "##open global")) {
		os::openExplorer(settings.m_app.getEngine().getFileSystem().getBasePath());
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(SETTINGS_PATH);

	ImGui::TableNextColumn();
	ImGuiEx::Label("Local settings path");
	ImGui::TableNextColumn();
	if (ImGui::Button(ICON_FA_FOLDER "##open_local")) {
		os::openExplorer(Path::getDir(settings.m_app_data_path.c_str()));
	}
	ImGui::SameLine();
	ImGui::TextUnformatted(settings.m_app_data_path.c_str());

	ImGui::TableNextColumn();
	ImGuiEx::Label("Mouse sensitivity X");
	ImGui::TableNextColumn();
	settings.m_mouse_sensitivity_x.gui();

	ImGui::TableNextColumn();
	ImGuiEx::Label("Mouse sensitivity Y");
	ImGui::TableNextColumn();
	settings.m_mouse_sensitivity_y.gui();
}

static i32 clampInt(i32 value, float min, float max) {
	return (i32)clamp((float)value, min, max);
}

void Settings::commandPaletteUI(const TextFilter& filter) {
	if (ImGui::BeginTable("settings_table", 2, ImGuiTableFlags_RowBg)) {
		shortcutsGUI(filter, *this);
		if (styleGUI(filter)) {
			m_dirty = true;
		}
		iterVars(filter, 0);
		ImGui::EndTable();
	}
}

void Settings::iterVars(const TextFilter& filter, u32 selected_tab) {
	for (auto iter = m_variables.begin(), end = m_variables.end(); iter != end; ++iter) {
		Variable& var = iter.value();
		if (var.category == -1) continue;
		const char* cat_name = m_categories[var.category].name.c_str();
		if (!filter.isActive()) { if(var.category != selected_tab - 2) continue; }
		else if (!filter.pass(var.label) && !filter.pass(cat_name)) continue;
		
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		if (filter.isActive()) {
			ImGui::TextUnformatted(cat_name);
			ImGui::SameLine();
			ImGui::TextUnformatted(" > ");
			ImGui::SameLine();
		}
		ImGui::TextUnformatted(var.label);
		ImGui::TableNextColumn();
		ImGui::PushID(&var);
		auto CB = [&](bool changed) {
			if (!changed) return changed;
			if (var.set_callback.isValid()) var.set_callback.invoke();
			m_dirty = true;
			return changed;
		};
		switch (var.type) {
			case Variable::BOOL: CB(ImGui::Checkbox("##var", &var.bool_value)); break;
			case Variable::BOOL_PTR: CB(ImGui::Checkbox("##var", var.bool_ptr)); break;
			case Variable::I32: 
				if (CB(ImGui::InputInt("##var", &var.i32_value))) {
					var.i32_value = clampInt(var.i32_value, var.min, var.max);
				}
			break;
			case Variable::I32_PTR: 
				if (CB(ImGui::InputInt("##var", var.i32_ptr))) {
					*var.i32_ptr = clampInt(*var.i32_ptr, var.min, var.max);
				}
				break;
			case Variable::FLOAT: CB(ImGui::DragFloat("##var", &var.float_value, 1, var.min, var.max)); break;
			case Variable::FLOAT_PTR: {
				if (var.is_angle) {
					float deg = radiansToDegrees(*var.float_ptr);
					if (ImGui::DragFloat("##var", &deg, 1, var.min, var.max)) {
						*var.float_ptr = degreesToRadians(deg);
						if (var.set_callback.isValid()) var.set_callback.invoke();
						m_dirty = true;
					}
				}
				else {
					CB(ImGui::DragFloat("##var", var.float_ptr, 1, var.min, var.max));
				}
				break;
			}
			case Variable::STRING_PTR: CB(inputString("##var", var.string_ptr)); break;
			case Variable::STRING: CB(inputString("##var", &var.string_value)); break;
		}
		ImGui::PopID();
	}			
}

void Settings::gui() {
	bool is_popup = m_show_settings_as_popup;
	
	if (m_app.checkShortcut(m_toggle_ui_action, true)) {
		if (is_popup) ImGui::OpenPopup(ICON_FA_COG "Settings##settings");
		else m_is_open = !m_is_open;
	}
	if (is_popup) m_is_open = ImGui::IsPopupOpen(ICON_FA_COG "Settings##settings");
	if (!m_is_open) return;
	
	bool open;
	if (is_popup) {
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 size = viewport->Size;
		size.x *= 0.4f;
		size.y *= 0.8f;
		ImVec2 pos = ImVec2(viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f, viewport->Pos.y + (viewport->Size.y - size.y) * 0.5f);
		ImGui::SetNextWindowPos(pos);
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		open = ImGui::BeginPopup(ICON_FA_COG "Settings##settings", ImGuiWindowFlags_NoNavInputs | (m_dirty ? ImGuiWindowFlags_UnsavedDocument : 0));
	}
	else {
		open = ImGui::Begin(ICON_FA_COG "Settings##settings", &m_is_open, m_dirty ? ImGuiWindowFlags_UnsavedDocument : 0);
	}

	if (open) {
		static u32 selected = 0;
		
		static TextFilter filter;
		if (m_app.checkShortcut(m_focus_search)) ImGui::SetKeyboardFocusHere();
		filter.gui("Filter", -1, is_popup && ImGui::IsWindowAppearing(), &m_focus_search, false);
		ImGui::Separator();

		if (filter.isActive()) {
			if (ImGui::BeginTable("settings_table", 2, ImGuiTableFlags_RowBg)) {
				shortcutsGUI(filter, *this);
				if (styleGUI(filter)) m_dirty = true;
				iterVars(filter, selected);
				ImGui::EndTable();
			}
		}
		else {
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
			if (ImGui::BeginChild("left_pane", ImVec2(150, 0), ImGuiChildFlags_AutoResizeX )) {
				auto vtab = [](const char* label, bool is_selected){
					if (is_selected) {
						ImGui::Dummy(ImVec2(15, 1));
						ImGui::SameLine();
					}
					const ImGuiStyle& style = ImGui::GetStyle();
					ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[is_selected ? ImGuiCol_TabActive : ImGuiCol_Tab]);
					bool res = false;
					if (ImGui::Button(label, ImVec2(-1, 0))) {
						res = true;
					}
					ImGui::PopStyleColor();
					return res;
				};
				
				if (vtab("Shortcuts", selected == 0)) selected = 0; 
				if (vtab("Style", selected == 1)) selected = 1; 

				for (Category& cat : m_categories) {
					const u32 idx = u32(&cat - m_categories.begin());
					bool is_selected = selected == idx + 2;
					if (vtab(cat.name.c_str(), is_selected)) selected = idx + 2;
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::SameLine();

			if (ImGui::BeginChild("right_pane", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding)) {
				if (selected == 0) shortcutsGUI(filter, *this);
				else if (selected == 1) {
					if (styleGUI(filter)) m_dirty = true;
				}
				else if (ImGui::BeginTable("settings_table", 2, ImGuiTableFlags_RowBg)) {
					if (m_categories[selected - 2].name == "General") generalGUI(*this);
					iterVars(filter, selected);
					ImGui::EndTable();
				}
			}
			ImGui::EndChild();
		}
		if (is_popup) ImGui::EndPopup();
	}
	if (!is_popup) ImGui::End();
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

const char* Settings::getString(const char* var_name, const char* default_value) {
	Settings::Variable* var = findVar(*this, var_name);
	if (!var) return default_value;

	switch(var->type) {
		case Variable::STRING: return var->string_value.c_str();
		case Variable::STRING_PTR:
			// use direct access through pointer, not getString
			ASSERT(false);
			return var->string_ptr->c_str();
		default:
			logError("Variable ", var_name, " in settings is not a string");
			return default_value;
	}
}

float Settings::getFloat(const char* var_name, float default_value) {
	Settings::Variable* var = findVar(*this, var_name);
	if (!var) return default_value;

	switch(var->type) {
		case Variable::FLOAT: return var->float_value;
		case Variable::FLOAT_PTR:
			// use direct access through pointer, not getBool
			ASSERT(false);
			return *var->float_ptr;
		default:
			logError("Variable ", var_name, " in settings is not a float");
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

void Settings::setString(const char* var_name, const char* value, Storage storage) {
	// find existing
	Variable* var =  findVar(*this, var_name);
	if (var) {
		var->storage = storage;
		switch(var->type) {
			case Variable::STRING: var->string_value = value; return;
			case Variable::STRING_PTR: 
				// use direct access through pointer, not setString
				ASSERT(false);
				return;
			default:
				logError("Variable ", var_name, " in settings is not a string");
				return;
		}
		return;
	}

	// create new
	Variable& new_var = m_variables.insert(String(var_name, m_allocator));
	new_var.string_value = value;
	new_var.type = Variable::STRING;
	new_var.storage = storage;
}

void Settings::setFloat(const char* var_name, float value, Storage storage) {
	// find existing
	Variable* var =  findVar(*this, var_name);
	if (var) {
		var->storage = storage;
		switch(var->type) {
			case Variable::FLOAT: var->float_value = value; return;
			case Variable::FLOAT_PTR: 
				// use direct access through pointer, not setFloat
				ASSERT(false);
				return;
			default:
				logError("Variable ", var_name, " in settings is not a float");
				return;
		}
		return;
	}

	// create new
	Variable& new_var = m_variables.insert(String(var_name, m_allocator));
	new_var.float_value = value;
	new_var.type = Variable::FLOAT;
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

Settings::Variable& Settings::registerOption(const char* name, String* value, const char* category, const char* label) {
	// if variable already exists
	Variable* var = findVar(*this, name);
	if (var) {
		var->label = label;
		var->category = getCategory(*this, category);
		if (var->type != Variable::STRING) {
			logError("Setting ", name, " already exists but is not a string");
			return *var;
		}
		*value = var->string_value;
		var->string_ptr = value;
		var->type = Variable::STRING_PTR;
		return *var;
	}

	// create variable
	Variable& new_var = m_variables.insert(String(name, m_allocator));
	new_var.label = label;
	new_var.string_ptr = value;
	new_var.type = Variable::STRING_PTR;
	new_var.storage = WORKSPACE;
	new_var.category = getCategory(*this, category);
	return new_var;
}

Settings::Variable& Settings::registerOption(const char* name, bool* value, const char* category, const char* label, const Delegate<void()>* callback) {
	// if variable already exists
	Variable* var = findVar(*this, name);
	if (var) {
		var->label = label;
		var->category = getCategory(*this, category);
		if (var->type != Variable::BOOL) {
			logError("Setting ", name, " already exists but is not a bool");
			return *var;
		}
		*value = var->bool_value;
		var->bool_ptr = value;
		var->type = Variable::BOOL_PTR;
		if (callback) var->set_callback = *callback;
		return *var;
	}

	// create variable
	Variable& new_var = m_variables.insert(String(name, m_allocator));
	new_var.label = label;
	new_var.bool_ptr = value;
	new_var.type = Variable::BOOL_PTR;
	new_var.storage = WORKSPACE;
	if (callback) new_var.set_callback = *callback;
	new_var.category = getCategory(*this, category);\
	return new_var;
}

Settings::Variable& Settings::registerOption(const char* name, i32* value, const char* category, const char* label) {
	// if variable already exists
	Variable* var = findVar(*this, name);
	if (var) {
		var->label = label;
		var->category = getCategory(*this, category);
		if (var->type != Variable::I32) {
			logError("Setting ", name, " already exists but is not a bool");
			return *var;
		}
		*value = var->bool_value;
		var->i32_ptr = value;
		var->type = Variable::I32_PTR;
		return *var;
	}

	// create variable
	Variable& new_var = m_variables.insert(String(name, m_allocator));
	new_var.label = label;
	new_var.i32_ptr = value;
	new_var.type = Variable::I32_PTR;
	new_var.storage = WORKSPACE;
	new_var.category = getCategory(*this, category);
	return new_var;
}

Settings::Variable& Settings::registerOption(const char* name, float* value, const char* category, const char* label) {
	// if variable already exists
	Variable* var = findVar(*this, name);
	if (var) {
		var->label = label;
		var->category = getCategory(*this, category);
		if (var->type == Variable::I32) {
			*value = (float)var->i32_value;
			var->float_ptr = value;
			var->type = Variable::FLOAT_PTR;
			return *var;
		}
		if (var->type != Variable::FLOAT) {
			logError("Setting ", name, " already exists but is not float");
			return *var;
		}
		*value = var->float_value;
		var->float_ptr = value;
		var->type = Variable::FLOAT_PTR;
		return *var;
	}

	// create variable
	Variable& new_var = m_variables.insert(String(name, m_allocator));
	new_var.label = label;
	new_var.float_ptr = value;
	new_var.type = Variable::FLOAT_PTR;
	new_var.storage = WORKSPACE;
	new_var.category = getCategory(*this, category);
	return new_var;
}

} // namespace Lumix