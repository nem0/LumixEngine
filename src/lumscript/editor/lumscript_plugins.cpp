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
#include "editor/property_grid.h"
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
	IM_COL32(0xFF, 0x00, 0xFF, 0xff), // EMPTY (not rendered)
	IM_COL32(0xD6, 0xDE, 0xEB, 0xff), // IDENTIFIER
	IM_COL32(0xF6, 0xC1, 0x77, 0xff), // NUMBER
	IM_COL32(0xA7, 0xD8, 0xA0, 0xff), // STRING
	IM_COL32(0xC7, 0x92, 0xEA, 0xff), // KEYWORD
	IM_COL32(0x89, 0xDD, 0xFF, 0xff), // OPERATOR
	IM_COL32(0x6A, 0x99, 0x55, 0xff), // COMMENT
};

enum class TokenType : u8 {
	EMPTY,
	IDENTIFIER,
	NUMBER,
	STRING,
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

	if (*c == '"') {
		token_type = (u8)TokenType::STRING;
		++c;
		while (*c && *c != '"') {
			if (*c == '\\' && c[1]) ++c;
			++c;
		}
		if (*c == '"') ++c;
		token_len = u32(c - str);
		return *c;
	}

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
static Action g_debugger_continue{"LumScript", "Continue", "Continue execution", "lumscript_continue", ICON_FA_PLAY, Action::Type::NORMAL};
static Action g_debugger_step_over{"LumScript", "Step over", "Step over next statement", "lumscript_step_over", ICON_FA_ARROW_RIGHT, Action::Type::NORMAL};
static Action g_debugger_step_into{"LumScript", "Step into", "Step into function call", "lumscript_step_into", ICON_FA_ARROW_DOWN, Action::Type::NORMAL};
static Action g_debugger_step_out{"LumScript", "Step out", "Step out of function", "lumscript_step_out", ICON_FA_ARROW_UP, Action::Type::NORMAL};

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
		case LS_TYPE_STRUCT: return "struct";
		case LS_TYPE_ENUM: return "enum";
		case LS_TYPE_ARRAY: return "array";
		case LS_TYPE_SLICE: return "slice";
		case LS_TYPE_CPTR: return "cptr";
		case LS_TYPE_FUNCTION: return "function";
		case LS_TYPE_TAGGED_UNION: return "union";
		case LS_TYPE_NULLABLE: return "?";
		case LS_TYPE_NULL_VALUE: return "null";
		default: return "unknown";
	}
}

static void drawTypeColumn(ls_type_kind kind, const ls_type* type) {
	if (type) {
		const ls_string_view name = ls_type_get_name(type);
		if (name.begin && name.length != 0) {
			ImGui::Text("%.*s", int(name.length), name.begin);
			return;
		}
	}
	if (kind == LS_TYPE_INVALID) {
		ImGui::TextUnformatted("invalid");
		return;
	}
	ImGui::TextUnformatted(typeName(kind));
}

static bool isStringSlice(const ls_type* type) {
	if (!type || ls_type_get_kind(type) != LS_TYPE_SLICE || !ls_type_is_const(type)) return false;
	const ls_type* elem = ls_type_array_element_type(type);
	return elem && ls_type_get_kind(elem) == LS_TYPE_U8;
}

static void drawPrimitiveValue(ls_type_kind kind, const void* value, const ls_type* type) {
	switch (kind) {
		case LS_TYPE_BOOL: ImGui::TextUnformatted(*(const bool*)value ? "true" : "false"); break;
		case LS_TYPE_I8:   ImGui::Text("%d", *(const i8*)value); break;
		case LS_TYPE_U8:   ImGui::Text("%u", *(const u8*)value); break;
		case LS_TYPE_I16:  ImGui::Text("%d", *(const i16*)value); break;
		case LS_TYPE_U16:  ImGui::Text("%u", *(const u16*)value); break;
		case LS_TYPE_I32:  ImGui::Text("%d", *(const i32*)value); break;
		case LS_TYPE_U32:  ImGui::Text("%u", *(const u32*)value); break;
		case LS_TYPE_I64:  ImGui::Text("%lld", *(const i64*)value); break;
		case LS_TYPE_U64:  ImGui::Text("%llu", *(const u64*)value); break;
		case LS_TYPE_F32:  ImGui::Text("%g", *(const f32*)value); break;
		case LS_TYPE_F64:  ImGui::Text("%g", *(const f64*)value); break;
		case LS_TYPE_CPTR: ImGui::Text("0x%p", *(const void* const*)value); break;
		case LS_TYPE_ENUM: {
			const i32 v = *(const i32*)value;
			if (type) {
				const u32 count = ls_type_enum_value_count(type);
				for (u32 i = 0; i < count; ++i) {
					if (ls_type_enum_value_value(type, i) == v) {
						const ls_string_view ev_name = ls_type_enum_value_name(type, i);
						ImGui::Text("%.*s (%d)", int(ev_name.length), ev_name.begin, v);
						break;
					}
				}
			} else {
				ImGui::Text("%d", v);
			}
			break;
		}
		case LS_TYPE_FUNCTION: ImGui::TextUnformatted("<function>"); break;
		case LS_TYPE_NULL_VALUE: ImGui::TextUnformatted("null"); break;
		default:               ImGui::TextUnformatted("invalid"); break;
	}
}

static void drawVariable(ls_string_view name, ls_type_kind kind, const ls_type* type, void* value, u32 size, bool editable = false) {
	if (type && ls_type_get_kind(type) != LS_TYPE_INVALID) {
		kind = ls_type_get_kind(type);
		size = ls_type_get_size(type);
	}

	if (!value || size == 0) {ImGuiEx::Label(StaticString<128>(StringView(name.begin, name.length))); ImGui::TextDisabled("<unavailable>"); return;
	}

	if (kind == LS_TYPE_STRUCT && type) {const bool open = ImGui::TreeNodeEx(name.begin
			, ImGuiTreeNodeFlags_SpanFullWidth
			, "%.*s"
			, int(name.length)
			, name.begin);ImGui::Text("(%u B)", ls_type_get_size(type));if (open) {
			for (u32 i = 0, c = ls_type_struct_field_count(type); i < c; ++i) {
				const ls_string_view fname = ls_type_struct_field_name(type, i);
				const u32 offset = ls_type_struct_field_offset(type, i);
				const ls_type* ftype = ls_type_struct_field_type(type, i);
				void* fv = (u8*)value + offset;
				const u32 fsize = ftype ? ls_type_get_size(ftype) : 0u;
				drawVariable(fname, ftype ? ls_type_get_kind(ftype) : LS_TYPE_INVALID, ftype, fv, fsize, editable);
			}
			ImGui::TreePop();
		}
		return;
	}

	if (kind == LS_TYPE_TAGGED_UNION && type) {const i32 tag = ls_type_union_tag(type, value);
		const bool open = ImGui::TreeNodeEx(name.begin
			, ImGuiTreeNodeFlags_SpanFullWidth
			, "%.*s"
			, int(name.length)
			, name.begin);{
			const u32 member_count = ls_type_union_member_count(type);
			if (tag >= 0 && (u32)tag < member_count) {
				const ls_type* member_type = ls_type_union_member_type(type, tag);
				ImGui::Text("tag=%d: ", tag);
				ImGui::SameLine();
				if (member_type) ImGui::TextUnformatted("<value>");
				else ImGui::TextUnformatted("?");
				ImGui::SameLine();
				ImGui::Text("(%u members)", member_count);
			} else {
				ImGui::Text("tag=%d (invalid) (%u members)", tag, member_count);
			}
		}if (open) {
			const u32 member_count = ls_type_union_member_count(type);
			if (tag >= 0 && (u32)tag < member_count) {
				const ls_type* member_type = ls_type_union_member_type(type, tag);
				void* payload = (u8*)value + 4;
				if (member_type && ls_type_get_kind(member_type) == LS_TYPE_STRUCT) {
					for (u32 i = 0, c = ls_type_struct_field_count(member_type); i < c; ++i) {
						const ls_string_view fname = ls_type_struct_field_name(member_type, i);
						const u32 offset = ls_type_struct_field_offset(member_type, i);
						const ls_type* ftype = ls_type_struct_field_type(member_type, i);
						void* fv = (u8*)payload + offset;
						const u32 fsize = ftype ? ls_type_get_size(ftype) : 0u;
						drawVariable(fname, ftype ? ls_type_get_kind(ftype) : LS_TYPE_INVALID, ftype, fv, fsize, editable);
					}
				} else {
					const ls_type_kind member_kind = member_type ? ls_type_get_kind(member_type) : LS_TYPE_INVALID;
					const u32 member_size = member_type ? ls_type_get_size(member_type) : 0u;
					const char* value_str = "value";
					drawVariable(ls_string_view{value_str, 5}, member_kind, member_type, payload, member_size, editable);
				}
			} else {ImGui::TextDisabled("<invalid tag>");
			}
			ImGui::TreePop();
		}
		return;
	}

	if (kind == LS_TYPE_ARRAY && type) {const bool open = ImGui::TreeNodeEx(name.begin
			, ImGuiTreeNodeFlags_SpanFullWidth
			, "%.*s"
			, int(name.length)
			, name.begin);ImGui::TextUnformatted("");{
			const u32 len = ls_type_array_length(type);
			const ls_type* elem = ls_type_array_element_type(type);
			ImGui::Text("[%u]", len);
		}
		if (open) {
			const u32 len = ls_type_array_length(type);
			const ls_type* elem = ls_type_array_element_type(type);
			const u32 elem_size = elem ? ls_type_get_size(elem) : 1u;
			char idx_buf[32];
			for (u32 i = 0; i < len; ++i) {
				idx_buf[0] = '[';
				char* end = toCString(i, Span<char>(idx_buf + 1, sizeof(idx_buf) - 2));
				if (!end) end = idx_buf + 1;
				*end = ']';
				*(end + 1) = '\0';
				const ls_string_view idx_name = { idx_buf, end - idx_buf + 1 };
				void* ev = (u8*)value + i * elem_size;
				drawVariable(idx_name, elem ? ls_type_get_kind(elem) : LS_TYPE_INVALID, elem, ev, elem_size, editable);
			}
			ImGui::TreePop();
		}
		return;
	}

	if (kind == LS_TYPE_SLICE && type) {
		const void* ptr = *(const void* const*)value;
		const u64 len = *(const u64*)((const u8*)value + 8);
		const ls_type* elem = ls_type_array_element_type(type);
		const u32 elem_size = elem ? ls_type_get_size(elem) : 1u;
		if (isStringSlice(type)) {
			ImGui::Indent();ImGui::Text("%.*s", int(name.length), name.begin);if (!ptr) {
				ImGui::TextUnformatted("null");
			} else {
				const u64 display_len = len > 0x7fffffffu ? 0x7fffffffu : len;
				ImGui::Text("\"%.*s\"", (int)display_len, (const char*)ptr);
			}ImGui::TextUnformatted("string");
			ImGui::Unindent();
			return;
		}const bool open = ImGui::TreeNodeEx(name.begin
			, ImGuiTreeNodeFlags_SpanFullWidth
			, "%.*s"
			, int(name.length)
			, name.begin);ImGui::TextUnformatted("");ImGui::Text("(%llu)", len);
		if (elem) {
			ImGui::SameLine();
			ImGui::TextUnformatted("<element>");
		}
		if (open) {
			if (ptr && len > 0) {
				char idx_buf[32];
				for (u64 i = 0; i < len; ++i) {
					idx_buf[0] = '[';
					char* end = toCString(i, Span<char>(idx_buf + 1, sizeof(idx_buf) - 2));
					if (!end) end = idx_buf + 1;
					*end = ']';
					*(end + 1) = '\0';
					const ls_string_view idx_name = { idx_buf, end - idx_buf + 1 };
					void* ev = (u8*)ptr + i * elem_size;
					drawVariable(idx_name, elem ? ls_type_get_kind(elem) : LS_TYPE_INVALID, elem, ev, elem_size, editable);
				}
			}
			ImGui::TreePop();
		}
		return;
	}

	if (kind == LS_TYPE_NULLABLE && type) {const bool open = ImGui::TreeNodeEx(name.begin
			, ImGuiTreeNodeFlags_SpanFullWidth
			, "%.*s"
			, int(name.length)
			, name.begin);if (ls_type_nullable_is_null(type, value)) {
			ImGui::TextDisabled("null");
		} else {
			const ls_type* inner = ls_type_nullable_inner_type(type);
			if (inner) ImGui::TextUnformatted("<value>");
			else ImGui::TextUnformatted("?");
		}ImGui::TextUnformatted("?");
		if (open) {
			if (!ls_type_nullable_is_null(type, value)) {
				const ls_type* inner = ls_type_nullable_inner_type(type);
				const void* inner_value = ls_type_nullable_value_ptr(type, value);
				const u32 inner_size = inner ? ls_type_get_size(inner) : 0u;
				const ls_type_kind inner_kind = inner ? ls_type_get_kind(inner) : LS_TYPE_INVALID;
				if (inner_kind == LS_TYPE_STRUCT) {
					for (u32 i = 0, c = ls_type_struct_field_count(inner); i < c; ++i) {
						const ls_string_view fname = ls_type_struct_field_name(inner, i);
						const u32 offset = ls_type_struct_field_offset(inner, i);
						const ls_type* ftype = ls_type_struct_field_type(inner, i);
						void* fv = (u8*)inner_value + offset;
						const u32 fsize = ftype ? ls_type_get_size(ftype) : 0u;
						drawVariable(fname, ftype ? ls_type_get_kind(ftype) : LS_TYPE_INVALID, ftype, fv, fsize, editable);
					}
				} else {
					const char* value_str = "value";
					const ls_string_view value_name = { value_str, 5 };
					drawVariable(value_name, inner_kind, inner, (void*)inner_value, inner_size, editable);
				}
			}
			ImGui::TreePop();
		}
		return;
	}

	// Primitive
	ImGui::Indent();
	ImGuiEx::Label(StaticString<128>(StringView(name.begin, name.length)));
	ImGui::PushID(value);
	if (editable) {
		switch (kind) {
			case LS_TYPE_BOOL: ImGui::Checkbox("##value", (bool*)value); break;
			case LS_TYPE_I8: ImGui::InputScalar("##value", ImGuiDataType_S8, value); break;
			case LS_TYPE_U8: ImGui::InputScalar("##value", ImGuiDataType_U8, value); break;
			case LS_TYPE_I16: ImGui::InputScalar("##value", ImGuiDataType_S16, value); break;
			case LS_TYPE_U16: ImGui::InputScalar("##value", ImGuiDataType_U16, value); break;
			case LS_TYPE_I32: ImGui::InputInt("##value", (i32*)value); break;
			case LS_TYPE_U32: ImGui::InputScalar("##value", ImGuiDataType_U32, value); break;
			case LS_TYPE_I64: ImGui::InputScalar("##value", ImGuiDataType_S64, value); break;
			case LS_TYPE_U64: ImGui::InputScalar("##value", ImGuiDataType_U64, value); break;
			case LS_TYPE_F32: ImGui::InputFloat("##value", (f32*)value); break;
			case LS_TYPE_F64: ImGui::InputDouble("##value", (f64*)value); break;
			case LS_TYPE_ENUM: ImGui::InputInt("##value", (i32*)value); break;
			default: drawPrimitiveValue(kind, value, type); break;
		}
	}
	else {
		drawPrimitiveValue(kind, value, type);
	}
	ImGui::PopID();ImGui::Unindent();
}

struct LumScriptDebuggerWindow;
static LumScriptDebuggerWindow* g_lumscript_debugger = nullptr;
static bool toggleLumScriptBreakpoint(StudioApp& app, const Path& source, u32 line);
static void applyLumScriptBreakpointMarkers(CodeEditor& editor, const Path& path);

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
			StringView path_view(path.begin, path.length);
			Path file_path;
			if (startsWith(path_view, "core:")) {
				StringView name = path_view.withoutLeft(5);
				const bool has_lum_extension = endsWith(name, ".lum");
				file_path = has_lum_extension ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".lum");
			}
			else {
				file_path = endsWith(path_view, ".lum") ? Path(path_view) : Path(path_view, ".lum");
			}
			OutputMemoryStream& import_blob = ctx->import_ctx->sources.emplace(*ctx->import_ctx->allocator);
			if (!ctx->import_ctx->filesystem->getContentSync(file_path, import_blob)) {
				ctx->import_ctx->sources.pop();
				return 0;
			}
			*source = ls_string_view{(const char*)import_blob.data(), (i64)import_blob.size()};
			return 1;
		};
		ImportContext import_ctx(m_app.getEngine().getFileSystem(), m_app.getAllocator());
		ImportResolverContext resolver_ctx = {};
		ls_host host = {};
		ls_default_arena_create(&host.arena);
		host.diagnostics_userdata = &diagnostics_message;
		host.print = [](void* userdata, ls_string_view msg) {
			((String*)userdata)->append(StringView(msg.begin, msg.length));
		};
		ls_module* module = ls_module_create(&host);
		if (module) {
			resolver_ctx.module = module;
			resolver_ctx.import_ctx = &import_ctx;
			if (ls_module_compile(module,
				ls_string_view{(const char*)blob.data(), (i64)blob.size()},
				ls_string_view{m_path.c_str(), stringLength(m_path.c_str())},
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
					const StringView error_path(diagnostics.data, line_marker);
					line_marker += stringLength(": line ");
					i32 line;
					if (equalStrings(error_path, m_path.c_str())
						&& fromCString(StringView(line_marker, diagnostics.end()), line)
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
		World* world = m_app.getWorldEditor().getWorld();
		LumScriptModule* module = world ? static_cast<LumScriptModule*>(world->getModule("lumscript")) : nullptr;
		ls_runtime* runtime = module ? module->getDebugRuntime() : nullptr;

		u32 current_line = 0;
		if (runtime && ls_debug_is_suspended(runtime)) {
			ls_debug_event event = {};
			if (ls_debug_pause_event(runtime, &event) == LS_RESULT_OK) {
				StringView event_source(event.location.source_name.begin, event.location.source_name.length);
				StringView editor_path(m_path.c_str(), m_path.c_str() + stringLength(m_path.c_str()));
				if (!event_source.empty()) {
					const char* event_filename = reverseFind(event_source, '/');
					if (!event_filename) event_filename = reverseFind(event_source, '\\');
					if (!event_filename) event_filename = event_source.data;
					else ++event_filename;

					const char* editor_filename = reverseFind(editor_path, '/');
					if (!editor_filename) editor_filename = reverseFind(editor_path, '\\');
					if (!editor_filename) editor_filename = editor_path.data;
					else ++editor_filename;

					const StringView ed_name(editor_filename, editor_path.end());
					if (startsWith(event_source, "core:")) {
						const StringView core_name = event_source.withoutLeft(5);
						if (startsWith(ed_name, core_name)) {
							current_line = event.location.line > 0 ? event.location.line - 1 : 0;
						}
					} else if (equalStrings(StringView(event_filename, event_source.end()), ed_name)
						|| (endsWith(ed_name, ".lum")
							&& equalStrings(StringView(event_filename, event_source.end()), StringView(ed_name.data, ed_name.end() - 4)))) {
						current_line = event.location.line > 0 ? event.location.line - 1 : 0;
					}
				}
			}
		}

		if (runtime && ls_debug_is_suspended(runtime)) {
			m_editor->setCurrentDebugLine(current_line);
			if (m_focus_request && current_line > 0) {
				m_focus_request = false;
				m_editor->setSelection(current_line, 0, current_line, 0, true);
			}
		} else {
			m_editor->clearCurrentDebugLine();
		}

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
		applyLumScriptBreakpointMarkers(*m_editor, m_path);
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

	const char* getName() const override { return "lumscript_debugger_control"; }
	bool isOpen() const { return m_is_open; }
	void setOpen(bool open) { m_is_open = open; }

	Array<Breakpoint>& getBreakpoints() { return m_breakpoints; }
	const Array<Breakpoint>& getBreakpoints() const { return m_breakpoints; }

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

		World* world = m_app.getWorldEditor().getWorld();
		LumScriptModule* module = world ? static_cast<LumScriptModule*>(world->getModule("lumscript")) : nullptr;
		ls_runtime* runtime = module ? module->getDebugRuntime() : nullptr;
		const bool suspended = runtime && ls_debug_is_suspended(runtime);
		const bool just_suspended = suspended && !m_was_suspended;
		m_was_suspended = suspended;

		if (just_suspended) {
			ImGui::SetNextWindowFocus();
		}

		ImGui::SetNextWindowDockID(m_app.getDockspaceID(), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(ICON_FA_BUG " LumScript Debugger", &m_is_open)) {
			ImGui::End();
			return;
		}

		if (module) applyBreakpoints(module);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

		// Execution controls
		if (runtime && ls_debug_is_suspended(runtime)) {
			if (g_debugger_continue.iconButton(true, &m_app)) { ls_debug_resume(runtime, LS_DEBUG_CONTINUE); m_step_requested = true; }
			ImGui::SameLine();
			if (g_debugger_step_over.iconButton(true, &m_app)) { ls_debug_resume(runtime, LS_DEBUG_STEP_OVER); m_step_requested = true; }
			ImGui::SameLine();
			if (g_debugger_step_into.iconButton(true, &m_app)) { ls_debug_resume(runtime, LS_DEBUG_STEP_INTO); m_step_requested = true; }
			ImGui::SameLine();
			if (g_debugger_step_out.iconButton(true, &m_app)) { ls_debug_resume(runtime, LS_DEBUG_STEP_OUT); m_step_requested = true; }
		} else {
			ImGui::BeginDisabled();
			g_debugger_continue.iconButton(false, &m_app);
			ImGui::SameLine();
			g_debugger_step_over.iconButton(false, &m_app);
			ImGui::SameLine();
			g_debugger_step_into.iconButton(false, &m_app);
			ImGui::SameLine();
			g_debugger_step_out.iconButton(false, &m_app);
			ImGui::EndDisabled();
		}

		ImGui::PopStyleVar();

		if (suspended) {
			ls_debug_event event = {};
			if (ls_debug_pause_event(runtime, &event) == LS_RESULT_OK) {
				ImGui::Text("Suspended: %s", pauseReason(event.reason));
				if (event.reason == LS_DEBUG_PAUSE_ERROR && event.message.length > 0) {
					ImGui::TextWrapped("%.*s", int(event.message.length), event.message.begin);
				}
			}
		}

		const bool should_focus = just_suspended || m_step_requested;
		m_step_requested = false;
		if (should_focus && runtime && ls_debug_is_suspended(runtime)) {
			ls_debug_event event = {};
			if (ls_debug_pause_event(runtime, &event) == LS_RESULT_OK) {
				const StringView src_name(event.location.source_name.begin, event.location.source_name.length);
				Path path;
				if (startsWith(src_name, "core:")) {
					const StringView file_name = src_name.withoutLeft(5);
					const bool has_lum = endsWith(file_name, ".lum");
					path = has_lum ? Path("engine/scripts/core/", file_name) : Path("engine/scripts/core/", file_name, ".lum");
				} else {
					// Imported units use the import spelling as their source name
					// (e.g. `import demo` -> `demo`), while the asset is demo.lum.
					path = endsWith(src_name, ".lum") ? Path(src_name) : Path(src_name, ".lum");
				}
				m_app.getAssetBrowser().openEditor(path);
				AssetEditorWindow* win = m_app.getAssetBrowser().getWindow(path);
				if (win) {
					win->m_focus_request = true;
					auto* lswin = (LumScriptEditorWindow*)win;
					const u32 line_zero = event.location.line > 0 ? event.location.line - 1 : 0;
					lswin->m_editor->setSelection(line_zero, 0, line_zero, 0, true);
					lswin->m_editor->focus();
				}
			}
		}

		ImGui::Separator();

		// Breakpoints list
		if (ImGui::CollapsingHeader(ICON_FA_CIRCLE " Breakpoints", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (m_breakpoints.empty()) {
				ImGui::TextDisabled("No breakpoints set");
			} else {
				for (i32 i = 0; i < m_breakpoints.size(); ++i) {
					auto& bp = m_breakpoints[i];
					ImGui::PushID((int)i);
					ImGui::BulletText("%s", bp.source.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled(":%u", bp.line);
					ImGui::SameLine(ImGui::GetWindowWidth() - 70);
					if (ImGuiEx::IconButton(ICON_FA_TRASH, "Remove")) {
						removeBreakpoint(module, bp.source, bp.line);
						m_breakpoints.erase(i);
						ImGui::PopID();
						break;
					}
					ImGui::PopID();
				}
			}
		}

		// Call stack
		ImGui::Separator();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(ICON_FA_LIST " Call Stack");

		if (!suspended) {
			ImGui::TextDisabled(runtime ? "Running" : "Waiting for runtime...");
		} else {
			ls_debug_event event = {};
			ls_debug_pause_event(runtime, &event);
			ImGui::Separator();

			if (ImGui::BeginTable("debugger_callstack", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
				ImGui::TableSetupColumn("#");
				ImGui::TableSetupColumn("Function");
				ImGui::TableSetupColumn("Source");
				ImGui::TableHeadersRow();
				for (u32 i = 0, n = ls_debug_stack_depth(runtime); i < n; ++i) {
					ls_debug_location location;
					ls_debug_frame_location(runtime, i, &location);
					const ls_string_view name = ls_debug_frame_function_name(runtime, i);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%u", i);
					ImGui::TableNextColumn();
					ImGui::TextWrapped("%.*s", int(name.length), name.begin);
					ImGui::TableNextColumn();
					ImGui::TextWrapped("%.*s:%u",
						int(location.source_name.length), location.source_name.begin, location.line);
					if (ImGui::IsItemHovered()) {
						ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
						if (ImGui::IsMouseDoubleClicked(0)) {
							StringView src_name(location.source_name.begin, location.source_name.length);
							Path path;
							if (startsWith(src_name, "core:")) {
								const StringView file_name = src_name.withoutLeft(5);
								const bool has_lum = endsWith(file_name, ".lum");
								path = has_lum ? Path("engine/scripts/core/", file_name) : Path("engine/scripts/core/", file_name, ".lum");
							} else {
								path = Path(src_name);
							}
							m_app.getAssetBrowser().openEditor(path);
							AssetEditorWindow* win = m_app.getAssetBrowser().getWindow(path);
							if (win) {
								auto* lswin = (LumScriptEditorWindow*)win;
								const u32 line_zero = location.line > 0 ? location.line - 1 : 0;
								lswin->m_editor->setSelection(line_zero, 0, line_zero, 0, true);
							}
						}
					}
				}
				ImGui::EndTable();
			}
		}

		if (!module) {
			ImGui::Spacing();
			ImGui::TextDisabled(ICON_FA_EXCLAMATION " No active world module");
		}

		ImGui::End();
	}

	static const char* pauseReason(ls_debug_pause_reason reason) {
		switch (reason) {
			case LS_DEBUG_PAUSE_BREAKPOINT: return "breakpoint";
			case LS_DEBUG_PAUSE_STEP: return "step";
			case LS_DEBUG_PAUSE_ERROR: return "error";
		}
		return "unknown";
	}

	bool hasBreakpoint(const Path& source, u32 line) const {
		for (const Breakpoint& breakpoint : m_breakpoints) {
			if (breakpoint.line == line && breakpoint.source == source) return true;
		}
		return false;
	}

	StudioApp& m_app;
	Array<Breakpoint> m_breakpoints;
	bool m_is_open = true;
	bool m_was_suspended = false;
	bool m_step_requested = false;
};

static void applyLumScriptBreakpointMarkers(CodeEditor& editor, const Path& path) {
	if (!g_lumscript_debugger) return;
	for (const auto& bp : g_lumscript_debugger->getBreakpoints()) {
		if (bp.source == path) editor.setBreakpoint(bp.line - 1, true);
	}
}

struct LumScriptVariablesWindow final : StudioApp::GUIPlugin {
	explicit LumScriptVariablesWindow(StudioApp& app) : m_app(app), m_is_open(true) { m_filter[0] = '\0'; }
	const char* getName() const override { return "lumscript_variables"; }
	bool isOpen() const { return m_is_open; }
	void setOpen(bool open) { m_is_open = open; }

	static bool nameMatchesFilter(ls_string_view name, const char* filter) {
		if (!filter || !filter[0]) return true;
		for (const char* c = name.begin; c < name.begin + name.length; ++c) {
			const char* f = filter;
			const char* n = c;
			while (*f && n < name.begin + name.length && toLower(*n) == toLower(*f)) { ++n; ++f; }
			if (!*f) return true;
		}
		return false;
	}

	void onGUI() override {
		if (!m_is_open) return;
		ImGui::SetNextWindowDockID(m_app.getDockspaceID(), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(ICON_FA_CUBE " Variables", &m_is_open)) {
			ImGui::End();
			return;
		}

		World* world = m_app.getWorldEditor().getWorld();
		LumScriptModule* module = world ? static_cast<LumScriptModule*>(world->getModule("lumscript")) : nullptr;
		ls_runtime* runtime = module ? module->getDebugRuntime() : nullptr;

		const bool suspended = runtime && ls_debug_is_suspended(runtime);
		if (!suspended) {
			ImGui::TextDisabled(runtime ? "Running" : "Waiting for runtime...");
		} else {
			ImGui::InputTextWithHint("##filter", "Filter variables...", m_filter, sizeof(m_filter), ImGuiInputTextFlags_AutoSelectAll);
			const bool has_filter = m_filter[0] != '\0';

			if (ImGui::BeginTable("lumscript_variables", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Value");
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableHeadersRow();

				// Locals
				for (u32 i = 0, n = ls_debug_frame_local_count(runtime, 0); i < n; ++i) {
					const ls_string_view name = ls_debug_local_name(runtime, 0, i);
					if (has_filter && !nameMatchesFilter(name, m_filter)) continue;
					const ls_type_kind kind = ls_debug_local_kind(runtime, 0, i);
					const ls_type* type = ls_debug_local_type(runtime, 0, i);
					u32 size = 0;
					void* value = ls_debug_local_value(runtime, 0, i, &size);
					drawVariable(name, kind, type, value, size);
				}

				// Globals
				for (u32 i = 0, n = ls_debug_global_count(runtime); i < n; ++i) {
					const ls_string_view name = ls_debug_global_name(runtime, i);
					if (has_filter && !nameMatchesFilter(name, m_filter)) continue;
					const ls_type_kind kind = ls_debug_global_kind(runtime, i);
					const ls_type* type = ls_debug_global_type(runtime, i);
					u32 size = 0;
					void* value = ls_debug_global_value(runtime, i, &size);
					drawVariable(name, kind, type, value, size);
				}
				ImGui::EndTable();
			}
		}

		ImGui::End();
	}

	StudioApp& m_app;
	bool m_is_open;
	char m_filter[64];
};

static bool toggleLumScriptBreakpoint(StudioApp& app, const Path& source, u32 line) {
	if (!g_lumscript_debugger) return false;
	return g_lumscript_debugger->toggleBreakpoint(source, line);
}

static Action g_toggle_variables_window{"LumScript", "Variables window", "Show/hide variables window", "lumscript_toggle_variables", ICON_FA_CUBE, Action::Type::TOOL};

struct LumScriptDataCommand final : IEditorCommand {
	LumScriptDataCommand(WorldEditor& editor, EntityRef entity, const ls_type* type, bool add)
		: m_editor(editor)
		, m_entity(entity)
		, m_type(type)
		, m_add(add)
	{}

	bool execute() override { return apply(m_add); }
	void undo() override { apply(!m_add); }
	const char* getType() override { return m_add ? "add_lumscript_data" : "remove_lumscript_data"; }
	bool merge(IEditorCommand&) override { return false; }

	bool apply(bool add) {
		World* world = m_editor.getWorld();
		if (!world || !world->hasEntity(m_entity)) return false;
		LumScriptModule* module = static_cast<LumScriptModule*>(world->getModule("lumscript"));
		if (!module) return false;
		return add ? module->addLumScriptData(m_entity, m_type) : module->removeLumScriptData(m_entity, m_type);
	}

	WorldEditor& m_editor;
	EntityRef m_entity;
	const ls_type* m_type;
	bool m_add;
};

struct LumScriptPropertyGridPlugin final : PropertyGrid::IPlugin {
	void onGUI(PropertyGrid&, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor) override {
		if (entities.length() != 1) return;
		World* world = editor.getWorld();
		IModule* base_module = world ? world->getModule(cmp_type) : nullptr;
		if (!base_module || !equalStrings(base_module->getName(), "lumscript")) return;

		LumScriptModule& module = static_cast<LumScriptModule&>(*base_module);
		const EntityRef entity = entities[0];
		for (u32 i = 0, count = module.getLumScriptDataCount(entity); i < count; ++i) {
			const ls_type* type = module.getLumScriptDataType(entity, i);
			if (!type) continue;
			const ls_string_view ls_name = ls_type_get_name(type);
			const StringView name(ls_name.begin, ls_name.length);
			if (filter.isActive() && !filter.pass(name)) continue;

			ImGui::PushID(type);
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Remove data")) {
				UniquePtr<IEditorCommand> command = UniquePtr<LumScriptDataCommand>::create(
					editor.getAllocator(), editor, entity, type, false);
				editor.executeCommand(command.move());
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			const bool open = ImGui::TreeNodeEx("##data", ImGuiTreeNodeFlags_SpanFullWidth, "%.*s", int(name.length), name.data);
			if (open) {
				{
					const void* value = module.getLumScriptData(entity, type);
					const ls_type_kind kind = ls_type_get_kind(type);
					if (kind == LS_TYPE_STRUCT) {
						for (u32 j = 0, n = ls_type_struct_field_count(type); j < n; ++j) {
							const ls_string_view field_name = ls_type_struct_field_name(type, j);
							const ls_type* field_type = ls_type_struct_field_type(type, j);
							const u32 offset = ls_type_struct_field_offset(type, j);
							drawVariable(field_name
								, field_type ? ls_type_get_kind(field_type) : LS_TYPE_INVALID
								, field_type
								, value ? (void*)((const u8*)value + offset) : nullptr
								, field_type ? ls_type_get_size(field_type) : 0
								, true);
						}
					}
					else {
						static const char value_name[] = "value";
						drawVariable({value_name, lengthOf(value_name) - 1 }, kind, type, (void*)value, ls_type_get_size(type), true);
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (ImGui::Button(ICON_FA_PLUS " Add data")) ImGui::OpenPopup("lumscript_add_data");
		if (ImGui::BeginPopup("lumscript_add_data")) {
			for (const ls_type* type : module.getLumScriptDataTypes()) {
				if (module.hasLumScriptData(entity, type)) continue;
				const ls_string_view ls_name = ls_type_get_name(type);
				const StringView name(ls_name.begin, ls_name.length);
				if (ImGui::MenuItem(StaticString<128>(name))) {
					UniquePtr<IEditorCommand> command = UniquePtr<LumScriptDataCommand>::create(
						editor.getAllocator(), editor, entity, type, true);
					editor.executeCommand(command.move());
				}
			}
			ImGui::EndPopup();
		}
	}

};

struct LumScriptPlugin : StudioApp::IPlugin {
	explicit LumScriptPlugin(StudioApp& app)
		: m_app(app)
		, m_asset_plugin(app)
		, m_debugger(app)
		, m_variables_window(app)
	{
	}

	const char* getName() const override { return "lumscript"; }

	void init() override {
		const char* lum_exts[] = {"lum"};
		g_toggle_lumscript_breakpoint.shortcut = os::Keycode::F8;
		g_debugger_continue.shortcut = os::Keycode::F1;
		g_debugger_step_over.shortcut = os::Keycode::F2;
		g_debugger_step_into.shortcut = os::Keycode::F3;
		g_debugger_step_out.shortcut = os::Keycode::SHIFT | os::Keycode::F11;
		m_app.getAssetBrowser().addPlugin(m_asset_plugin, Span(lum_exts));
		m_app.getAssetCompiler().addPlugin(m_asset_plugin, Span(lum_exts));
		m_app.getPropertyGrid().addPlugin(m_property_grid_plugin);
		m_app.addPlugin(m_debugger);
		m_app.addPlugin(m_variables_window);
	}

	void update(float) override {
		if (m_debugger_action.request) {
			m_debugger.setOpen(!m_debugger.isOpen());
			m_debugger_action.request = false;
		}
		if (g_toggle_variables_window.request) {
			m_variables_window.setOpen(!m_variables_window.isOpen());
			g_toggle_variables_window.request = false;
		}

		// Handle debugger shortcuts globally
		World* world = m_app.getWorldEditor().getWorld();
		LumScriptModule* module = world ? static_cast<LumScriptModule*>(world->getModule("lumscript")) : nullptr;
		ls_runtime* runtime = module ? module->getDebugRuntime() : nullptr;

		if (runtime && ls_debug_is_suspended(runtime)) {
			if (m_app.checkShortcut(g_debugger_continue, true)) { ls_debug_resume(runtime, LS_DEBUG_CONTINUE); m_debugger.m_step_requested = true; }
			if (m_app.checkShortcut(g_debugger_step_over, true)) { ls_debug_resume(runtime, LS_DEBUG_STEP_OVER); m_debugger.m_step_requested = true; }
			if (m_app.checkShortcut(g_debugger_step_into, true)) { ls_debug_resume(runtime, LS_DEBUG_STEP_INTO); m_debugger.m_step_requested = true; }
			if (m_app.checkShortcut(g_debugger_step_out, true)) { ls_debug_resume(runtime, LS_DEBUG_STEP_OUT); m_debugger.m_step_requested = true; }
		}
	}

	bool showGizmo(struct WorldView& view, struct ComponentUID cmp) override {
		return false;
	}

	~LumScriptPlugin() {
		m_app.getPropertyGrid().removePlugin(m_property_grid_plugin);
		m_app.removePlugin(m_variables_window);
		m_app.removePlugin(m_debugger);
		m_app.getAssetBrowser().removePlugin(m_asset_plugin);
		m_app.getAssetCompiler().removePlugin(m_asset_plugin);
	}

private:
	StudioApp& m_app;
	LumScriptAssetPlugin m_asset_plugin;
	LumScriptDebuggerWindow m_debugger;
	LumScriptVariablesWindow m_variables_window;
	LumScriptPropertyGridPlugin m_property_grid_plugin;
	Action m_debugger_action{"LumScript", "Debugger", "LumScript Debugger", "lumscript_debugger", ICON_FA_BUG, Action::Type::TOOL};
};

LUMIX_STUDIO_ENTRY(lumscript) {
	auto& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, LumScriptPlugin)(app);
}

} // namespace Lumix
