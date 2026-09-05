#include <imgui/imgui.h>
#include <imgui/imgui_user.h>
#include "evox/capi.h"
#include "evox/evox_resource.h"
#include "core/log.h"
#include "core/path.h"
#include "core/debug.h"
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
#include "evox/evox_module.h"
#include "evox/evox_capi.gen.h"
#include "core/array.h"
#include "core/stream.h"
#include "../../external/evox/capi.h"
#include "../../external/evox/arena.h"

namespace Lumix {

namespace EvoxTokens {

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
		"comptime",
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

} // namespace EvoxTokens

static Action g_toggle_evox_breakpoint{"Evox", "Toggle breakpoint", "Toggle breakpoint at cursor", "evox_toggle_breakpoint", ICON_FA_CIRCLE, Action::Type::NORMAL};
static Action g_evox_check{"Evox", "Check", "Check Evox source", "evox_check", ICON_FA_CHECK, Action::Type::NORMAL};
static Action g_evox_go_to_definition{"Evox", "Go to definition", "Go to definition", "evox_go_to_definition", ""};
static Action g_debugger_continue{"Evox", "Continue", "Continue execution", "evox_continue", ICON_FA_PLAY, Action::Type::NORMAL};
static Action g_debugger_step_over{"Evox", "Step over", "Step over next statement", "evox_step_over", ICON_FA_ARROW_RIGHT, Action::Type::NORMAL};
static Action g_debugger_step_into{"Evox", "Step into", "Step into function call", "evox_step_into", ICON_FA_ARROW_DOWN, Action::Type::NORMAL};
static Action g_debugger_step_out{"Evox", "Step out", "Step out of function", "evox_step_out", ICON_FA_ARROW_UP, Action::Type::NORMAL};
static u32 g_debug_frame_index = 0;

static bool isStringSlice(const ex_type* type) {
	if (!type || ex_type_get_kind(type) != EX_TYPE_SLICE || !ex_type_is_const(type)) return false;
	const ex_type* elem = ex_type_array_element_type(type);
	return elem && ex_type_get_kind(elem) == EX_TYPE_U8;
}

static const char* anyValueTypeName(ex_type_kind kind) {
	switch (kind) {
		case EX_TYPE_VOID: return "void";
		case EX_TYPE_BOOL: return "bool";
		case EX_TYPE_I8: return "i8";
		case EX_TYPE_U8: return "u8";
		case EX_TYPE_I16: return "i16";
		case EX_TYPE_U16: return "u16";
		case EX_TYPE_I32: return "i32";
		case EX_TYPE_U32: return "u32";
		case EX_TYPE_I64: return "i64";
		case EX_TYPE_U64: return "u64";
		case EX_TYPE_F32: return "f32";
		case EX_TYPE_F64: return "f64";
		case EX_TYPE_CPTR: return "pointer";
		case EX_TYPE_UNTYPED_INT: return "int";
		case EX_TYPE_UNTYPED_FLOAT: return "float";
		case EX_TYPE_STRUCT: return "struct";
		case EX_TYPE_TAGGED_UNION: return "union";
		case EX_TYPE_ENUM: return "enum";
		case EX_TYPE_FUNCTION: return "function";
		case EX_TYPE_ARRAY: return "array";
		case EX_TYPE_SLICE: return "slice";
		case EX_TYPE_NULL_VALUE: return "null";
		case EX_TYPE_NULLABLE: return "nullable";
		case EX_TYPE_ANY: return "any";
		default: return "invalid";
	}
}

static void drawPrimitiveValue(ex_type_kind kind, const void* value, const ex_type* type);

static const ex_runtime* g_debug_runtime = nullptr;

static void drawAnyValue(const void* value) {
	const void* payload = *(const void* const*)value;
	const ex_type* type = ex_type_from_any(g_debug_runtime, value);
	const ex_type_kind kind = ex_type_get_kind(type);
	ImGui::Text("%s: ", anyValueTypeName(kind));
	ImGui::SameLine(0, 0);
	if (!payload) {
		ImGui::TextDisabled("<unavailable>");
		return;
	}

	switch (kind) {
		case EX_TYPE_BOOL:
		case EX_TYPE_I8: case EX_TYPE_U8:
		case EX_TYPE_I16: case EX_TYPE_U16:
		case EX_TYPE_I32: case EX_TYPE_U32:
		case EX_TYPE_I64: case EX_TYPE_U64:
		case EX_TYPE_F32: case EX_TYPE_F64:
		case EX_TYPE_CPTR:
		case EX_TYPE_FUNCTION:
		case EX_TYPE_ENUM:
		case EX_TYPE_NULL_VALUE:
			drawPrimitiveValue(kind, payload, nullptr);
			break;
		case EX_TYPE_SLICE: {
			// `any` does not carry an ex_type descriptor, but interpolated
			// strings are represented as []const u8 slices.  Display the erased
			// slice directly instead of treating it as an unavailable value.
			const ex_slice& slice = *(const ex_slice*)payload;
			if (!slice.data) ImGui::TextUnformatted("null");
			else {
				const i64 length = slice.length < 0 ? 0 : slice.length;
				ImGui::Text("\"%.*s\"", int(length > 0x7fffffffu ? 0x7fffffffu : length), (const char*)slice.data);
			}
			break;
		}
		default:
			ImGui::TextUnformatted("<value unavailable>");
			break;
	}
}

static void drawPrimitiveValue(ex_type_kind kind, const void* value, const ex_type* type) {
	switch (kind) {
		case EX_TYPE_BOOL: ImGui::TextUnformatted(*(const bool*)value ? "true" : "false"); break;
		case EX_TYPE_I8:   ImGui::Text("%d", *(const i8*)value); break;
		case EX_TYPE_U8:   ImGui::Text("%u", *(const u8*)value); break;
		case EX_TYPE_I16:  ImGui::Text("%d", *(const i16*)value); break;
		case EX_TYPE_U16:  ImGui::Text("%u", *(const u16*)value); break;
		case EX_TYPE_I32:  ImGui::Text("%d", *(const i32*)value); break;
		case EX_TYPE_U32:  ImGui::Text("%u", *(const u32*)value); break;
		case EX_TYPE_I64:  ImGui::Text("%lld", *(const i64*)value); break;
		case EX_TYPE_U64:  ImGui::Text("%llu", *(const u64*)value); break;
		case EX_TYPE_F32:  ImGui::Text("%g", *(const f32*)value); break;
		case EX_TYPE_F64:  ImGui::Text("%g", *(const f64*)value); break;
		case EX_TYPE_CPTR: ImGui::Text("0x%p", *(const void* const*)value); break;
		case EX_TYPE_ENUM: {
			const i32 v = *(const i32*)value;
			if (type) {
				const u32 count = ex_type_enum_value_count(type);
				for (u32 i = 0; i < count; ++i) {
					if (ex_type_enum_value_value(type, i) == v) {
						const ex_string_view ev_name = ex_type_enum_value_name(type, i);
						ImGui::Text("%.*s (%d)", int(ev_name.length), ev_name.begin, v);
						break;
					}
				}
			} else {
				ImGui::Text("%d", v);
			}
			break;
		}
		case EX_TYPE_FUNCTION: ImGui::TextUnformatted("<function>"); break;
		case EX_TYPE_NULL_VALUE: ImGui::TextUnformatted("null"); break;
		default:               ImGui::TextUnformatted("invalid"); break;
	}
}

static void drawVariableName(ex_string_view name) {
	ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
	ImGui::TextUnformatted(name.begin, name.begin + name.length);
	ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
}

static bool isEntityType(const ex_type* type) {
	if (!type || ex_type_get_kind(type) != EX_TYPE_STRUCT) return false;
	const ex_string_view type_name = ex_type_get_name(type);
	return type_name.length == 6 && equalStrings(StringView(type_name.begin, type_name.length), "Entity");
}

static void drawEntityValue(const ExEntity& entity, WorldEditor* editor) {
	// Runtime values can retain pointers to the game world after game mode has
	// stopped and that world has been destroyed. Do not inspect those pointers
	// outside game mode; pointer equality alone cannot prove that a pointer is
	// still alive.
	World* current_world = editor && editor->isGameMode() ? editor->getWorld() : nullptr;
	const bool valid = current_world
		&& entity.world == current_world
		&& entity.index >= 0
		&& current_world->hasEntity(EntityRef(entity.index));
	StaticString<256> label;
	if (valid) {
		const char* entity_name = entity.world->getEntityName(EntityRef(entity.index));
		if (entity_name && entity_name[0]) label.append(entity_name, " (", entity.index, ")");
		else label.append("Entity (", entity.index, ")");
	}
	else {
		label.append("<invalid entity>");
	}

	if (editor && valid && entity.world == editor->getWorld()) {
		ImGui::Selectable(label, false, ImGuiSelectableFlags_DontClosePopups);
		if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		if (ImGui::IsItemClicked()) {
			const EntityRef ref(entity.index);
			editor->selectEntities(Span(&ref, 1), false);
		}
	}
	else {
		ImGui::TextUnformatted(label);
	}
}

static void drawVariable(ex_string_view name, ex_type_kind kind, const ex_type* type, void* value, u32 size, bool editable = false, WorldEditor* editor = nullptr) {
	if (type && ex_type_get_kind(type) != EX_TYPE_INVALID) {
		kind = ex_type_get_kind(type);
		size = ex_type_get_size(type);
	}

	if (!value || size == 0) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		drawVariableName(name);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("<unavailable>");
		return;
	}

	if (kind == EX_TYPE_ANY) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		drawVariableName(name);
		ImGui::TableNextColumn();
		drawAnyValue(value);
		return;
	}

	if (kind == EX_TYPE_NULLABLE && type && !ex_type_nullable_is_null(type, value)) {
		const ex_type* inner = ex_type_nullable_inner_type(type);
		if (inner) {
			drawVariable(name
				, ex_type_get_kind(inner)
				, inner
				, (void*)ex_type_nullable_value_ptr(type, value)
				, ex_type_get_size(inner)
				, editable
				, editor);
			return;
		}
	}

	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	if (isEntityType(type) && size >= sizeof(ExEntity)) {
		drawVariableName(name);
		ImGui::TableNextColumn();
		drawEntityValue(*(const ExEntity*)value, editor);
		return;
	}

	if (kind == EX_TYPE_STRUCT && type) {
		const bool open = ImGui::TreeNodeEx(value, ImGuiTreeNodeFlags_SpanAvailWidth, "%.*s", int(name.length), name.begin);
		ImGui::TableNextColumn();
		if (open) {
			for (u32 i = 0, c = ex_type_struct_field_count(type); i < c; ++i) {
				const ex_string_view field_name = ex_type_struct_field_name(type, i);
				const ex_type* field_type = ex_type_struct_field_type(type, i);
				drawVariable(field_name
					, field_type ? ex_type_get_kind(field_type) : EX_TYPE_INVALID
					, field_type
					, (u8*)value + ex_type_struct_field_offset(type, i)
					, field_type ? ex_type_get_size(field_type) : 0
					, editable
					, editor);
			}
			ImGui::TreePop();
		}
		return;
	}

	if (kind == EX_TYPE_TAGGED_UNION && type) {
		const i32 tag = ex_type_union_tag(type, value);
		const u32 member_count = ex_type_union_member_count(type);
		const bool valid_tag = tag >= 0 && (u32)tag < member_count;
		const bool open = ImGui::TreeNodeEx(value, ImGuiTreeNodeFlags_SpanAvailWidth, "%.*s", int(name.length), name.begin);
		ImGui::TableNextColumn();
		if (valid_tag) ImGui::Text("tag %d", tag);
		else ImGui::TextDisabled("invalid tag %d", tag);
		if (open) {
			if (valid_tag) {
				const ex_type* member_type = ex_type_union_member_type(type, tag);
				void* payload = (u8*)value + 4;
				if (member_type && ex_type_get_kind(member_type) == EX_TYPE_STRUCT) {
					for (u32 i = 0, c = ex_type_struct_field_count(member_type); i < c; ++i) {
						const ex_string_view field_name = ex_type_struct_field_name(member_type, i);
						const ex_type* field_type = ex_type_struct_field_type(member_type, i);
						drawVariable(field_name
							, field_type ? ex_type_get_kind(field_type) : EX_TYPE_INVALID
							, field_type
							, (u8*)payload + ex_type_struct_field_offset(member_type, i)
							, field_type ? ex_type_get_size(field_type) : 0
							, editable
							, editor);
					}
				} else {
					static const char value_name[] = "value";
					drawVariable({value_name, sizeof(value_name) - 1}
						, member_type ? ex_type_get_kind(member_type) : EX_TYPE_INVALID
						, member_type
						, payload
						, member_type ? ex_type_get_size(member_type) : 0
						, editable
						, editor);
				}
			}
			ImGui::TreePop();
		}
		return;
	}

	if ((kind == EX_TYPE_ARRAY || kind == EX_TYPE_SLICE) && type) {
		const ex_type* element_type = ex_type_array_element_type(type);
		const u32 element_size = element_type ? ex_type_get_size(element_type) : 1;
		const bool slice = kind == EX_TYPE_SLICE;
		const void* data = slice ? *(const void* const*)value : value;
		const u64 count = slice ? *(const u64*)((const u8*)value + 8) : ex_type_array_length(type);
		if (slice && isStringSlice(type)) {
			drawVariableName(name);
			ImGui::TableNextColumn();
			if (data) ImGui::Text("\"%.*s\"", int(count > 0x7fffffffu ? 0x7fffffffu : count), (const char*)data);
			else ImGui::TextUnformatted("null");
			return;
		}

		const bool open = ImGui::TreeNodeEx(value, ImGuiTreeNodeFlags_SpanAvailWidth, "%.*s", int(name.length), name.begin);
		ImGui::TableNextColumn();
		ImGui::Text("[%llu]", count);
		if (open) {
			if (data) {
				for (u64 i = 0; i < count; ++i) {
					char index[32];
					index[0] = '[';
					char* end = toCString(i, Span<char>(index + 1, sizeof(index) - 2));
					if (!end) end = index + 1;
					*end = ']';
					*(end + 1) = '\0';
					drawVariable({index, u32(end - index + 1)}
						, element_type ? ex_type_get_kind(element_type) : EX_TYPE_INVALID
						, element_type
						, (u8*)data + i * element_size
						, element_size
						, editable
						, editor);
				}
			}
			ImGui::TreePop();
		}
		return;
	}

	drawVariableName(name);
	ImGui::TableNextColumn();
	ImGui::PushID(value);
	if (kind == EX_TYPE_NULLABLE) {
		ImGui::TextDisabled("null");
	}
	else if (isEntityType(type) && size >= sizeof(ExEntity)) {
		drawEntityValue(*(const ExEntity*)value, editor);
	}
	else if (kind == EX_TYPE_CPTR && editor) {
		drawPrimitiveValue(kind, value, type);
		ImGui::SameLine();
		if (ImGuiEx::IconButton(ICON_FA_BUG, "Break into C++ debugger")) debug::debugBreak();
	}
	else if (editable) {
		switch (kind) {
			case EX_TYPE_BOOL: ImGui::Checkbox("##value", (bool*)value); break;
			case EX_TYPE_I8: ImGui::InputScalar("##value", ImGuiDataType_S8, value); break;
			case EX_TYPE_U8: ImGui::InputScalar("##value", ImGuiDataType_U8, value); break;
			case EX_TYPE_I16: ImGui::InputScalar("##value", ImGuiDataType_S16, value); break;
			case EX_TYPE_U16: ImGui::InputScalar("##value", ImGuiDataType_U16, value); break;
			case EX_TYPE_I32: ImGui::InputInt("##value", (i32*)value); break;
			case EX_TYPE_U32: ImGui::InputScalar("##value", ImGuiDataType_U32, value); break;
			case EX_TYPE_I64: ImGui::InputScalar("##value", ImGuiDataType_S64, value); break;
			case EX_TYPE_U64: ImGui::InputScalar("##value", ImGuiDataType_U64, value); break;
			case EX_TYPE_F32: ImGui::InputFloat("##value", (f32*)value); break;
			case EX_TYPE_F64: ImGui::InputDouble("##value", (f64*)value); break;
			case EX_TYPE_ENUM: ImGui::InputInt("##value", (i32*)value); break;
			default: drawPrimitiveValue(kind, value, type); break;
		}
	}
	else {
		drawPrimitiveValue(kind, value, type);
	}
	ImGui::PopID();
}

struct EvoxDebuggerWindow;
static EvoxDebuggerWindow* g_evox_debugger = nullptr;
static bool toggleEvoxBreakpoint(StudioApp& app, const Path& source, u32 line);
static void applyEvoxBreakpointMarkers(CodeEditor& editor, const Path& path);

struct EvoxEditorWindow final : AssetEditorWindow {
	EvoxEditorWindow(const Path& path, StudioApp& app)
		: AssetEditorWindow(app)
		, m_path(path)
		, m_message(app.getAllocator())
	{
		m_editor = createCodeEditor(app);
		m_editor->setTokenColors(EvoxTokens::token_colors);
		m_editor->setTokenizer(&EvoxTokens::tokenize);
		m_editor->focus();

		OutputMemoryStream blob(app.getAllocator());
		if (app.getEngine().getFileSystem().getContentSync(path, blob)) {
			m_editor->setText(StringView((const char*)blob.data(), (u32)blob.size()));
		}
	}

	const char* getName() const override { return "evox_editor"; }
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
		m_editor->clearUnderlines();
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
			ex_module* module = nullptr;
			ImportContext* import_ctx = nullptr;
		};
		auto import_resolver = [](void* userdata, ex_string_view path, ex_string_view alias, ex_string_view* source) -> int {
			ImportResolverContext* ctx = (ImportResolverContext*)userdata;
			if (!ctx || !ctx->module || !ctx->import_ctx) return 0;
			StringView path_view(path.begin, path.length);
			Path file_path;
			if (startsWith(path_view, "core:")) {
				StringView name = path_view.withoutLeft(5);
				const bool has_lum_extension = endsWith(name, ".evox");
				file_path = has_lum_extension ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".evox");
			}
			else {
				file_path = endsWith(path_view, ".evox") ? Path(path_view) : Path(path_view, ".evox");
			}
			OutputMemoryStream& import_blob = ctx->import_ctx->sources.emplace(*ctx->import_ctx->allocator);
			if (!ctx->import_ctx->filesystem->getContentSync(file_path, import_blob)) {
				ctx->import_ctx->sources.pop();
				return 0;
			}
			*source = ex_string_view{(const char*)import_blob.data(), (i64)import_blob.size()};
			return 1;
		};
		ImportContext import_ctx(m_app.getEngine().getFileSystem(), m_app.getAllocator());
		ImportResolverContext resolver_ctx = {};
		ex_host host = {};
		ex_default_arena_create(&host.arena);
		host.diagnostics_userdata = &diagnostics_message;
		host.print = [](void* userdata, ex_string_view msg) {
			((String*)userdata)->append(StringView(msg.begin, msg.length));
		};
		ex_module* module = ex_module_create(&host);
		if (module) {
			resolver_ctx.module = module;
			resolver_ctx.import_ctx = &import_ctx;
			if (ex_module_compile(module,
				ex_string_view{(const char*)blob.data(), (i64)blob.size()},
				ex_string_view{m_path.c_str(), stringLength(m_path.c_str())},
				import_resolver,
				&resolver_ctx))
			{
				m_message = "OK";
				logInfo("Evox check OK: ", m_path);
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
				logError("Evox check failed: ", m_path, ": ", diagnostics_message);
			}
			ex_module_destroy(module);
		}
		else {
			m_message = "Failed to allocate Evox module";
			logError("Evox check failed: ", m_path, ": Failed to allocate Evox module");
		}
		ex_default_arena_destroy(&host.arena);
	}

	void windowGUI() override {
		World* world = m_app.getWorldEditor().getWorld();
		EvoxModule* module = world ? static_cast<EvoxModule*>(world->getModule("evox")) : nullptr;
		ex_runtime* runtime = module ? module->getDebugRuntime() : nullptr;

		u32 current_line = 0;
		if (runtime && ex_debug_is_suspended(runtime)) {
			ex_debug_event event = {};
			if (ex_debug_pause_event(runtime, &event) == EX_RESULT_OK) {
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
						|| (endsWith(ed_name, ".evox")
							&& equalStrings(StringView(event_filename, event_source.end()), StringView(ed_name.data, ed_name.end() - 4)))) {
						current_line = event.location.line > 0 ? event.location.line - 1 : 0;
					}
				}
			}
		}

		if (runtime && ex_debug_is_suspended(runtime)) {
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
			if (g_evox_check.iconButton(true, &m_app)) check();
			ImGui::EndMenuBar();
		}

		if (m_message.length() > 0) {
			ImGui::TextUnformatted(m_message.c_str());
			ImGui::Separator();
		}
		applyEvoxBreakpointMarkers(*m_editor, m_path);
		if (m_editor->gui("evox_editor", ImGui::GetContentRegionAvail(), m_app.getMonospaceFont(), m_app.getDefaultFont())) {
			m_dirty = true;
		}
		if (m_editor->canHandleInput() && m_app.checkShortcut(g_evox_go_to_definition)) {
			goToDefinition();
		}
		Action* toggle_breakpoint = m_app.getAction("evox_toggle_breakpoint");
		if (toggle_breakpoint && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && m_app.checkShortcut(*toggle_breakpoint)) toggleBreakpointAtCursor();
	}

	void goToDefinition() {
		// TODO do not parse on every go to?
		OutputMemoryStream blob(m_app.getAllocator());
		m_editor->serializeText(blob);
		ex_definition_location location = {};
		const ex_string_view source{(const char*)blob.data(), (i64)blob.size()};
		const ex_string_view source_name{m_path.c_str(), (i64)stringLength(m_path.c_str())};
		ex_host host = {};
		ex_default_arena_create(&host.arena);
		struct ImportContext {
			FileSystem* filesystem;
			IAllocator* allocator;
			Array<OutputMemoryStream> sources;
			ImportContext(FileSystem& fs, IAllocator& allocator)
				: filesystem(&fs)
				, allocator(&allocator)
				, sources(allocator) {}
		};
		ImportContext import_ctx(m_app.getEngine().getFileSystem(), m_app.getAllocator());
		auto import_resolver = [](void* userdata, ex_string_view import_path, ex_string_view, ex_string_view* imported_source) -> int {
			ImportContext* ctx = (ImportContext*)userdata;
			StringView path(import_path.begin, import_path.length);
			Path file_path;
			if (startsWith(path, "core:")) {
				StringView name = path.withoutLeft(5);
				file_path = endsWith(name, ".evox") ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".evox");
			} else {
				file_path = endsWith(path, ".evox") ? Path(path) : Path(path, ".evox");
			}
			OutputMemoryStream& blob = ctx->sources.emplace(*ctx->allocator);
			if (!ctx->filesystem->getContentSync(file_path, blob)) {
				ctx->sources.pop();
				return 0;
			}
			*imported_source = {(const char*)blob.data(), (i64)blob.size()};
			return 1;
		};
		ex_module* module = ex_module_create(&host);
		if (module && ex_module_compile(module, source, source_name, import_resolver, &import_ctx) == EX_RESULT_OK &&
			ex_module_definition_at(module, source_name, m_editor->getCursorLine(), m_editor->getCursorColumn(), &location) == EX_RESULT_OK) {
			StringView definition_source(location.source_name.begin, location.source_name.length);
			Path definition_path;
			if (startsWith(definition_source, "core:")) {
				StringView name = definition_source.withoutLeft(5);
				definition_path = endsWith(name, ".evox") ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".evox");
			} else {
				definition_path = endsWith(definition_source, ".evox") ? Path(definition_source) : Path(definition_source, ".evox");
			}
			m_app.getAssetBrowser().openEditor(definition_path);
			AssetEditorWindow* window = m_app.getAssetBrowser().getWindow(definition_path);
			if (window) {
				EvoxEditorWindow* editor_window = (EvoxEditorWindow*)window;
				editor_window->m_editor->setSelection(location.line, location.column, location.line, location.column + location.length, true);
				editor_window->m_editor->focus();
			}
		}
		if (module) ex_module_destroy(module);
		ex_default_arena_destroy(&host.arena);
	}

	void toggleBreakpointAtCursor() {
		const u32 line = m_editor->getCursorLine() + 1;
		const bool enabled = toggleEvoxBreakpoint(m_app, m_path, line);
		m_editor->setBreakpoint(line - 1, enabled);
	}

	Path m_path;
	UniquePtr<CodeEditor> m_editor;
	String m_message;
};

struct EvoxAssetPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	explicit EvoxAssetPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("evox", EvoxResource::TYPE);
	}

	void addSubresources(AssetCompiler& compiler, const Path& path, AtomicI32&) override {
		compiler.addResource(EvoxResource::TYPE, path);
	}

	void openEditor(const Path& path) override {
		auto win = UniquePtr<EvoxEditorWindow>::create(m_app.getAllocator(), path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool compile(const Path& src) override {
		// For Evox, we just copy the source file as-is
		// The runtime will parse and compile it when loaded
		return m_app.getAssetCompiler().copyCompile(src);
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "evox"; }
	
	void createResource(OutputMemoryStream& content) override {
		const char* template_str = R"(fn update(dt : f32) : void {
	// Update logic here
}
)";
		content.write(template_str, stringLength(template_str));
	}
	
	const char* getIcon() const override { return ICON_FA_FILE_CODE; }
	const char* getLabel() const override { return "Evox"; }
	ResourceType getResourceType() const override { return EvoxResource::TYPE; }

	StudioApp& m_app;
};

struct EvoxDebuggerWindow final : StudioApp::GUIPlugin {
	struct Breakpoint {
		Path source;
		u32 line;
	};

	explicit EvoxDebuggerWindow(StudioApp& app)
		: m_app(app)
		, m_breakpoints(app.getAllocator())
	{
		g_evox_debugger = this;
	}

	~EvoxDebuggerWindow() {
		if (g_evox_debugger == this) g_evox_debugger = nullptr;
	}

	const char* getName() const override { return "evox_debugger_control"; }
	bool isOpen() const { return m_is_open; }
	void setOpen(bool open) { m_is_open = open; }

	Array<Breakpoint>& getBreakpoints() { return m_breakpoints; }
	const Array<Breakpoint>& getBreakpoints() const { return m_breakpoints; }

	bool toggleBreakpoint(const Path& source, u32 line) {
		World* world = m_app.getWorldEditor().getWorld();
		EvoxModule* module = world ? static_cast<EvoxModule*>(world->getModule("evox")) : nullptr;
		for (i32 i = 0; i < m_breakpoints.size(); ++i) {
			if (m_breakpoints[i].line != line || m_breakpoints[i].source != source) continue;
			removeBreakpoint(module, source, line);
			m_breakpoints.erase(i);
			return false;
		}
		addBreakpoint(module, source, line);
		return true;
	}

	void addBreakpoint(EvoxModule* module, const Path& source, u32 line) {
		if (hasBreakpoint(source, line)) return;
		m_breakpoints.emplace(Breakpoint{source, line});
		if (module && module->getDebugRuntime()) module->setDebugBreakpoint(source, line);
	}

	void removeBreakpoint(EvoxModule* module, const Path& source, u32 line) {
		if (module && module->getDebugRuntime()) module->removeDebugBreakpoint(source, line);
	}

	void applyBreakpoints(EvoxModule* module) {
		if (!module->getDebugRuntime()) return;
		for (const Breakpoint& breakpoint : m_breakpoints) module->setDebugBreakpoint(breakpoint.source, breakpoint.line);
	}

	void update(float) override {
		World* world = m_app.getWorldEditor().getWorld();
		EvoxModule* module = world ? static_cast<EvoxModule*>(world->getModule("evox")) : nullptr;
		if (module) applyBreakpoints(module);
	}

	void onGUI() override {
		if (!m_is_open) return;

		World* world = m_app.getWorldEditor().getWorld();
		EvoxModule* module = world ? static_cast<EvoxModule*>(world->getModule("evox")) : nullptr;
		ex_runtime* runtime = module ? module->getDebugRuntime() : nullptr;
		const bool suspended = runtime && ex_debug_is_suspended(runtime);
		const bool just_suspended = suspended && !m_was_suspended;
		m_was_suspended = suspended;

		if (just_suspended) {
			ImGui::SetNextWindowFocus();
		}

		ImGui::SetNextWindowDockID(m_app.getDockspaceID(), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(ICON_FA_BUG " Evox Debugger", &m_is_open)) {
			ImGui::End();
			return;
		}

		if (module) applyBreakpoints(module);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

		// Execution controls
		if (runtime && ex_debug_is_suspended(runtime)) {
			if (g_debugger_continue.iconButton(true, &m_app)) { ex_debug_resume(runtime, EX_DEBUG_CONTINUE); m_step_requested = true; }
			ImGui::SameLine();
			if (g_debugger_step_over.iconButton(true, &m_app)) { ex_debug_resume(runtime, EX_DEBUG_STEP_OVER); m_step_requested = true; }
			ImGui::SameLine();
			if (g_debugger_step_into.iconButton(true, &m_app)) { ex_debug_resume(runtime, EX_DEBUG_STEP_INTO); m_step_requested = true; }
			ImGui::SameLine();
			if (g_debugger_step_out.iconButton(true, &m_app)) { ex_debug_resume(runtime, EX_DEBUG_STEP_OUT); m_step_requested = true; }
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
			ex_debug_event event = {};
			if (ex_debug_pause_event(runtime, &event) == EX_RESULT_OK) {
				ImGui::Text("Suspended: %s", pauseReason(event.reason));
				if (event.reason == EX_DEBUG_PAUSE_ERROR && event.message.length > 0) {
					ImGui::TextWrapped("%.*s", int(event.message.length), event.message.begin);
				}
			}
		}

		const bool should_focus = just_suspended || m_step_requested;
		m_step_requested = false;
		if (should_focus && runtime && ex_debug_is_suspended(runtime)) {
			ex_debug_event event = {};
			if (ex_debug_pause_event(runtime, &event) == EX_RESULT_OK) {
				const StringView src_name(event.location.source_name.begin, event.location.source_name.length);
				Path path;
				if (startsWith(src_name, "core:")) {
					const StringView file_name = src_name.withoutLeft(5);
					const bool has_lum = endsWith(file_name, ".evox");
					path = has_lum ? Path("engine/scripts/core/", file_name) : Path("engine/scripts/core/", file_name, ".evox");
				} else {
					// Imported units use the import spelling as their source name
					// (e.g. `import demo` -> `demo`), while the asset is demo.evox.
					path = endsWith(src_name, ".evox") ? Path(src_name) : Path(src_name, ".evox");
				}
				m_app.getAssetBrowser().openEditor(path);
				AssetEditorWindow* win = m_app.getAssetBrowser().getWindow(path);
				if (win) {
					win->m_focus_request = true;
					auto* exwin = (EvoxEditorWindow*)win;
					const u32 line_zero = event.location.line > 0 ? event.location.line - 1 : 0;
					exwin->m_editor->setSelection(line_zero, 0, line_zero, 0, true);
					exwin->m_editor->focus();
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
			ex_debug_event event = {};
			ex_debug_pause_event(runtime, &event);
			ImGui::Separator();

			if (ImGui::BeginTable("debugger_callstack", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
				ImGui::TableSetupColumn("#");
				ImGui::TableSetupColumn("Function");
				ImGui::TableSetupColumn("Source");
				ImGui::TableHeadersRow();
				const u32 frame_count = ex_debug_stack_depth(runtime);
				if (g_debug_frame_index >= frame_count) g_debug_frame_index = 0;
				for (u32 i = 0; i < frame_count; ++i) {
					ex_debug_location location;
					ex_debug_frame_location(runtime, i, &location);
					const ex_string_view name = ex_debug_frame_function_name(runtime, i);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%u", i);
					ImGui::TableNextColumn();
					ImGui::PushID((int)i);
					if (ImGui::Selectable("##frame", g_debug_frame_index == i, ImGuiSelectableFlags_SpanAllColumns)) {
						g_debug_frame_index = i;
					}
					ImGui::PopID();
					ImGui::SameLine();
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
								const bool has_lum = endsWith(file_name, ".evox");
								path = has_lum ? Path("engine/scripts/core/", file_name) : Path("engine/scripts/core/", file_name, ".evox");
							} else {
								path = Path(src_name);
							}
							m_app.getAssetBrowser().openEditor(path);
							AssetEditorWindow* win = m_app.getAssetBrowser().getWindow(path);
							if (win) {
								auto* exwin = (EvoxEditorWindow*)win;
								const u32 line_zero = location.line > 0 ? location.line - 1 : 0;
								exwin->m_editor->setSelection(line_zero, 0, line_zero, 0, true);
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

	static const char* pauseReason(ex_debug_pause_reason reason) {
		switch (reason) {
			case EX_DEBUG_PAUSE_BREAKPOINT: return "breakpoint";
			case EX_DEBUG_PAUSE_STEP: return "step";
			case EX_DEBUG_PAUSE_ERROR: return "error";
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

static void applyEvoxBreakpointMarkers(CodeEditor& editor, const Path& path) {
	if (!g_evox_debugger) return;
	for (const auto& bp : g_evox_debugger->getBreakpoints()) {
		if (bp.source == path) editor.setBreakpoint(bp.line - 1, true);
	}
}

struct EvoxVariablesWindow final : StudioApp::GUIPlugin {
	explicit EvoxVariablesWindow(StudioApp& app)
		: m_app(app)
		, m_is_open(true) {
		m_filter[0] = '\0';
	}
	const char* getName() const override { return "evox_variables"; }
	bool isOpen() const { return m_is_open; }
	void setOpen(bool open) { m_is_open = open; }

	static bool nameMatchesFilter(ex_string_view name, const char* filter) {
		if (!filter || !filter[0]) return true;
		for (const char* c = name.begin; c < name.begin + name.length; ++c) {
			const char* f = filter;
			const char* n = c;
			while (*f && n < name.begin + name.length && toLower(*n) == toLower(*f)) {
				++n;
				++f;
			}
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
		EvoxModule* module = world ? static_cast<EvoxModule*>(world->getModule("evox")) : nullptr;
		ex_runtime* runtime = module ? module->getDebugRuntime() : nullptr;
		g_debug_runtime = runtime;

		const bool suspended = runtime && ex_debug_is_suspended(runtime);
		if (!runtime) {
			ImGui::TextDisabled("Waiting for runtime...");
		} else {
			ImGui::InputTextWithHint("##filter", "Filter variables...", m_filter, sizeof(m_filter), ImGuiInputTextFlags_AutoSelectAll);
			const bool has_filter = m_filter[0] != '\0';

			const ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerV
				| ImGuiTableFlags_Resizable
				| ImGuiTableFlags_RowBg
				| ImGuiTableFlags_ScrollY;
			if (ImGui::BeginTable("evox_variables", 2, table_flags)) {
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.5f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.5f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				if (suspended) {
					const u32 frame_count = ex_debug_stack_depth(runtime);
					if (g_debug_frame_index >= frame_count) g_debug_frame_index = 0;
					for (u32 i = 0, n = ex_debug_frame_local_count(runtime, g_debug_frame_index); i < n; ++i) {
						const ex_string_view name = ex_debug_local_name(runtime, g_debug_frame_index, i);
						if (has_filter && !nameMatchesFilter(name, m_filter)) continue;
						const ex_type_kind kind = ex_debug_local_kind(runtime, g_debug_frame_index, i);
						const ex_type* type = ex_debug_local_type(runtime, g_debug_frame_index, i);
						u32 size = 0;
						void* value = ex_debug_local_value(runtime, g_debug_frame_index, i, &size);
						drawVariable(name, kind, type, value, size, false, &m_app.getWorldEditor());
					}
				}

				for (u32 i = 0, n = ex_debug_global_count(runtime); i < n; ++i) {
					const ex_string_view name = ex_debug_global_name(runtime, i);
					if (has_filter && !nameMatchesFilter(name, m_filter)) continue;
					const ex_type_kind kind = ex_debug_global_kind(runtime, i);
					const ex_type* type = ex_debug_global_type(runtime, i);
					u32 size = 0;
					void* value = ex_debug_global_value(runtime, i, &size);
					drawVariable(name, kind, type, value, size, false, &m_app.getWorldEditor());
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

static bool toggleEvoxBreakpoint(StudioApp& app, const Path& source, u32 line) {
	if (!g_evox_debugger) return false;
	return g_evox_debugger->toggleBreakpoint(source, line);
}

static Action g_toggle_variables_window{"Evox", "Variables window", "Show/hide variables window", "evox_toggle_variables", ICON_FA_CUBE, Action::Type::TOOL};

struct EvoxDataCommand final : IEditorCommand {
	EvoxDataCommand(WorldEditor& editor, EntityRef entity, const ex_type* type, bool add)
		: m_editor(editor)
		, m_entity(entity)
		, m_type(type)
		, m_add(add)
	{}

	bool execute() override { return apply(m_add); }
	void undo() override { apply(!m_add); }
	const char* getType() override { return m_add ? "add_evox_data" : "remove_evox_data"; }
	bool merge(IEditorCommand&) override { return false; }

	bool apply(bool add) {
		World* world = m_editor.getWorld();
		if (!world || !world->hasEntity(m_entity)) return false;
		EvoxModule* module = static_cast<EvoxModule*>(world->getModule("evox"));
		if (!module) return false;
		return add ? module->addEvoxData(m_entity, m_type) : module->removeEvoxData(m_entity, m_type);
	}

	WorldEditor& m_editor;
	EntityRef m_entity;
	const ex_type* m_type;
	bool m_add;
};

struct EvoxPropertyGridPlugin final : PropertyGrid::IPlugin {
	void onGUI(PropertyGrid&, Span<const EntityRef> entities, ComponentType cmp_type, const TextFilter& filter, WorldEditor& editor) override {
		if (entities.length() != 1) return;
		World* world = editor.getWorld();
		IModule* base_module = world ? world->getModule(cmp_type) : nullptr;
		if (!base_module || !equalStrings(base_module->getName(), "evox")) return;

		EvoxModule& module = static_cast<EvoxModule&>(*base_module);
		const EntityRef entity = entities[0];
		for (u32 i = 0, count = module.getEvoxDataCount(entity); i < count; ++i) {
			const ex_type* type = module.getEvoxDataType(entity, i);
			if (!type) continue;
			const ex_string_view ex_name = ex_type_get_name(type);
			const StringView name(ex_name.begin, ex_name.length);
			if (filter.isActive() && !filter.pass(name)) continue;

			ImGui::PushID(type);
			if (ImGuiEx::IconButton(ICON_FA_TIMES, "Remove data")) {
				UniquePtr<IEditorCommand> command = UniquePtr<EvoxDataCommand>::create(
					editor.getAllocator(), editor, entity, type, false);
				editor.executeCommand(command.move());
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			const bool open = ImGui::TreeNodeEx("##data", ImGuiTreeNodeFlags_SpanFullWidth, "%.*s", int(name.length), name.data);
			if (open) {
				if (ImGui::BeginTable("##data_table", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.5f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.5f);
					const void* value = module.getEvoxData(entity, type);
					const ex_type_kind kind = ex_type_get_kind(type);
					if (kind == EX_TYPE_STRUCT) {
						for (u32 j = 0, n = ex_type_struct_field_count(type); j < n; ++j) {
							const ex_string_view field_name = ex_type_struct_field_name(type, j);
							const ex_type* field_type = ex_type_struct_field_type(type, j);
							const u32 offset = ex_type_struct_field_offset(type, j);
							drawVariable(field_name
								, field_type ? ex_type_get_kind(field_type) : EX_TYPE_INVALID
								, field_type
								, value ? (void*)((const u8*)value + offset) : nullptr
								, field_type ? ex_type_get_size(field_type) : 0
								, true);
						}
					}
					else {
						static const char value_name[] = "value";
						drawVariable({value_name, lengthOf(value_name) - 1}, kind, type, (void*)value, ex_type_get_size(type), true);
					}
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (ImGui::Button(ICON_FA_PLUS " Add data")) ImGui::OpenPopup("evox_add_data");
		if (ImGui::BeginPopup("evox_add_data")) {
			for (const ex_type* type : module.getEvoxDataTypes()) {
				if (module.hasEvoxData(entity, type)) continue;
				const ex_string_view ex_name = ex_type_get_name(type);
				const StringView name(ex_name.begin, ex_name.length);
				if (ImGui::MenuItem(StaticString<128>(name))) {
					UniquePtr<IEditorCommand> command = UniquePtr<EvoxDataCommand>::create(
						editor.getAllocator(), editor, entity, type, true);
					editor.executeCommand(command.move());
				}
			}
			ImGui::EndPopup();
		}
	}

};

struct EvoxPlugin : StudioApp::IPlugin {
	explicit EvoxPlugin(StudioApp& app)
		: m_app(app)
		, m_asset_plugin(app)
		, m_debugger(app)
		, m_variables_window(app)
	{
	}

	const char* getName() const override { return "evox"; }

	void init() override {
		const char* evox_exts[] = {"evox"};
		g_toggle_evox_breakpoint.shortcut = os::Keycode::F8;
		g_debugger_continue.shortcut = os::Keycode::F1;
		g_debugger_step_over.shortcut = os::Keycode::F2;
		g_debugger_step_into.shortcut = os::Keycode::F3;
		g_debugger_step_out.shortcut = os::Keycode::SHIFT | os::Keycode::F11;
		m_app.getAssetBrowser().addPlugin(m_asset_plugin, Span(evox_exts));
		m_app.getAssetCompiler().addPlugin(m_asset_plugin, Span(evox_exts));
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
		EvoxModule* module = world ? static_cast<EvoxModule*>(world->getModule("evox")) : nullptr;
		ex_runtime* runtime = module ? module->getDebugRuntime() : nullptr;

		if (runtime && ex_debug_is_suspended(runtime)) {
			if (m_app.checkShortcut(g_debugger_continue, true)) { ex_debug_resume(runtime, EX_DEBUG_CONTINUE); m_debugger.m_step_requested = true; }
			if (m_app.checkShortcut(g_debugger_step_over, true)) { ex_debug_resume(runtime, EX_DEBUG_STEP_OVER); m_debugger.m_step_requested = true; }
			if (m_app.checkShortcut(g_debugger_step_into, true)) { ex_debug_resume(runtime, EX_DEBUG_STEP_INTO); m_debugger.m_step_requested = true; }
			if (m_app.checkShortcut(g_debugger_step_out, true)) { ex_debug_resume(runtime, EX_DEBUG_STEP_OUT); m_debugger.m_step_requested = true; }
		}
	}

	bool showGizmo(struct WorldView& view, struct ComponentUID cmp) override {
		return false;
	}

	~EvoxPlugin() {
		m_app.getPropertyGrid().removePlugin(m_property_grid_plugin);
		m_app.removePlugin(m_variables_window);
		m_app.removePlugin(m_debugger);
		m_app.getAssetBrowser().removePlugin(m_asset_plugin);
		m_app.getAssetCompiler().removePlugin(m_asset_plugin);
	}

private:
	StudioApp& m_app;
	EvoxAssetPlugin m_asset_plugin;
	EvoxDebuggerWindow m_debugger;
	EvoxVariablesWindow m_variables_window;
	EvoxPropertyGridPlugin m_property_grid_plugin;
	Action m_debugger_action{"Evox", "Debugger", "Evox Debugger", "evox_debugger", ICON_FA_BUG, Action::Type::TOOL};
};

LUMIX_STUDIO_ENTRY(evox) {
	auto& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, EvoxPlugin)(app);
}

} // namespace Lumix
