#include "meta.h"
#include <stdio.h>

#define OUT_ENDL "\r\n"
#define L(...) out.add(__VA_ARGS__, OUT_ENDL)

namespace {

enum class LumScriptType { UNKNOWN, VOID_T, BOOL_T, I32_T, F32_T, VEC2_T, VEC3_T, DVEC3_T, VEC4_T, COLOR_T, QUAT_T, ENTITY_T, ENUM_T, STRUCT_T, PATH_T };

template <int CAPACITY> struct StaticString {
	template <typename... Args> StaticString(Args&&... args) {
		buffer[0] = 0;
		int dummy[] = {(append(args), 0)...};
	}

	static size_t strlen(const char* v) {
		size_t len = 0;
		while (v[len]) ++len;
		return len;
	}

	static void memcpy(void* dst, const void* src, size_t size) {
		char* d = (char*)dst;
		char* s = (char*)src;
		for (size_t i = 0; i < size; ++i) d[i] = s[i];
	}

	void append(const char* v) {
		int v_len = (int)strlen(v);
		if (length + v_len >= CAPACITY) return;
		memcpy(buffer + length, v, v_len + 1);
		length += v_len;
	}

	void append(StringView v) {
		int v_len = v.size();
		if (length + v_len >= CAPACITY) return;
		memcpy(buffer + length, v.begin, v_len);
		length += v_len;
		buffer[length] = 0;
	}

	operator const char*() const { return buffer; }

	char buffer[CAPACITY];
	int length = 0;
};

char toLowerAscii(char c) {
	if (c >= 'A' && c <= 'Z') return char(c - 'A' + 'a');
	return c;
}

template <int CAPACITY> void appendLowercase(StaticString<CAPACITY>& out, StringView value) {
	for (const char* c = value.begin; c != value.end; ++c) {
		const char lower = toLowerAscii(*c);
		out.append(StringView{&lower, &lower + 1});
	}
}

static MetaData* g_meta_data = nullptr;

template <typename... Args> void logInfo(Args... args) {
	StaticString<4096> str(args...);
	fputs(str.buffer, stdout);
	fputc('\n', stdout);
}

// Finds enum metadata by reflected type token (short or qualified name).
const Enum* findEnumByTypeName(StringView type) {
	if (!g_meta_data) return nullptr;
	for (Enum& e : g_meta_data->enums) {
		if (equal(type, e.name) || equal(type, e.full)) return &e;
	}
	for (Module& m : g_meta_data->modules) {
		for (Enum& e : m.enums) {
			if (equal(type, e.name) || equal(type, e.full)) return &e;
		}
	}
	return nullptr;
}

// Finds struct metadata by reflected type token (short or qualified name).
const Struct* findStructByTypeName(StringView type) {
	if (!g_meta_data) return nullptr;
	for (Struct& s : g_meta_data->structs) {
		if (equal(type, s.name) || equal(type, s.full)) return &s;
	}
	return nullptr;
}

// Emits C++ enum type name suitable for casts (uses fully-qualified metadata name when available).
void appendEnumCPPType(OutputStream& out, StringView type) {
	const Enum* e = findEnumByTypeName(type);
	out.add(e ? e->full : type);
}

// Emits C++ struct type name suitable for casts (uses fully-qualified metadata name when available).
void appendStructCPPType(OutputStream& out, StringView type) {
	const Struct* s = findStructByTypeName(type);
	out.add(s ? s->full : type);
}

// Maps reflected C++ type names to the compact LumScript type category.
LumScriptType getLumScriptType(StringView type) {
	// Single source of truth for supported LumScript-facing C++ types.
	if (equal(type, "void")) return LumScriptType::VOID_T;
	if (equal(type, "bool")) return LumScriptType::BOOL_T;
	if (equal(type, "i32") || equal(type, "int") || equal(type, "u32")) return LumScriptType::I32_T;
	if (equal(type, "float")) return LumScriptType::F32_T;
	if (equal(type, "Vec2")) return LumScriptType::VEC2_T;
	if (equal(type, "Vec3")) return LumScriptType::VEC3_T;
	if (equal(type, "DVec3")) return LumScriptType::DVEC3_T;
	if (equal(type, "Vec4")) return LumScriptType::VEC4_T;
	if (equal(type, "Color")) return LumScriptType::COLOR_T;
	if (equal(type, "Quat")) return LumScriptType::QUAT_T;
	if (equal(type, "EntityRef") || equal(type, "EntityPtr")) return LumScriptType::ENTITY_T;
	if (equal(type, "Path")) return LumScriptType::PATH_T;
	if (findEnumByTypeName(type)) return LumScriptType::ENUM_T;
	if (findStructByTypeName(type)) return LumScriptType::STRUCT_T;
	return LumScriptType::UNKNOWN;
}

// Returns true when a reflected type can be represented in LumScript bindings.
bool isSupportedLumScriptType(StringView type) {
	const LumScriptType t = getLumScriptType(type);
	if (t == LumScriptType::UNKNOWN) return false;
	if (t != LumScriptType::STRUCT_T) return true;
	const Struct* s = findStructByTypeName(type);
	if (!s) return false;
	for (const StructVar& v : s->vars) {
		const LumScriptType field_type = getLumScriptType(v.type);
		if (field_type == LumScriptType::UNKNOWN) return false;
		if (field_type == LumScriptType::PATH_T) return false;
		if (field_type == LumScriptType::STRUCT_T && !isSupportedLumScriptType(v.type)) return false;
	}
	return true;
}

// Detects supported C-string input arguments (`const char*`).
bool isLumScriptStringArg(const Arg& arg) {
	return arg.is_const && arg.is_ptr && equal(arg.type, "char");
}

// Detects Path input arguments represented as const Path&.
bool isLumScriptPathArg(const Arg& arg) {
	return arg.is_const && arg.is_ref && equal(arg.type, "Path");
}

// Checks whether the type is an entity handle type.
bool isLumScriptEntityType(StringView type) {
	return getLumScriptType(type) == LumScriptType::ENTITY_T;
}

// Returns how many runtime stack slots a reflected type consumes.
i32 getLumScriptTypeStackSlots(StringView type);

// Returns how many runtime stack slots a reflected argument consumes.
i32 getLumScriptArgStackSlots(const Arg& arg) {
	return getLumScriptTypeStackSlots(arg.type);
}

// Validates argument shape and type for generated wrapper calls.
bool isSupportedLumScriptArg(const Arg& arg) {
	if (isLumScriptStringArg(arg)) return true;
	if (arg.is_ptr) return false;
	if (arg.is_ref && !arg.is_const) return false;
	return isSupportedLumScriptType(arg.type);
}

// Emits a reflected struct value by consuming the flattened stack slots for each field.
void appendArgValue(OutputStream& out, const Arg& arg, i32 idx);
void appendStructArgValue(OutputStream& out, StringView type, i32 idx) {
	const Struct* s = findStructByTypeName(type);
	if (!s) {
		out.add(type);
		return;
	}

	out.add("([&]() {");
	appendStructCPPType(out, type);
	out.add(" res{};");
	i32 slot_idx = 0;
	for (const StructVar& v : s->vars) {
		const LumScriptType field_type = getLumScriptType(v.type);
		if (field_type == LumScriptType::VOID_T) continue;
		if (field_type == LumScriptType::PATH_T) continue;
		Arg field_arg;
		field_arg.type = v.type;
		const i32 field_slots = getLumScriptTypeStackSlots(v.type);
		out.add("res.", v.name, " = ");
		appendArgValue(out, field_arg, idx + slot_idx);
		out.add(";");
		slot_idx += field_slots;
	}
	out.add("return res; }())");
}

// Returns true when an argument is supported by generic function wrappers (excluding enums).
bool isSupportedLumScriptFunctionArg(const Arg& arg) {
	if (isLumScriptStringArg(arg)) return true;
	if (isLumScriptPathArg(arg)) return true;
	if (arg.is_ptr) return false;
	if (arg.is_ref && !arg.is_const) return false;
	const LumScriptType type = getLumScriptType(arg.type);
	return type != LumScriptType::UNKNOWN && type != LumScriptType::ENUM_T && type != LumScriptType::PATH_T;
}

// Returns true when any function argument blocks export generation.
bool hasUnsupportedLumScriptFunctionArg(Function& f) {
	bool unsupported = false;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptFunctionArg(arg)) unsupported = true;
	});
	return unsupported;
}

// Resolves the exposed script name, preferring explicit alias when present.
StringView functionScriptName(Function& f) {
	return f.attributes.alias.size() > 0 ? f.attributes.alias : f.name;
}

// Emits a diagnostic for a skipped function whose arguments are not supported.
void logUnsupportedLumScriptFunctionArgs(const char* scope, StringView owner, Function& f) {
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptFunctionArg(arg)) {
			logInfo("LumScript: skipped ", scope, " ", owner, ".", functionScriptName(f), " because arg ", arg.name, " of type ", arg.type, " is not supported");
		}
	});
}

void appendArgValue(OutputStream& out, const Arg& arg, i32 idx) {
	if (isLumScriptStringArg(arg)) {
		out.add("lumscript_string_arg_", idx);
		return;
	}
	if (isLumScriptPathArg(arg)) {
		out.add("Path(lumscript_string_arg_", idx, ")");
		return;
	}
	switch (getLumScriptType(arg.type)) {
		case LumScriptType::BOOL_T: out.add("bool(ls_to_bool(runtime, ", -idx, "))"); break;
		case LumScriptType::I32_T:
			if (equal(arg.type, "u32"))
				out.add("ls_to_u32(runtime, ", -idx, ")");
			else
				out.add("ls_to_i32(runtime, ", -idx, ")");
			break;
		case LumScriptType::F32_T: out.add("ls_to_f32(runtime, ", -idx, ")"); break;
		case LumScriptType::VEC2_T: out.add("Vec2(ls_to_f32(runtime, ", -idx - 1, "), ls_to_f32(runtime, ", -idx, "))"); break;
		case LumScriptType::VEC3_T: out.add("Vec3(ls_to_f32(runtime, ", -idx - 2, "), ls_to_f32(runtime, ", -idx - 1, "), ls_to_f32(runtime, ", -idx, "))"); break;
		case LumScriptType::DVEC3_T: out.add("DVec3(ls_to_f64(runtime, ", -idx - 2, "), ls_to_f64(runtime, ", -idx - 1, "), ls_to_f64(runtime, ", -idx, "))"); break;
		case LumScriptType::VEC4_T: out.add("Vec4(ls_to_f32(runtime, ", -idx - 3, "), ls_to_f32(runtime, ", -idx - 2, "), ls_to_f32(runtime, ", -idx - 1, "), ls_to_f32(runtime, ", -idx, "))"); break;
		case LumScriptType::COLOR_T:
			out.add("Color(u8(ls_to_i32(runtime, ", -idx - 3, ")), u8(ls_to_i32(runtime, ", -idx - 2, ")), u8(ls_to_i32(runtime, ", -idx - 1, ")), u8(ls_to_i32(runtime, ", -idx, ")))");
			break;
		case LumScriptType::QUAT_T: out.add("Quat(ls_to_f32(runtime, ", -idx - 3, "), ls_to_f32(runtime, ", -idx - 2, "), ls_to_f32(runtime, ", -idx - 1, "), ls_to_f32(runtime, ", -idx, "))"); break;
		case LumScriptType::ENTITY_T:
			if (equal(arg.type, "EntityRef"))
				out.add("EntityRef(ls_to_i32(runtime, ", -idx - 1, "))");
			else
				out.add("EntityPtr(ls_to_i32(runtime, ", -idx - 1, "))");
			break;
		case LumScriptType::ENUM_T:
			out.add("(");
			appendEnumCPPType(out, arg.type);
			out.add(")ls_to_i32(runtime, ", -idx, ")");
			break;
		case LumScriptType::PATH_T: out.add("Path()"); break;
		case LumScriptType::STRUCT_T: appendStructArgValue(out, arg.type, idx); break;
		default: break;
	}
}

// Returns how many runtime stack slots a reflected type consumes.
i32 getLumScriptTypeStackSlots(StringView type) {
	switch (getLumScriptType(type)) {
		case LumScriptType::VEC2_T: return 2;
		case LumScriptType::VEC3_T: return 3;
		case LumScriptType::DVEC3_T: return 3;
		case LumScriptType::VEC4_T: return 4;
		case LumScriptType::COLOR_T: return 4;
		case LumScriptType::QUAT_T: return 4;
		case LumScriptType::ENTITY_T: return 2;
		case LumScriptType::VOID_T: return 0;
		case LumScriptType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			if (!s) return 0;
			i32 slots = 0;
			for (const StructVar& v : s->vars) {
				slots += getLumScriptTypeStackSlots(v.type);
			}
			return slots;
		}
		default: return 1;
	}
}

// TODO get rid of these temporaries
// Emits a local temporary used to bridge LumScript string arguments to `const char*`.
void emitStringArgTemp(OutputStream& out, i32 idx, i32 stack_idx) {
	L("ls_string_view lumscript_string_sv_", idx, " = ls_to_string(runtime, ", -stack_idx, ");");
	L("char lumscript_string_arg_", idx, "[128];");
	L("copyString(Span(lumscript_string_arg_", idx, "), StringView{lumscript_string_sv_", idx, ".begin, lumscript_string_sv_", idx, ".end});");
}

void appendReturnValue(OutputStream& out, StringView type, const char* value, const char* world_expr = nullptr) {
	if (getLumScriptType(type) == LumScriptType::VOID_T) return;
	switch (getLumScriptType(type)) {
		case LumScriptType::BOOL_T: L("ls_push_bool(runtime, ", value, ");"); break;
		case LumScriptType::I32_T: L("ls_push_i32(runtime, (i32)", value, ");"); break;
		case LumScriptType::F32_T: L("ls_push_f32(runtime, ", value, ");"); break;
		case LumScriptType::VEC2_T:
			L("ls_push_f32(runtime, ", value, ".x);");
			L("ls_push_f32(runtime, ", value, ".y);");
			break;
		case LumScriptType::VEC3_T:
			L("ls_push_f32(runtime, ", value, ".x);");
			L("ls_push_f32(runtime, ", value, ".y);");
			L("ls_push_f32(runtime, ", value, ".z);");
			break;
		case LumScriptType::DVEC3_T:
			L("ls_push_f64(runtime, ", value, ".x);");
			L("ls_push_f64(runtime, ", value, ".y);");
			L("ls_push_f64(runtime, ", value, ".z);");
			break;
		case LumScriptType::VEC4_T:
			L("ls_push_f32(runtime, ", value, ".x);");
			L("ls_push_f32(runtime, ", value, ".y);");
			L("ls_push_f32(runtime, ", value, ".z);");
			L("ls_push_f32(runtime, ", value, ".w);");
			break;
		case LumScriptType::COLOR_T:
			L("ls_push_i32(runtime, ", value, ".r);");
			L("ls_push_i32(runtime, ", value, ".g);");
			L("ls_push_i32(runtime, ", value, ".b);");
			L("ls_push_i32(runtime, ", value, ".a);");
			break;
		case LumScriptType::QUAT_T:
			L("ls_push_f32(runtime, ", value, ".x);");
			L("ls_push_f32(runtime, ", value, ".y);");
			L("ls_push_f32(runtime, ", value, ".z);");
			L("ls_push_f32(runtime, ", value, ".w);");
			break;
		case LumScriptType::ENTITY_T:
			L("ls_push_i32(runtime, ", value, ".index);");
			if (world_expr) L("ls_push_ptr(runtime, ", world_expr, ");");
			break;
		case LumScriptType::ENUM_T: L("ls_push_i32(runtime, (i32)", value, ");"); break;
		case LumScriptType::PATH_T: L("ls_push_string(runtime, ls_string_view{", value, ".c_str(), ", value, ".c_str() + ", value, ".length()});"); break;
		case LumScriptType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			if (!s) break;
			for (const StructVar& v : s->vars) {
				if (getLumScriptType(v.type) == LumScriptType::VOID_T) continue;
				StaticString<128> field_value(value, ".", v.name);
				appendReturnValue(out, v.type, field_value.buffer, world_expr);
			}
			break;
		}
	}
}

// Returns true when return type and all arguments are wrapper-compatible.
bool isSupportedLumScriptFunction(Function& f) {
	const LumScriptType ret_type = getLumScriptType(f.return_type);
	if (!isSupportedLumScriptType(f.return_type) || ret_type == LumScriptType::PATH_T) return false;
	bool supported = true;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptFunctionArg(arg)) supported = false;
	});
	return supported;
}

// Returns true when an argument is supported by property wrappers.
bool isSupportedLumScriptPropertyArg(const Arg& arg) {
	if (isLumScriptPathArg(arg)) return true;
	return isSupportedLumScriptArg(arg);
}

// Returns true when a component property getter can be exported to LumScript.
bool isSupportedLumScriptPropertyGetter(Property& p) {
	if (p.getter_name.size() == 0) return false;
	if (!isSupportedLumScriptType(p.type)) return false;
	bool supported = true;
	forEachArg(p.getter_args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptPropertyArg(arg)) supported = false;
	});
	return supported;
}

// Returns true when a component property setter can be exported to LumScript.
bool isSupportedLumScriptPropertySetter(Property& p) {
	if (p.setter_name.size() == 0) return false;
	bool supported = true;
	forEachArg(p.setter_args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptPropertyArg(arg)) supported = false;
	});
	return supported;
}

// Returns true when array accessor has at least entity and index arguments.
bool hasArrayAccessorPrefixArgs(StringView args) {
	i32 count = 0;
	bool ok = true;
	forEachArg(args, [&](const Arg& arg, bool) {
		if (count == 0 && !isLumScriptEntityType(arg.type)) ok = false;
		if (count == 1 && !(equal(arg.type, "i32") || equal(arg.type, "int") || equal(arg.type, "u32"))) ok = false;
		++count;
	});
	return ok && count >= 2;
}

// Returns true when an array child getter can be exported to LumScript.
bool isSupportedLumScriptArrayChildGetter(Property& p) {
	if (p.getter_name.size() == 0) return false;
	if (!isSupportedLumScriptType(p.type)) return false;
	if (!hasArrayAccessorPrefixArgs(p.getter_args)) return false;
	i32 idx = 0;
	bool supported = true;
	forEachArg(p.getter_args, [&](const Arg& arg, bool) {
		if (idx >= 2 && !isSupportedLumScriptPropertyArg(arg)) supported = false;
		++idx;
	});
	return supported;
}

// Returns true when an array child setter can be exported to LumScript.
bool isSupportedLumScriptArrayChildSetter(Property& p) {
	if (p.setter_name.size() == 0) return false;
	if (!hasArrayAccessorPrefixArgs(p.setter_args)) return false;
	i32 idx = 0;
	bool supported = true;
	forEachArg(p.setter_args, [&](const Arg& arg, bool) {
		if (idx >= 2 && !isSupportedLumScriptPropertyArg(arg)) supported = false;
		++idx;
	});
	return supported;
}

// Emits a stable generated wrapper symbol name.
void appendWrapperName(OutputStream& out, Component& c, Function& f, i32 idx) {
	out.add("lumscript_", c.id, "_", functionScriptName(f), "_", idx);
}

// Emits a stable generated wrapper symbol name for property accessors.
void appendPropertyWrapperName(OutputStream& out, Component& c, Property& p, bool is_setter, i32 idx) {
	out.add("lumscript_", c.id, "_", is_setter ? p.setter_name : p.getter_name, "_", idx);
}

// Emits the native wrapper function body for one reflected component method.
void serializeLumScriptWrapper(OutputStream& out, Module& m, Component& c, Function& f, i32 idx) {
	out.add("static void ");
	appendWrapperName(out, c, f, idx);
	L("(ls_runtime* runtime) {");
	L("auto* module = static_cast<", m.name, "*>(ls_to_ptr(runtime, -1));");

	i32 total_slots = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) { total_slots += getLumScriptArgStackSlots(arg); });
	i32 slot_idx = 0;
	i32 string_arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		const i32 stack_idx = total_slots - slot_idx - getLumScriptArgStackSlots(arg) + 1;
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg)) {
			emitStringArgTemp(out, string_arg_idx + 1, stack_idx);
			++string_arg_idx;
		}
		slot_idx += getLumScriptArgStackSlots(arg);
	});
	if (!equal(f.return_type, "void")) out.add("auto ret = ");
	out.add("module->", f.name, "(");
	slot_idx = 0;
	string_arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		const i32 stack_idx = total_slots - slot_idx - getLumScriptArgStackSlots(arg) + 1;
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg))
			appendArgValue(out, arg, string_arg_idx + 1);
		else
			appendArgValue(out, arg, stack_idx);
		slot_idx += getLumScriptArgStackSlots(arg);
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg)) ++string_arg_idx;
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

// Emits the native wrapper function body for one reflected component property accessor.
void serializeLumScriptPropertyWrapper(OutputStream& out, Module& m, Component& c, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L("(ls_runtime* runtime) {");
	L("auto* module = static_cast<", m.name, "*>(ls_to_ptr(runtime, -1));");

	i32 total_slots = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) { total_slots += getLumScriptArgStackSlots(arg); });
	i32 slot_idx = 0;
	i32 string_arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		const i32 stack_idx = total_slots - slot_idx - getLumScriptArgStackSlots(arg) + 1;
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg)) {
			emitStringArgTemp(out, string_arg_idx + 1, stack_idx);
			++string_arg_idx;
		}
		slot_idx += getLumScriptArgStackSlots(arg);
	});
	if (!is_setter) out.add("auto ret = ");
	out.add("module->", is_setter ? p.setter_name : p.getter_name, "(");
	string_arg_idx = 0;
	slot_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		const i32 stack_idx = total_slots - slot_idx - getLumScriptArgStackSlots(arg) + 1;
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg))
			appendArgValue(out, arg, string_arg_idx + 1);
		else
			appendArgValue(out, arg, stack_idx);
		slot_idx += getLumScriptArgStackSlots(arg);
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg)) ++string_arg_idx;
	});
	L(");");
	if (!is_setter) appendReturnValue(out, p.type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

// Emits a stable generated wrapper symbol name for array count accessor.
void appendArrayCountWrapperName(OutputStream& out, Component& c, ArrayProperty& a, i32 idx) {
	out.add("lumscript_", c.id, "_", a.id, "_count_", idx);
}

// Emits a stable generated wrapper symbol name for array item accessor.
void appendArrayItemWrapperName(OutputStream& out, Component& c, ArrayProperty& a, i32 idx) {
	out.add("lumscript_", c.id, "_", a.id, "_item_", idx);
}

// Emits a stable generated wrapper symbol name for array child accessors.
void appendArrayChildWrapperName(OutputStream& out, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	out.add("lumscript_", c.id, "_", a.id, "_", is_setter ? p.setter_name : p.getter_name, "_", idx);
}

// Emits a stable generated wrapper symbol name for module function wrappers.
void appendModuleWrapperName(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("lumscript_", m.id, "_", functionScriptName(f), "_", idx);
}

// Emits the native wrapper function body for one reflected module method.
void serializeLumScriptModuleWrapper(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("static void ");
	appendModuleWrapperName(out, m, f, idx);
	L("(ls_runtime* runtime) {");
	L("auto* module = static_cast<", m.name, "*>(ls_to_ptr(runtime, -1));");

	i32 total_slots = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) { total_slots += getLumScriptArgStackSlots(arg); });
	i32 string_arg_idx = 0;
	i32 src_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		const i32 stack_idx = total_slots - src_idx - getLumScriptArgStackSlots(arg) + 1;
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg)) {
			emitStringArgTemp(out, string_arg_idx + 1, stack_idx);
			++string_arg_idx;
		}
		++src_idx;
	});

	if (!equal(f.return_type, "void")) out.add("auto ret = ");
	out.add("module->", f.name, "(");
	src_idx = 0;
	i32 slot_idx = 0;
	string_arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		const i32 stack_idx = total_slots - slot_idx - getLumScriptArgStackSlots(arg) + 1;
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg))
			appendArgValue(out, arg, string_arg_idx + 1);
		else
			appendArgValue(out, arg, stack_idx);
		slot_idx += getLumScriptArgStackSlots(arg);
		++src_idx;
		if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg)) ++string_arg_idx;
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

// Emits wrapper that returns array element count.
void serializeLumScriptArrayCountWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static void ");
	appendArrayCountWrapperName(out, c, a, idx);
	L("(ls_runtime* runtime) {");
	L("auto* module = static_cast<", m.name, "*>(ls_to_ptr(runtime, -1));");
	L("const i32 count = module->get", a.name, "Count(EntityRef(ls_to_i32(runtime, -2)));");
	L("ls_push_i32(runtime, count);");
	L("}" OUT_ENDL);
}

// Emits wrapper that returns an array item handle from component handle + index.
void serializeLumScriptArrayItemWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static void ");
	appendArrayItemWrapperName(out, c, a, idx);
	L("(ls_runtime* runtime) {");
	L("auto* module = static_cast<", m.name, "*>(ls_to_ptr(runtime, -2));");
	L("const i32 count = module->get", a.name, "Count(EntityRef(ls_to_i32(runtime, -3)));");
	L("if (ls_to_i32(runtime, -1) < 0 || ls_to_i32(runtime, -1) >= count) {");
	L("ls_push_null(runtime);");
	L("return;");
	L("}");
	out.add("ls_push_i32(runtime, ls_to_i32(runtime, -2));" OUT_ENDL);
	L("ls_push_i64(runtime, ls_to_i32(runtime, -1));");
	L("ls_push_ptr(runtime, module);");
	L("}" OUT_ENDL);
}

// Emits wrapper for array child getter/setter accessors.
void serializeLumScriptArrayChildWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendArrayChildWrapperName(out, c, a, p, is_setter, idx);
	L("(ls_runtime* runtime) {");
	i32 total_slots = 3;
	forEachArg(accessor_args, [&](const Arg& arg, bool) { total_slots += getLumScriptArgStackSlots(arg); });
	L("auto* module = static_cast<", m.name, "*>(ls_to_ptr(runtime, ", -total_slots + 2, "));");

	i32 string_arg_idx = 0;
	i32 slot_idx = 3;
	i32 src_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (src_idx >= 2) {
			const i32 stack_idx = total_slots - slot_idx - getLumScriptArgStackSlots(arg) + 1;
			if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg)) {
				emitStringArgTemp(out, string_arg_idx + 1, stack_idx);
				++string_arg_idx;
			}
			slot_idx += getLumScriptArgStackSlots(arg);
		}
		++src_idx;
	});

	if (!is_setter) out.add("auto ret = ");
	out.add("module->", is_setter ? p.setter_name : p.getter_name, "(");
	out.add("EntityRef(ls_to_i32(runtime, ", -total_slots, ")), (i32)ls_to_i64(runtime, ", -total_slots + 1, ")");
	src_idx = 0;
	slot_idx = 3;
	string_arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (src_idx >= 2) {
			out.add(", ");
			const i32 stack_idx = total_slots - slot_idx - getLumScriptArgStackSlots(arg) + 1;
			if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg))
				appendArgValue(out, arg, string_arg_idx + 1);
			else
				appendArgValue(out, arg, stack_idx);
			slot_idx += getLumScriptArgStackSlots(arg);
			if (isLumScriptStringArg(arg) || isLumScriptPathArg(arg)) ++string_arg_idx;
		}
		++src_idx;
	});
	L(");");
	if (!is_setter) appendReturnValue(out, p.type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

// Emits generated file preamble and includes needed by produced bindings.
void emitGeneratedHeader(OutputStream& out, MetaData& data) {
	out.add("// Generated by meta.cpp" OUT_ENDL OUT_ENDL);
	out.add("#pragma once" OUT_ENDL OUT_ENDL);
	out.add("#include \"lumscript/capi.h\"" OUT_ENDL);
	out.add("#include <string.h>" OUT_ENDL);
	out.add("#include \"core/stream.h\"" OUT_ENDL);
	out.add("#include \"engine/reflection.h\"" OUT_ENDL);
	out.add("#include \"engine/world.h\"" OUT_ENDL);
	for (Module& m : data.modules) {
		StringView include_path = makeStringView(m.filename);
		// Generated header lives under src/lumscript, so plugin includes need one extra "..".
		if (startsWith(include_path, "plugins/")) {
			out.add("#include \"../", include_path, "\"" OUT_ENDL);
		} else {
			if (startsWith(include_path, "src/")) include_path = withoutPrefix(include_path, 4);
			out.add("#include \"", include_path, "\"" OUT_ENDL);
		}
	}
	out.add(OUT_ENDL);
}

// Emits wrappers that expose world-level module handles, one per reflected module.
void emitGeneratedWorldModuleAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		Component& first_component = m.components[0];
		out.add("static void lumscript_world_", m.id);
		L("(ls_runtime* runtime) {");
		L("World* world = (World*)ls_to_ptr(runtime, -1);");
		L("IModule* module = world->getModule(reflection::getComponentType(\"", first_component.id, "\"));");
		L("if (!module) { ls_push_null(runtime); return; }");
		L("ls_push_ptr(runtime, module);");
		L("}" OUT_ENDL);
	}
}

// Emits per-component entity-to-component accessor wrappers.
void emitGeneratedComponentEntityAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("static void lumscript_entity_", c.id, "(ls_runtime* runtime) {");
			L("World* world = (World*)ls_to_ptr(runtime, -1);");
			L("i32 entity_idx = ls_to_i32(runtime, -2);");
			L("const ComponentType component_type = reflection::getComponentType(\"", c.id, "\");");
			L("IModule* module = world->getModule(component_type);");
			L("if (!module || !world->hasComponent(EntityRef(entity_idx), component_type)) {");
			L("ls_push_null(runtime);");
			L("return;");
			L("}");
			L("ls_push_i32(runtime, entity_idx);");
			L("ls_push_ptr(runtime, module);");
			L("}" OUT_ENDL);
		}
	}
}

// Emits wrappers for all supported reflected module methods.
void emitGeneratedModuleWrappers(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Module& m : data.modules) {
		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			serializeLumScriptModuleWrapper(out, m, f, *wrapper_idx);
			++*wrapper_idx;
		}
	}
}

// Emits wrappers for all supported reflected component functions.
void emitGeneratedComponentWrappers(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				serializeLumScriptWrapper(out, m, c, f, *wrapper_idx);
				++*wrapper_idx;
			}
			for (Property& p : c.properties) {
				if (isSupportedLumScriptPropertyGetter(p)) {
					serializeLumScriptPropertyWrapper(out, m, c, p, false, *wrapper_idx);
					++*wrapper_idx;
				}
				if (isSupportedLumScriptPropertySetter(p)) {
					serializeLumScriptPropertyWrapper(out, m, c, p, true, *wrapper_idx);
					++*wrapper_idx;
				}
			}
			for (ArrayProperty& a : c.arrays) {
				serializeLumScriptArrayCountWrapper(out, m, c, a, *wrapper_idx);
				++*wrapper_idx;
				serializeLumScriptArrayItemWrapper(out, m, c, a, *wrapper_idx);
				++*wrapper_idx;
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p)) {
						serializeLumScriptArrayChildWrapper(out, m, c, a, p, false, *wrapper_idx);
						++*wrapper_idx;
					}
					if (isSupportedLumScriptArrayChildSetter(p)) {
						serializeLumScriptArrayChildWrapper(out, m, c, a, p, true, *wrapper_idx);
						++*wrapper_idx;
					}
				}
			}
		}
	}
}

// Emits registration branches for reflected component imports.
void emitGeneratedComponentImportRegistrations(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			bool has_supported_function = false;
			for (Function& f : c.functions) {
				if (isSupportedLumScriptFunction(f)) {
					has_supported_function = true;
					break;
				}
			}
			bool has_supported_property = false;
			for (Property& p : c.properties) {
				if (isSupportedLumScriptPropertyGetter(p) || isSupportedLumScriptPropertySetter(p)) {
					has_supported_property = true;
					break;
				}
			}
			bool has_supported_array = false;
			for (ArrayProperty& a : c.arrays) {
				bool array_supported = false;
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p) || isSupportedLumScriptArrayChildSetter(p)) {
						array_supported = true;
						break;
					}
				}
				if (array_supported) {
					has_supported_array = true;
					break;
				}
			}
			if (!has_supported_function && !has_supported_property && !has_supported_array) continue;

			for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				out.add("functions.insert(StringView(\"core:", c.id, ".", functionScriptName(f), "\"), &");
				appendWrapperName(out, c, f, *wrapper_idx);
				L(");");
				++*wrapper_idx;
			}
			for (Property& p : c.properties) {
				if (isSupportedLumScriptPropertyGetter(p)) {
					out.add("functions.insert(StringView(\"core:", c.id, ".", p.getter_name, "\"), &");
					appendPropertyWrapperName(out, c, p, false, *wrapper_idx);
					L(");");
					++*wrapper_idx;
				}
				if (isSupportedLumScriptPropertySetter(p)) {
					out.add("functions.insert(StringView(\"core:", c.id, ".", p.setter_name, "\"), &");
					appendPropertyWrapperName(out, c, p, true, *wrapper_idx);
					L(");");
					++*wrapper_idx;
				}
			}
			for (ArrayProperty& a : c.arrays) {
				L("functions.insert(StringView(\"core:", c.id, ".", a.id, "Count\"), &");
				appendArrayCountWrapperName(out, c, a, *wrapper_idx);
				L(");");
				++*wrapper_idx;
				L("functions.insert(StringView(\"core:", c.id, ".", a.id, "\"), &");
				appendArrayItemWrapperName(out, c, a, *wrapper_idx);
				L(");");
				++*wrapper_idx;
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p)) {
						out.add("functions.insert(StringView(\"core:", c.id, ".", p.getter_name, "\"), &");
						appendArrayChildWrapperName(out, c, a, p, false, *wrapper_idx);
						L(");");
						++*wrapper_idx;
					}
					if (isSupportedLumScriptArrayChildSetter(p)) {
						out.add("functions.insert(StringView(\"core:", c.id, ".", p.setter_name, "\"), &");
						appendArrayChildWrapperName(out, c, a, p, true, *wrapper_idx);
						L(");");
						++*wrapper_idx;
					}
				}
			}
		}
	}
}

// Emits a LumScript reference type name for declaration-only signatures.
void appendLumScriptDeclType(OutputStream& out, StringView type) {
	switch (getLumScriptType(type)) {
		case LumScriptType::VOID_T: out.add("void"); break;
		case LumScriptType::BOOL_T: out.add("bool"); break;
		case LumScriptType::I32_T: out.add("i32"); break;
		case LumScriptType::F32_T: out.add("f32"); break;
		case LumScriptType::VEC2_T: out.add("Vec2"); break;
		case LumScriptType::VEC3_T: out.add("Vec3"); break;
		case LumScriptType::DVEC3_T: out.add("DVec3"); break;
		case LumScriptType::VEC4_T: out.add("Vec4"); break;
		case LumScriptType::COLOR_T: out.add("Color"); break;
		case LumScriptType::QUAT_T: out.add("Quat"); break;
		case LumScriptType::ENTITY_T: out.add("Entity"); break;
		case LumScriptType::ENUM_T: {
			const Enum* e = findEnumByTypeName(type);
			out.add(e ? e->name : type);
			break;
		}
		case LumScriptType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			out.add(s ? s->name : type);
			break;
		}
		case LumScriptType::PATH_T: out.add("string"); break;
		default: out.add("void"); break;
	}
}

// Emits a LumScript reference argument type for declaration-only signatures.
void appendLumScriptDeclArgType(OutputStream& out, const Arg& arg) {
	if (isLumScriptStringArg(arg)) {
		out.add("string");
		return;
	}
	appendLumScriptDeclType(out, arg.type);
}

// Emits the import token for a reflected type that lives in a generated core .lum file.
void appendLumScriptImportType(OutputStream& out, StringView type) {
	if (equal(type, "World")) {
		out.add("world");
		return;
	}
	switch (getLumScriptType(type)) {
		case LumScriptType::VEC2_T: out.add("vec2"); break;
		case LumScriptType::VEC3_T: out.add("vec3"); break;
		case LumScriptType::DVEC3_T: out.add("dvec3"); break;
		case LumScriptType::VEC4_T: out.add("vec4"); break;
		case LumScriptType::COLOR_T: out.add("color"); break;
		case LumScriptType::QUAT_T: out.add("quat"); break;
		case LumScriptType::ENTITY_T: out.add("entity"); break;
		case LumScriptType::ENUM_T: {
			const Enum* e = findEnumByTypeName(type);
			out.add(e ? e->name : type);
			break;
		}
		case LumScriptType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			out.add(s ? s->name : type);
			break;
		}
		default: out.add(type); break;
	}
}

// Emits a declaration argument name, falling back to a stable generated placeholder.
void appendLumScriptDeclArgName(OutputStream& out, StringView name, i32 idx) {
	if (name.size() > 0) {
		out.add(name);
		return;
	}
	out.add("arg", idx + 1);
}

// Emits one declaration line for a reflected function in a component import context.
void emitComponentFunctionDecl(OutputStream& out, Component& c, Function& f) {
	out.add("extern fn ", functionScriptName(f), "(");
	i32 arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendLumScriptDeclArgName(out, arg.name, arg_idx);
		out.add(" : ");
		if (is_first && isLumScriptEntityType(arg.type))
			out.add(c.name);
		else
			appendLumScriptDeclArgType(out, arg);
		++arg_idx;
	});
	out.add(") : ");
	appendLumScriptDeclType(out, f.return_type);
	out.add(";" OUT_ENDL);
}

// Emits one declaration line for a reflected property accessor in a component import context.
void emitComponentPropertyDecl(OutputStream& out, Component& c, Property& p, bool is_setter) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	StringView script_name = is_setter ? p.setter_name : p.getter_name;
	out.add("extern fn ", script_name, "(");
	i32 arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendLumScriptDeclArgName(out, arg.name, arg_idx);
		out.add(" : ");
		if (is_first && isLumScriptEntityType(arg.type))
			out.add(c.name);
		else
			appendLumScriptDeclArgType(out, arg);
		++arg_idx;
	});
	out.add(") : ");
	if (is_setter)
		out.add("void");
	else
		appendLumScriptDeclType(out, p.type);
	out.add(";" OUT_ENDL);
}

// Emits one declaration line for an array child accessor.
void emitArrayChildPropertyDecl(OutputStream& out, Component& c, ArrayProperty& a, Property& p, bool is_setter) {
	StringView script_name = is_setter ? p.setter_name : p.getter_name;
	out.add("extern fn ", script_name, "(");
	out.add("item : ", c.name, a.name, "ArrayItem");
	i32 arg_idx = 0;
	forEachArg(is_setter ? p.setter_args : p.getter_args, [&](const Arg& arg, bool) {
		if (arg_idx >= 2) {
			out.add(", ");
			appendLumScriptDeclArgName(out, arg.name, arg_idx);
			out.add(" : ");
			appendLumScriptDeclArgType(out, arg);
		}
		++arg_idx;
	});
	out.add(") : ");
	if (is_setter)
		out.add("void");
	else
		appendLumScriptDeclType(out, p.type);
	out.add(";" OUT_ENDL);
}

// Emits one LumScript enum declaration for reference-only API docs.
void emitLumScriptReferenceEnum(OutputStream& out, Enum& e) {
	if (e.values.size == 0) return;
	out.add("enum ", e.name, " {" OUT_ENDL);
	for (i32 i = 0, c = e.values.size; i < c; ++i) {
		Enumerator& en = e.values[i];
		out.add("\t", en.name, " = ", en.value);
		if (i != c - 1) out.add(",");
		out.add(OUT_ENDL);
	}
	out.add("};" OUT_ENDL OUT_ENDL);
}

// create /data/scripts/core/* files
void serializeCoreImports(MetaData& data) {
	OutputStream out;

	// enums
	auto output_enum = [&](Enum& e) {
		if (e.values.size == 0) return;

		out.length = 0;
		L("// Generated by meta.cpp" OUT_ENDL);
		L("enum ", e.name, " {");
		for (i32 i = 0, c = e.values.size; i < c; ++i) {
			const Enumerator& en = e.values[i];
			out.add("\t", en.name, " = ", en.value);
			if (i != c - 1) out.add(",");
			out.add(OUT_ENDL);
		}
		L("};" OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendLowercase(path, e.name);
		path.append(".lum");
		writeFile(path, out);
	};

	for (Enum& e : data.enums) output_enum(e);
	for (Module& m : data.modules) {
		for (Enum& e : m.enums) output_enum(e);
	}

	// structs
	auto output_struct = [&](Struct& s) {
		if (s.vars.size == 0) return;

		out.length = 0;
		L("// Generated by meta.cpp" OUT_ENDL);
		for (i32 i = 0; i < s.vars.size; ++i) {
			const StructVar& v = s.vars[i];
			const LumScriptType type = getLumScriptType(v.type);
			if (type != LumScriptType::VEC2_T && type != LumScriptType::VEC3_T && type != LumScriptType::DVEC3_T && type != LumScriptType::VEC4_T && type != LumScriptType::COLOR_T &&
				type != LumScriptType::QUAT_T && type != LumScriptType::ENTITY_T && type != LumScriptType::ENUM_T && type != LumScriptType::STRUCT_T) {
				continue;
			}
			if (equal(v.type, s.name)) continue;
			bool already_imported = false;
			for (i32 j = 0; j < i; ++j) {
				if (equal(s.vars[j].type, v.type)) {
					already_imported = true;
					break;
				}
			}
			if (already_imported) continue;
			out.add("import \"core:");
			appendLumScriptImportType(out, v.type);
			out.add("\"" OUT_ENDL);
		}
		L("struct ", s.name, " {");
		for (StructVar& v : s.vars) {
			out.add("\t", v.name, " : ");
			appendLumScriptDeclType(out, v.type);
			L(";");
		}
		L("};" OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendLowercase(path, s.name);
		path.append(".lum");
		writeFile(path, out);
	};

	for (Struct& s : data.structs) output_struct(s);

	// modules
	for (Module& m : data.modules) {
		bool has_supported_function = false;
		for (Function& f : m.functions) {
			if (isSupportedLumScriptFunction(f)) {
				has_supported_function = true;
				break;
			}
		}
		if (!has_supported_function) continue;

		out.length = 0;
		L("// Generated by meta.cpp");
		L("// import core:", m.id, " as ", m.id, OUT_ENDL);
		L("import \"core:world\"", OUT_ENDL);

		StringView imported_types[32];
		i32 imported_types_count = 0;
		auto emitImportForType = [&](StringView type) {
			if (type.size() == 0) return;
			if (type.begin[0] == '?') type = {type.begin + 1, type.end};
			const LumScriptType import_type = getLumScriptType(type);
			if (import_type != LumScriptType::VEC2_T && import_type != LumScriptType::VEC3_T && import_type != LumScriptType::DVEC3_T && import_type != LumScriptType::VEC4_T &&
				import_type != LumScriptType::COLOR_T && import_type != LumScriptType::QUAT_T && import_type != LumScriptType::ENTITY_T && import_type != LumScriptType::ENUM_T &&
				import_type != LumScriptType::STRUCT_T && !equal(type, "World")) {
				return;
			}
			if (equal(type, m.name)) return;
			for (i32 i = 0; i < imported_types_count; ++i) {
				if (equal(imported_types[i], type)) return;
			}
			imported_types[imported_types_count++] = type;
			out.add("import \"core:");
			appendLumScriptImportType(out, type);
			out.add("\"" OUT_ENDL);
		};
		emitImportForType(StringView{"World", "World" + 5});
		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			emitImportForType(f.return_type);
			forEachArg(f.args, [&](const Arg& arg, bool) { emitImportForType(arg.type); });
		}

		L("struct ", m.name, " { module : cptr; }" OUT_ENDL);
		L("extern fn ", m.id, "(w : World) : ?", m.name, ";");

		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			out.add("extern fn ", functionScriptName(f), "(module : ", m.name);
			forEachArg(f.args, [&](const Arg& arg, bool) {
				out.add(", ", arg.name, " : ");
				appendLumScriptDeclArgType(out, arg);
			});
			out.add(") : ");
			appendLumScriptDeclType(out, f.return_type);
			out.add(";" OUT_ENDL);
		}
		out.add(OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendLowercase(path, m.id);
		path.append(".lum");
		writeFile(path, out);
	}

	// components
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			bool has_supported_function = false;
			for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				has_supported_function = true;
				break;
			}
			bool has_supported_property = false;
			for (Property& p : c.properties) {
				if (isSupportedLumScriptPropertyGetter(p) || isSupportedLumScriptPropertySetter(p)) {
					has_supported_property = true;
					break;
				}
			}
			bool has_supported_array = false;
			for (ArrayProperty& a : c.arrays) {
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p) || isSupportedLumScriptArrayChildSetter(p)) {
						has_supported_array = true;
						break;
					}
				}
				if (has_supported_array) break;
			}
			if (!has_supported_function && !has_supported_property && !has_supported_array) continue;

			out.length = 0;
			L("// Generated by meta.cpp");
			L("// import core:", c.id, " as ", c.id, OUT_ENDL);

			StringView imported_types[32];
			i32 imported_types_count = 0;
			auto emitImportForType = [&](StringView type) {
				if (type.size() == 0) return;
				if (type.begin[0] == '?') type = {type.begin + 1, type.end};
				const LumScriptType import_type = getLumScriptType(type);
				if (import_type != LumScriptType::VEC2_T && import_type != LumScriptType::VEC3_T && import_type != LumScriptType::DVEC3_T && import_type != LumScriptType::VEC4_T &&
					import_type != LumScriptType::COLOR_T && import_type != LumScriptType::QUAT_T && import_type != LumScriptType::ENTITY_T && import_type != LumScriptType::ENUM_T &&
					import_type != LumScriptType::STRUCT_T)
					return;
				if (equal(type, c.name)) return;
				for (i32 i = 0; i < imported_types_count; ++i) {
					if (equal(imported_types[i], type)) return;
				}
				imported_types[imported_types_count++] = type;
				out.add("import \"core:");
				appendLumScriptImportType(out, type);
				out.add("\"" OUT_ENDL);
			};
			emitImportForType(StringView{"Entity", "Entity" + 6});
			for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				emitImportForType(f.return_type);
				forEachArg(f.args, [&](const Arg& arg, bool) { emitImportForType(arg.type); });
			}
			for (Property& p : c.properties) {
				emitImportForType(p.type);
				forEachArg(p.getter_args, [&](const Arg& arg, bool) { emitImportForType(arg.type); });
				forEachArg(p.setter_args, [&](const Arg& arg, bool) { emitImportForType(arg.type); });
			}
			for (ArrayProperty& a : c.arrays) {
				for (Property& p : a.children) {
					emitImportForType(p.type);
					forEachArg(p.getter_args, [&](const Arg& arg, bool) { emitImportForType(arg.type); });
					forEachArg(p.setter_args, [&](const Arg& arg, bool) { emitImportForType(arg.type); });
				}
			}

			L("struct ", c.name, " { entity : i32; module : cptr; }" OUT_ENDL);
			for (ArrayProperty& a : c.arrays) {
				bool has_array_child = false;
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p) || isSupportedLumScriptArrayChildSetter(p)) {
						has_array_child = true;
						break;
					}
				}
				if (!has_array_child) continue;
				L("struct ", c.name, a.name, "ArrayItem { entity : i32; idx : i32; module : cptr; }" OUT_ENDL);
			}

			L("extern fn ", c.id, "(e : Entity) : ?", c.name, ";");

			for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				emitComponentFunctionDecl(out, c, f);
			}
			for (Property& p : c.properties) {
				if (isSupportedLumScriptPropertyGetter(p)) emitComponentPropertyDecl(out, c, p, false);
				if (isSupportedLumScriptPropertySetter(p)) emitComponentPropertyDecl(out, c, p, true);
			}
			for (ArrayProperty& a : c.arrays) {
				bool has_array_child = false;
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p) || isSupportedLumScriptArrayChildSetter(p)) {
						has_array_child = true;
						break;
					}
				}
				if (!has_array_child) continue;
				L("extern fn ", a.id, "Count(component : ", c.name, ") : i32;");
				L("extern fn ", a.id, "(component : ", c.name, ", idx : i32) : ?", c.name, a.name, "ArrayItem;");
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p)) emitArrayChildPropertyDecl(out, c, a, p, false);
					if (isSupportedLumScriptArrayChildSetter(p)) emitArrayChildPropertyDecl(out, c, a, p, true);
				}
			}
			out.add(OUT_ENDL);
			StaticString<256> path("data/scripts/core/");
			appendLowercase(path, c.id);
			path.append(".lum");
			writeFile(path, out);
		}
	}
}

// Entry point: generates the full LumScript C API header from metadata.
void serializeLumScriptMeta(MetaData& data) {
	g_meta_data = &data;
	for (Module& m : data.modules) {
		for (Function& f : m.functions) {
			if (hasUnsupportedLumScriptFunctionArg(f)) {
				logUnsupportedLumScriptFunctionArgs("module function", m.id, f);
			}
		}
		for (Component& c : m.components) {
			for (Function& f : c.functions) {
				if (hasUnsupportedLumScriptFunctionArg(f)) {
					logUnsupportedLumScriptFunctionArgs("component function", c.id, f);
				}
			}
		}
	}
	OutputStream out;
	emitGeneratedHeader(out, data);

	L("namespace Lumix::LumScript::generated {" OUT_ENDL);

	emitGeneratedWorldModuleAccessors(out, data);
	emitGeneratedComponentEntityAccessors(out, data);
	i32 wrapper_idx = 0;
	emitGeneratedModuleWrappers(out, data, &wrapper_idx);
	emitGeneratedComponentWrappers(out, data, &wrapper_idx);

	// register native functions
	L("static void registerGeneratedEngineImport(HashMap<StringView, ls_native_fn>& functions) {");
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("functions.insert(StringView(\"core:entity.", c.id, "\"), &lumscript_entity_", c.id, ");");
		}
	}
	wrapper_idx = 0;
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;

		bool any_function = false;
		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			out.add("functions.insert(StringView(\"core:", m.id, ".", functionScriptName(f), "\"), &");
			appendModuleWrapperName(out, m, f, wrapper_idx);
			L(");");
			++wrapper_idx;
			any_function = true;
		}
		if (any_function) {
			L("functions.insert(StringView(\"core:world.", m.id, "\"), &lumscript_world_", m.id, ");");
		}
	}
	emitGeneratedComponentImportRegistrations(out, data, &wrapper_idx);

	L("}" OUT_ENDL);
	L("} // namespace Lumix::LumScript::generated");
	formatCPP(out);
	writeFile("src/lumscript/lumscript_capi.gen.h", out);

	serializeCoreImports(data);
	g_meta_data = nullptr;
}

} // anonymous namespace

META_PLUGIN(serializeLumScriptMeta)
