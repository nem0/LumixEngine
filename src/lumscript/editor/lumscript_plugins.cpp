#include <imgui/imgui.h>
#include <imgui/imgui_user.h>
#include "lumscript/capi.h"
#include "lumscript/lumscript_resource.h"
#include "core/log.h"
#include "core/path.h"
#include "editor/action.h"
#include "editor/studio_app.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/editor_asset.h"
#include "editor/utils.h"
#include "engine/engine.h"
#include "engine/component_uid.h"
#include "engine/file_system.h"
#include "engine/world.h"
#include "editor/world_editor.h"
#include "lumscript/lumscript_module.h"
#include "core/array.h"
#include "core/stream.h"
#include "../../external/lumscript/capi.h"
#include "../../external/lumscript/arena.h"

namespace Lumix {

namespace LumScriptTokens {

static inline const u32 token_colors[] = {
	IM_COL32(0xFF, 0x00, 0xFF, 0xff),
	IM_COL32(0xe1, 0xe1, 0xe1, 0xff),
	IM_COL32(0xf7, 0xc9, 0x5c, 0xff),
	IM_COL32(0xE5, 0x8A, 0xC9, 0xff),
	IM_COL32(0x93, 0xDD, 0xFA, 0xff),
	IM_COL32(0x67, 0x6b, 0x6f, 0xff),
};

enum class TokenType : u8 {
	EMPTY,
	IDENTIFIER,
	NUMBER,
	KEYWORD,
	OPERATOR,
	COMMENT,
};

static bool isWordChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool tokenize(const char* str, u32& token_len, u8& token_type, u8) {
	static const char* keywords[] = {
		"and",
		"as",
		"bool",
		"case",
		"const",
		"defer",
		"else",
		"enum",
		"f32",
		"f64",
		"false",
		"fn",
		"i8",
		"i16",
		"i32",
		"i64",
		"if",
		"import",
		"match",
		"null",
		"not",
		"or",
		"ref",
		"return",
		"struct",
		"true",
		"u8",
		"u16",
		"u32",
		"u64",
		"var",
		"void",
		"while",
	};

	const char* c = str;
	if (!*c) return false;

	if (c[0] == '/' && c[1] == '/') {
		token_type = (u8)TokenType::COMMENT;
		while (*c) ++c;
		token_len = u32(c - str);
		return *c;
	}

	const char operators[] = "*/+-%.<>;=(),:{}!";
	for (char op : operators) {
		if (*c == op) {
			token_type = (u8)TokenType::OPERATOR;
			token_len = 1;
			return *c;
		}
	}

	if (*c >= '0' && *c <= '9') {
		token_type = (u8)TokenType::NUMBER;
		while (*c >= '0' && *c <= '9') ++c;
		if (*c == '.') {
			++c;
			while (*c >= '0' && *c <= '9') ++c;
		}
		token_len = u32(c - str);
		return *c;
	}

	if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_') {
		token_type = (u8)TokenType::IDENTIFIER;
		while (isWordChar(*c)) ++c;
		token_len = u32(c - str);
		const StringView token_view(str, str + token_len);
		for (const char* kw : keywords) {
			if (equalStrings(kw, token_view)) {
				token_type = (u8)TokenType::KEYWORD;
				break;
			}
		}
		return *c;
	}

	token_type = (u8)TokenType::IDENTIFIER;
	token_len = 1;
	++c;
	return *c;
}

} // namespace LumScriptTokens

static Action g_toggle_lumscript_breakpoint{"LumScript", "Toggle breakpoint", "Toggle breakpoint at cursor", "lumscript_toggle_breakpoint", ICON_FA_CIRCLE, Action::Type::NORMAL};

struct LumScriptDebuggerWindow;
static LumScriptDebuggerWindow* g_lumscript_debugger = nullptr;
static bool toggleLumScriptBreakpoint(StudioApp& app, const Path& source, u32 line);

struct LumScriptEditorWindow final : AssetEditorWindow {
	LumScriptEditorWindow(const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_path(path)
		, m_message(app.getAllocator())
	{
		m_editor = createCodeEditor(app);
		m_editor->setTokenColors(LumScriptTokens::token_colors);
		m_editor->setTokenizer(&LumScriptTokens::tokenize);
		m_editor->focus();

		OutputMemoryStream blob(app.getAllocator());
		if (app.getEngine().getFileSystem().getContentSync(path, blob)) {
			m_editor->setText(StringView((const char*)blob.data(), (u32)blob.size()));
		}
	}

	const char* getName() const override { return "lumscript_editor"; }
	const Path& getPath() override { return m_path; }

	void fileChangedExternally() override {
		OutputMemoryStream editor_blob(m_app.getAllocator());
		OutputMemoryStream file_blob(m_app.getAllocator());
		m_editor->serializeText(editor_blob);
		if (!m_app.getEngine().getFileSystem().getContentSync(m_path, file_blob)) return;
		if (editor_blob.size() == file_blob.size() && memcmp(editor_blob.data(), file_blob.data(), editor_blob.size()) == 0) {
			m_dirty = false;
		}
	}

	void save() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		m_app.getAssetBrowser().saveResource(m_path, blob);
		m_dirty = false;
	}

	void check() {
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		String diagnostics_message(m_app.getAllocator());
		struct ImportContext {
			FileSystem* filesystem;
			IAllocator* allocator;
			Array<OutputMemoryStream> sources;

			ImportContext(FileSystem& filesystem, IAllocator& allocator)
				: filesystem(&filesystem)
				, allocator(&allocator)
				, sources(allocator)
			{}
		};
		struct ImportResolverContext {
			ls_module* module = nullptr;
			ImportContext* import_ctx = nullptr;
		};
		auto import_resolver = [](void* userdata, ls_string_view path, ls_string_view alias, ls_string_view* source) -> int {
			ImportResolverContext* ctx = (ImportResolverContext*)userdata;
			if (!ctx || !ctx->module || !ctx->import_ctx) return 0;
			StringView path_view(path.begin, path.end);
			if (startsWith(path_view, "core:")) {
				StringView name = path_view.withoutLeft(5);
				const bool has_lum_extension = endsWith(name, ".lum");
				Path file_path = has_lum_extension ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".lum");
				OutputMemoryStream& import_blob = ctx->import_ctx->sources.emplace(*ctx->import_ctx->allocator);
				if (!ctx->import_ctx->filesystem->getContentSync(file_path, import_blob)) {
					ctx->import_ctx->sources.pop();
					return 0;
				}
				*source = ls_string_view{(const char*)import_blob.data(), (const char*)import_blob.data() + import_blob.size()};
				return 1;
			}
			return 0;
		};
		ImportContext import_ctx(m_app.getEngine().getFileSystem(), m_app.getAllocator());
		ImportResolverContext resolver_ctx = {};
		ls_host host = {};
		ls_default_arena_create(&host.arena);
		host.diagnostics_userdata = &diagnostics_message;
		host.print = [](void* userdata, ls_string_view msg) {
			((String*)userdata)->append(StringView(msg.begin, msg.end));
		};
		ls_module* module = ls_module_create(&host);
		if (module) {
			resolver_ctx.module = module;
			resolver_ctx.import_ctx = &import_ctx;
			if (ls_module_compile(module,
				ls_string_view{(const char*)blob.data(), (const char*)blob.data() + blob.size()},
				ls_string_view{m_path.c_str(), m_path.c_str() + stringLength(m_path.c_str())},
				import_resolver,
				&resolver_ctx))
			{
				m_message = "OK";
				logInfo("LumScript check OK: ", m_path);
			}
			else {
				m_message = diagnostics_message;
				const StringView diagnostics = diagnostics_message;
				if (const char* line_marker = find(diagnostics, ": line ")) {
					const StringView error_path(diagnostics.begin, line_marker);
					line_marker += stringLength(": line ");
					i32 line;
					if (equalStrings(error_path, m_path.c_str())
						&& fromCString(StringView(line_marker, diagnostics.end), line)
						&& line > 0)
					{
						m_editor->underlineTokens(line - 1, 0, 0xffFFffFF, diagnostics_message.c_str());
					}
				}
				logError("LumScript check failed: ", m_path, ": ", diagnostics_message);
			}
			ls_module_destroy(module);
		}
		else {
			m_message = "Failed to allocate LumScript module";
			logError("LumScript check failed: ", m_path, ": Failed to allocate LumScript module");
		}
		ls_default_arena_destroy(&host.arena);
	}

	void windowGUI() override {
		CommonActions& actions = m_app.getCommonActions();
		if (ImGui::BeginMenuBar()) {
			if (actions.save.iconButton(m_dirty, &m_app)) save();
			if (actions.open_externally.iconButton(true, &m_app)) m_app.getAssetBrowser().openInExternalEditor(m_path);
			if (actions.view_in_browser.iconButton(true, &m_app)) m_app.getAssetBrowser().locate(m_path);
			if (ImGuiEx::IconButton(ICON_FA_CHECK, "Check")) check();
			ImGui::EndMenuBar();
		}

		if (m_message.length() > 0) {
			ImGui::TextUnformatted(m_message.c_str());
			ImGui::Separator();
		}
		if (m_editor->gui("lumscript_editor", ImGui::GetContentRegionAvail(), m_app.getMonospaceFont(), m_app.getDefaultFont())) {
			m_dirty = true;
		}
		Action* toggle_breakpoint = m_app.getAction("lumscript_toggle_breakpoint");
		if (toggle_breakpoint && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && m_app.checkShortcut(*toggle_breakpoint)) toggleBreakpointAtCursor();
	}

	void toggleBreakpointAtCursor() {
		const u32 line = m_editor->getCursorLine() + 1;
		const bool enabled = toggleLumScriptBreakpoint(m_app, m_path, line);
		m_editor->setBreakpoint(line - 1, enabled);
	}

	Path m_path;
	UniquePtr<CodeEditor> m_editor;
	String m_message;
};

struct LumScriptAssetPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	explicit LumScriptAssetPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("lum", LumScriptResource::TYPE);
	}

	void addSubresources(AssetCompiler& compiler, const Path& path, AtomicI32&) override {
		compiler.addResource(LumScriptResource::TYPE, path);
	}

	void openEditor(const Path& path) override {
		auto win = UniquePtr<LumScriptEditorWindow>::create(m_app.getAllocator(), path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool compile(const Path& src) override {
		// For LumScript, we just copy the source file as-is
		// The runtime will parse and compile it when loaded
		return m_app.getAssetCompiler().copyCompile(src);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "lum"; }
	
	void createResource(OutputMemoryStream& content) override {
		const char* template_str = R"(fn update(dt : f32) : void {
	// Update logic here
}
)";
		content.write(template_str, stringLength(template_str));
	}
	
	const char* getIcon() const override { return ICON_FA_FILE_CODE; }
	const char* getLabel() const override { return "LumScript"; }
	ResourceType getResourceType() const override { return LumScriptResource::TYPE; }

	StudioApp& m_app;
};

struct LumScriptDebuggerWindow final : StudioApp::GUIPlugin {
	struct Breakpoint {
		Path source;
		u32 line;
	};

	explicit LumScriptDebuggerWindow(StudioApp& app)
		: m_app(app)
		, m_breakpoints(app.getAllocator())
	{
		g_lumscript_debugger = this;
	}

	~LumScriptDebuggerWindow() {
		if (g_lumscript_debugger == this) g_lumscript_debugger = nullptr;
	}

	const char* getName() const override { return "lumscript_debugger"; }
	bool isOpen() const { return m_is_open; }
	void setOpen(bool open) { m_is_open = open; }

	bool toggleBreakpoint(const Path& source, u32 line) {
		World* world = m_app.getWorldEditor().getWorld();
		LumScriptModule* module = world ? static_cast<LumScriptModule*>(world->getModule("lumscript")) : nullptr;
		for (i32 i = 0; i < m_breakpoints.size(); ++i) {
			if (m_breakpoints[i].line != line || m_breakpoints[i].source != source) continue;
			removeBreakpoint(module, source, line);
			m_breakpoints.erase(i);
			return false;
		}
		addBreakpoint(module, source, line);
		return true;
	}

	void addBreakpoint(LumScriptModule* module, const Path& source, u32 line) {
		if (hasBreakpoint(source, line)) return;
		m_breakpoints.emplace(Breakpoint{source, line});
		if (module && module->getDebugRuntime()) module->setDebugBreakpoint(source, line);
	}

	void removeBreakpoint(LumScriptModule* module, const Path& source, u32 line) {
		if (module && module->getDebugRuntime()) module->removeDebugBreakpoint(source, line);
	}

	void applyBreakpoints(LumScriptModule* module) {
		if (!module->getDebugRuntime()) return;
		for (const Breakpoint& breakpoint : m_breakpoints) module->setDebugBreakpoint(breakpoint.source, breakpoint.line);
	}

	void update(float) override {
		World* world = m_app.getWorldEditor().getWorld();
		LumScriptModule* module = world ? static_cast<LumScriptModule*>(world->getModule("lumscript")) : nullptr;
		if (module) applyBreakpoints(module);
	}

	void onGUI() override {
		if (!m_is_open) return;
		ImGui::SetNextWindowDockID(m_app.getDockspaceID(), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("LumScript Debugger", &m_is_open)) {
			ImGui::End();
			return;
		}

		World* world = m_app.getWorldEditor().getWorld();
		LumScriptModule* module = world ? static_cast<LumScriptModule*>(world->getModule("lumscript")) : nullptr;
		if (!module) {
			ImGui::TextDisabled("No active LumScript world module.");
			ImGui::End();
			return;
		}

		if (m_source[0] == '\0' && !module->getDebugPath().isEmpty()) copyString(m_source, module->getDebugPath().c_str());
		ls_runtime* runtime = module->getDebugRuntime();
		applyBreakpoints(module);

		bool enabled = m_debug_enabled;
		if (ImGui::Checkbox("Enable", &enabled)) {
			m_debug_enabled = enabled;
			module->setDebugEnabled(enabled);
		}
		ImGui::SameLine();
		if (runtime && ImGui::Button("Continue")) ls_debug_resume(runtime, LS_DEBUG_CONTINUE);
		ImGui::SameLine();
		if (runtime && ImGui::Button("Step over")) ls_debug_resume(runtime, LS_DEBUG_STEP_OVER);
		ImGui::SameLine();
		if (runtime && ImGui::Button("Step into")) ls_debug_resume(runtime, LS_DEBUG_STEP_INTO);
		ImGui::SameLine();
		if (runtime && ImGui::Button("Step out")) ls_debug_resume(runtime, LS_DEBUG_STEP_OUT);

		ImGui::Separator();
		ImGui::TextUnformatted("Breakpoint");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-150);
		ImGui::InputText("##source", m_source, sizeof(m_source));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		ImGui::InputInt("##line", &m_line);
		ImGui::SameLine();
		if (ImGui::Button("Add" ) && m_line > 0) {
			Path source(m_source);
			addBreakpoint(module, source, (u32)m_line);
		}

		const bool suspended = runtime && ls_debug_is_suspended(runtime);
		ls_debug_event event = {};
		if (suspended) ls_debug_pause_event(runtime, &event);

		ImGui::BeginChild("debugger_body", ImVec2(0, 0));
		ImGui::BeginChild("debugger_breakpoints", ImVec2(280, 0), true);
		ImGui::TextUnformatted("Breakpoints");
		ImGui::Separator();
		for (i32 i = 0; i < m_breakpoints.size(); ++i) {
			Breakpoint& breakpoint = m_breakpoints[i];
			ImGui::PushID((int)i);
			ImGui::TextWrapped("%s:%u", breakpoint.source.c_str(), breakpoint.line);
			if (ImGui::SmallButton("Remove")) {
				removeBreakpoint(module, breakpoint.source, breakpoint.line);
				m_breakpoints.erase(i);
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("debugger_inspection", ImVec2(0, 0), true);
		if (!suspended) {
			ImGui::TextDisabled(runtime ? "Running" : "Waiting for the game runtime");
		}
		else {
			ImGui::Text("Paused  %.*s:%u:%u  (%s)", int(event.location.source_name.end - event.location.source_name.begin), event.location.source_name.begin,
				event.location.line, event.location.column, pauseReason(event.reason));
			ImGui::SeparatorText("Call stack");
			ImGui::BeginChild("debugger_callstack", ImVec2(0, 115), true);
			for (u32 i = 0, n = ls_debug_stack_depth(runtime); i < n; ++i) {
				ls_debug_location location;
				ls_debug_frame_location(runtime, i, &location);
				const ls_string_view name = ls_debug_frame_function_name(runtime, i);
				ImGui::Text("%u  %.*s  %.*s:%u", i, int(name.end - name.begin), name.begin,
					int(location.source_name.end - location.source_name.begin), location.source_name.begin, location.line);
			}
			ImGui::EndChild();
			ImGui::SeparatorText("Locals");
			if (ImGui::BeginTable("lumscript_locals", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 145))) {
				ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("Value"); ImGui::TableHeadersRow();
				for (u32 i = 0, n = ls_debug_frame_local_count(runtime, 0); i < n; ++i) {
					const ls_string_view name = ls_debug_local_name(runtime, 0, i); const ls_type_kind kind = ls_debug_local_kind(runtime, 0, i); u32 size = 0;
					void* value = ls_debug_local_value(runtime, 0, i, &size);
					drawVariable(name, kind, value, size);
				}
				ImGui::EndTable();
			}
			ImGui::SeparatorText("Globals");
			if (ImGui::BeginTable("lumscript_globals", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
				ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("Value"); ImGui::TableHeadersRow();
				for (u32 i = 0, n = ls_debug_global_count(runtime); i < n; ++i) {
					const ls_string_view name = ls_debug_global_name(runtime, i); const ls_type_kind kind = ls_debug_global_kind(runtime, i); u32 size = 0;
					void* value = ls_debug_global_value(runtime, i, &size);
					drawVariable(name, kind, value, size);
				}
				ImGui::EndTable();
			}
		}
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::End();
	}

	private:
	static const char* pauseReason(ls_debug_pause_reason reason) {
		switch (reason) {
			case LS_DEBUG_PAUSE_BREAKPOINT: return "breakpoint";
			case LS_DEBUG_PAUSE_STEP: return "step";
			case LS_DEBUG_PAUSE_ERROR: return "error";
		}
		return "unknown";
	}

	static const char* typeName(ls_type_kind kind) {
		switch (kind) {
			case LS_TYPE_BOOL: return "bool";
			case LS_TYPE_I8: return "i8";
			case LS_TYPE_U8: return "u8";
			case LS_TYPE_I16: return "i16";
			case LS_TYPE_U16: return "u16";
			case LS_TYPE_I32: return "i32";
			case LS_TYPE_U32: return "u32";
			case LS_TYPE_I64: return "i64";
			case LS_TYPE_U64: return "u64";
			case LS_TYPE_F32: return "f32";
			case LS_TYPE_F64: return "f64";
			case LS_TYPE_STRING: return "string";
			case LS_TYPE_STRUCT: return "struct";
			case LS_TYPE_ENUM: return "enum";
			case LS_TYPE_ARRAY: return "array";
			case LS_TYPE_SLICE: return "slice";
			case LS_TYPE_CPTR: return "cptr";
			case LS_TYPE_FUNCTION: return "function";
			case LS_TYPE_NULL_VALUE: return "null";
			default: return "unknown";
		}
	}

	static void drawVariable(ls_string_view name, ls_type_kind kind, void* value, u32 size) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("%.*s", int(name.end - name.begin), name.begin);
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(typeName(kind));
		ImGui::TableNextColumn();
		if (!value || size == 0) {
			ImGui::TextDisabled("<unavailable>");
			return;
		}
		switch (kind) {
			case LS_TYPE_BOOL: ImGui::TextUnformatted(*(const bool*)value ? "true" : "false"); break;
			case LS_TYPE_I8: ImGui::Text("%d", *(const i8*)value); break;
			case LS_TYPE_U8: ImGui::Text("%u", *(const u8*)value); break;
			case LS_TYPE_I16: ImGui::Text("%d", *(const i16*)value); break;
			case LS_TYPE_U16: ImGui::Text("%u", *(const u16*)value); break;
			case LS_TYPE_I32: ImGui::Text("%d", *(const i32*)value); break;
			case LS_TYPE_U32: ImGui::Text("%u", *(const u32*)value); break;
			case LS_TYPE_I64: ImGui::Text("%lld", *(const i64*)value); break;
			case LS_TYPE_U64: ImGui::Text("%llu", *(const u64*)value); break;
			case LS_TYPE_F32: ImGui::Text("%g", *(const f32*)value); break;
			case LS_TYPE_F64: ImGui::Text("%g", *(const f64*)value); break;
			default: ImGui::TextDisabled("<complex value>"); break;
		}
	}

	bool hasBreakpoint(const Path& source, u32 line) const {
		for (const Breakpoint& breakpoint : m_breakpoints) {
			if (breakpoint.line == line && breakpoint.source == source) return true;
		}
		return false;
	}

	StudioApp& m_app;
	Array<Breakpoint> m_breakpoints;
	char m_source[MAX_PATH] = {};
	int m_line = 1;
	bool m_is_open = true;
	bool m_debug_enabled = false;
};

static bool toggleLumScriptBreakpoint(StudioApp& app, const Path& source, u32 line) {
	if (!g_lumscript_debugger) return false;
	return g_lumscript_debugger->toggleBreakpoint(source, line);
}

struct LumScriptPlugin : StudioApp::IPlugin {
	explicit LumScriptPlugin(StudioApp& app)
		: m_app(app)
		, m_asset_plugin(app)
		, m_debugger(app)
	{
	}

	const char* getName() const override { return "lumscript"; }

	void init() override {
		const char* lum_exts[] = {"lum"};
		g_toggle_lumscript_breakpoint.shortcut = os::Keycode::F9;
		m_app.getAssetBrowser().addPlugin(m_asset_plugin, Span(lum_exts));
		m_app.getAssetCompiler().addPlugin(m_asset_plugin, Span(lum_exts));
		m_app.addPlugin(m_debugger);
	}

	void update(float) override {
		if (m_debugger_action.request) {
			m_debugger.setOpen(!m_debugger.isOpen());
			m_debugger_action.request = false;
		}
	}

	bool showGizmo(struct WorldView& view, struct ComponentUID cmp) override {
		return false;
	}

	~LumScriptPlugin() {
		m_app.removePlugin(m_debugger);
		m_app.getAssetBrowser().removePlugin(m_asset_plugin);
		m_app.getAssetCompiler().removePlugin(m_asset_plugin);
	}

private:
	StudioApp& m_app;
	LumScriptAssetPlugin m_asset_plugin;
	LumScriptDebuggerWindow m_debugger;
	Action m_debugger_action{"LumScript", "Debugger", "LumScript Debugger", "lumscript_debugger", ICON_FA_BUG, Action::Type::TOOL};
};

LUMIX_STUDIO_ENTRY(lumscript) {
	auto& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, LumScriptPlugin)(app);
}

} // namespace Lumix
