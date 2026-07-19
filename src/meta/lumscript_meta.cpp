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

	static void memcpy(void* dst, const void* src, size_t size) {
		char* d = (char*)dst;
		char* s = (char*)src;
		for (size_t i = 0; i < size; ++i) d[i] = s[i];
	}

	void append(const char* v) {
		int v_len = 0;
		while (v[v_len]) ++v_len;
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

template <int CAPACITY> void appendLowercase(StaticString<CAPACITY>& out, StringView value) {
	for (const char* c = value.begin; c != value.end; ++c) {
		const char lower = *c >= 'A' && *c <= 'Z' ? char(*c - 'A' + 'a') : *c;
		out.append(StringView{&lower, &lower + 1});
	}
}

static MetaData* g_meta_data = nullptr;

template <typename... Args> void logInfo(Args... args) {
	StaticString<4096> str(args...);
	fputs(str.buffer, stdout);
	fputc('\n', stdout);
}

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

const Struct* findStructByTypeName(StringView type) {
	if (!g_meta_data) return nullptr;
	for (Struct& s : g_meta_data->structs) {
		if (equal(type, s.name) || equal(type, s.full)) return &s;
	}
	return nullptr;
}

LumScriptType getLumScriptType(StringView type) {
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

bool isSupportedLumScriptType(StringView type, LumScriptType t) {
	if (t == LumScriptType::UNKNOWN) return false;
	if (t != LumScriptType::STRUCT_T) return true;
	const Struct* s = findStructByTypeName(type);
	if (!s) return false;
	for (const StructVar& v : s->vars) {
		const LumScriptType field_type = getLumScriptType(v.type);
		if (field_type == LumScriptType::UNKNOWN) return false;
		if (field_type == LumScriptType::PATH_T) return false;
		if (field_type == LumScriptType::STRUCT_T && !isSupportedLumScriptType(v.type, field_type)) return false;
	}
	return true;
}

bool isSupportedLumScriptType(StringView type) {
	return isSupportedLumScriptType(type, getLumScriptType(type));
}

bool isLumScriptStringArg(const Arg& arg) {
	return arg.is_const && arg.is_ptr && equal(arg.type, "char");
}

bool isLumScriptPathArg(const Arg& arg) {
	return arg.is_const && arg.is_ref && equal(arg.type, "Path");
}

bool isLumScriptEntityType(StringView type) {
	return getLumScriptType(type) == LumScriptType::ENTITY_T;
}

bool isSupportedLumScriptFunctionArg(const Arg& arg) {
	if (isLumScriptStringArg(arg)) return true;
	if (isLumScriptPathArg(arg)) return true;
	if (arg.is_ptr) return false;
	if (arg.is_ref && !arg.is_const) return false;
	const LumScriptType type = getLumScriptType(arg.type);
	return type != LumScriptType::UNKNOWN && type != LumScriptType::ENUM_T && type != LumScriptType::PATH_T;
}

bool hasUnsupportedLumScriptFunctionArg(Function& f) {
	bool unsupported = false;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptFunctionArg(arg)) unsupported = true;
	});
	return unsupported;
}

StringView functionScriptName(Function& f) {
	return f.attributes.alias.size() > 0 ? f.attributes.alias : f.name;
}

void logUnsupportedLumScriptFunctionArgs(const char* scope, StringView owner, Function& f) {
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptFunctionArg(arg)) {
			logInfo("LumScript: skipped ", scope, " ", owner, ".", functionScriptName(f), " because arg ", arg.name, " of type ", arg.type, " is not supported");
		}
	});
}

void appendValueExpression(OutputStream& out, StringView type, StringView name) {
	switch (getLumScriptType(type)) {
		case LumScriptType::ENTITY_T:
			out.add(equal(type, "EntityRef") ? "EntityRef(" : "EntityPtr(", name, "_index)");
			break;
		case LumScriptType::COLOR_T:
			out.add("Color(u8(", name, "_r), u8(", name, "_g), u8(", name, "_b), u8(", name, "_a))");
			break;
		case LumScriptType::ENUM_T: {
			const Enum* e = findEnumByTypeName(type);
			out.add("(", e ? e->full : type, ")", name, "_value");
			break;
		}
		default: out.add(name); break;
	}
}

void emitFrameValueRead(OutputStream& out, StringView type, StringView name) {
	switch (getLumScriptType(type)) {
		case LumScriptType::ENTITY_T:
			L("LS_ENTITY_ARG(frame, ", name, ", ", name, "_world);");
			break;
		case LumScriptType::COLOR_T:
			L("LS_ARG(frame, i32, ", name, "_r);");
			L("LS_ARG(frame, i32, ", name, "_g);");
			L("LS_ARG(frame, i32, ", name, "_b);");
			L("LS_ARG(frame, i32, ", name, "_a);");
			break;
		case LumScriptType::ENUM_T:
			L("LS_ARG(frame, i32, ", name, "_value);");
			break;
		case LumScriptType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			if (!s) break;
			out.add(s->full);
			L(" ", name, "{};");
			for (const StructVar& v : s->vars) {
				if (getLumScriptType(v.type) == LumScriptType::VOID_T) continue;
				StaticString<256> field_name(name, "_", v.name);
				const StringView field_name_view{field_name.buffer, field_name.buffer + field_name.length};
				emitFrameValueRead(out, v.type, field_name_view);
				out.add(name, ".", v.name, " = ");
				appendValueExpression(out, v.type, field_name_view);
				L(";");
			}
			break;
		}
		default:
			out.add("LS_ARG(frame, ", type, ", ", name, ");" OUT_ENDL);
			break;
	}
}

void emitArgRead(OutputStream& out, const Arg& arg) {
	if (isLumScriptStringArg(arg)) {
		L("LS_STRING_ARG(frame, ", arg.name, ");");
		L("char lumscript_string_arg_", arg.name, "[128];");
		L("copyString(Span(lumscript_string_arg_", arg.name, "), StringView{", arg.name, ".begin, ", arg.name, ".end});");
	}
	else if (isLumScriptPathArg(arg)) L("LS_STRING_ARG(frame, ", arg.name, ");");
	else emitFrameValueRead(out, arg.type, arg.name);
}

void appendArgExpression(OutputStream& out, const Arg& arg) {
	if (isLumScriptStringArg(arg)) out.add("lumscript_string_arg_", arg.name);
	else if (isLumScriptPathArg(arg)) out.add("Path(StringView{", arg.name, ".begin, ", arg.name, ".end})");
	else appendValueExpression(out, arg.type, arg.name);
}

void emitResult(OutputStream& out, const char* value) {
	L("LS_RESULT(frame, ", value, ");");
}

void appendReturnValue(OutputStream& out, StringView type, const char* value, const char* world_expr = nullptr) {
	const LumScriptType lumscript_type = getLumScriptType(type);
	if (lumscript_type == LumScriptType::VOID_T) return;
	switch (lumscript_type) {
		case LumScriptType::BOOL_T: emitResult(out, value); break;
		case LumScriptType::I32_T: {
			StaticString<256> v("(i32)", value);
			emitResult(out, v);
			break;
		}
		case LumScriptType::F32_T: emitResult(out, value); break;
		case LumScriptType::VEC2_T: emitResult(out, value); break;
		case LumScriptType::VEC3_T: emitResult(out, value); break;
		case LumScriptType::DVEC3_T: emitResult(out, value); break;
		case LumScriptType::VEC4_T: emitResult(out, value); break;
		case LumScriptType::COLOR_T: {
			StaticString<256> r("(i32)", value, ".r"); emitResult(out, r);
			StaticString<256> g("(i32)", value, ".g"); emitResult(out, g);
			StaticString<256> b("(i32)", value, ".b"); emitResult(out, b);
			StaticString<256> a("(i32)", value, ".a"); emitResult(out, a);
			break;
		}
		case LumScriptType::QUAT_T: emitResult(out, value); break;
		case LumScriptType::ENTITY_T: {
			StaticString<256> index(value, ".index"); emitResult(out, index);
			emitResult(out, world_expr ? world_expr : "nullptr");
			break;
		}
		case LumScriptType::ENUM_T: {
			StaticString<256> v("(i32)", value);
			emitResult(out, v);
			break;
		}
		case LumScriptType::PATH_T:
			L("ls_result_string(runtime, &frame, ls_string_view{", value, ".c_str(), ", value, ".c_str() + ", value, ".length()});");
			break;
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

bool isSupportedLumScriptFunction(Function& f) {
	const LumScriptType ret_type = getLumScriptType(f.return_type);
	if (!isSupportedLumScriptType(f.return_type, ret_type)) return false;
	bool supported = true;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptFunctionArg(arg)) supported = false;
	});
	return supported;
}

bool isSupportedLumScriptPropertyArg(const Arg& arg) {
	if (isLumScriptPathArg(arg)) return true;
	if (isLumScriptStringArg(arg)) return true;
	if (arg.is_ptr) return false;
	if (arg.is_ref && !arg.is_const) return false;
	return isSupportedLumScriptType(arg.type);
}

bool isSupportedLumScriptPropertyGetter(Property& p) {
	if (p.getter_name.size() == 0) return false;
	const LumScriptType type = getLumScriptType(p.type);
	if (!isSupportedLumScriptType(p.type, type)) return false;
	bool supported = true;
	forEachArg(p.getter_args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptPropertyArg(arg)) supported = false;
	});
	return supported;
}

bool isSupportedLumScriptPropertySetter(Property& p) {
	if (p.setter_name.size() == 0) return false;
	bool supported = true;
	forEachArg(p.setter_args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptPropertyArg(arg)) supported = false;
	});
	return supported;
}

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

bool isSupportedLumScriptArrayChildGetter(Property& p) {
	if (p.getter_name.size() == 0) return false;
	const LumScriptType type = getLumScriptType(p.type);
	if (!isSupportedLumScriptType(p.type, type)) return false;
	if (!hasArrayAccessorPrefixArgs(p.getter_args)) return false;
	i32 idx = 0;
	bool supported = true;
	forEachArg(p.getter_args, [&](const Arg& arg, bool) {
		if (idx >= 2 && !isSupportedLumScriptPropertyArg(arg)) supported = false;
		++idx;
	});
	return supported;
}

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

void appendWrapperName(OutputStream& out, Component& c, Function& f, i32 idx) {
	out.add("lumscript_", c.id, "_", functionScriptName(f), "_", idx);
}

void appendPropertyWrapperName(OutputStream& out, Component& c, Property& p, bool is_setter, i32 idx) {
	out.add("lumscript_", c.id, "_", is_setter ? p.setter_name : p.getter_name, "_", idx);
}

void serializeLumScriptWrapper(OutputStream& out, Module& m, Component& c, Function& f, i32 idx) {
	out.add("static void ");
	appendWrapperName(out, c, f, idx);
	L("(ls_runtime* runtime, ls_call_frame frame) {");
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (is_first) {
			L("LS_COMPONENT_ARG(frame, ", m.name, "*, ", arg.name, ", module);");
		}
		else {
			emitArgRead(out, arg);
		}
	});
	if (!equal(f.return_type, "void")) out.add("auto ret = ");
	out.add("module->", f.name, "(");
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendArgExpression(out, arg);
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

void serializeLumScriptPropertyWrapper(OutputStream& out, Module& m, Component& c, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L("(ls_runtime* runtime, ls_call_frame frame) {");
	forEachArg(accessor_args, [&](const Arg& arg, bool is_first) {
		if (is_first) {
			L("LS_COMPONENT_ARG(frame, ", m.name, "*, ", arg.name, ", module);");
		}
		else {
			emitArgRead(out, arg);
		}
	});
	if (!is_setter) out.add("auto ret = ");
	out.add("module->", is_setter ? p.setter_name : p.getter_name, "(");
	forEachArg(accessor_args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendArgExpression(out, arg);
	});
	L(");");
	if (!is_setter) appendReturnValue(out, p.type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

void appendArrayCountWrapperName(OutputStream& out, Component& c, ArrayProperty& a, i32 idx) {
	out.add("lumscript_", c.id, "_", a.id, "_count_", idx);
}

void appendArrayItemWrapperName(OutputStream& out, Component& c, ArrayProperty& a, i32 idx) {
	out.add("lumscript_", c.id, "_", a.id, "_item_", idx);
}

void appendArrayChildWrapperName(OutputStream& out, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	out.add("lumscript_", c.id, "_", a.id, "_", is_setter ? p.setter_name : p.getter_name, "_", idx);
}

void appendModuleWrapperName(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("lumscript_", m.id, "_", functionScriptName(f), "_", idx);
}

void serializeLumScriptModuleWrapper(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("static void ");
	appendModuleWrapperName(out, m, f, idx);
	L("(ls_runtime* runtime, ls_call_frame frame) {");
	L("LS_ARG(frame, ", m.name, "*, module);");
	forEachArg(f.args, [&](const Arg& arg, bool) { emitArgRead(out, arg); });

	if (!equal(f.return_type, "void")) out.add("auto ret = ");
	out.add("module->", f.name, "(");
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendArgExpression(out, arg);
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

void serializeLumScriptArrayCountWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static void ");
	appendArrayCountWrapperName(out, c, a, idx);
	L("(ls_runtime* runtime, ls_call_frame frame) {");
	L("LS_COMPONENT_ARG(frame, ", m.name, "*, entity, module);");
	L("const i32 count = module->get", a.name, "Count(EntityRef(entity_index));");
	L("LS_RESULT(frame, count);");
	L("}" OUT_ENDL);
}

void serializeLumScriptArrayItemWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static void ");
	appendArrayItemWrapperName(out, c, a, idx);
	L("(ls_runtime* runtime, ls_call_frame frame) {");
	L("LS_COMPONENT_ARG(frame, ", m.name, "*, entity, module);");
	L("LS_ARG(frame, i32, item_idx);");
	L("const i32 count = module->get", a.name, "Count(EntityRef(entity_index));");
	L("if (item_idx < 0 || item_idx >= count) {");
	L("LS_RESULT(frame, u8(0));");
	L("return;");
	L("}");
	L("LS_RESULT(frame, u8(1));");
	emitResult(out, "entity_index");
	emitResult(out, "item_idx");
	emitResult(out, "module");
	L("}" OUT_ENDL);
}

void serializeLumScriptArrayChildWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendArrayChildWrapperName(out, c, a, p, is_setter, idx);
	L("(ls_runtime* runtime, ls_call_frame frame) {");
	L("LS_ARG(frame, i32, entity_idx);");
	L("LS_ARG(frame, i32, item_idx);");
	L("LS_ARG(frame, ", m.name, "*, module);");
	i32 src_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (src_idx >= 2) emitArgRead(out, arg);
		++src_idx;
	});

	if (!is_setter) out.add("auto ret = ");
	out.add("module->", is_setter ? p.setter_name : p.getter_name, "(");
	out.add("EntityRef(entity_idx), item_idx");
	src_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (src_idx >= 2) {
			out.add(", ");
			appendArgExpression(out, arg);
		}
		++src_idx;
	});
	L(");");
	if (!is_setter) appendReturnValue(out, p.type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

void emitGeneratedHeader(OutputStream& out, MetaData& data) {
	out.add("// Generated by meta.cpp" OUT_ENDL OUT_ENDL);
	out.add("#pragma once" OUT_ENDL OUT_ENDL);
	out.add("#include \"lumscript/capi.h\"" OUT_ENDL);
	out.add("#include <string.h>" OUT_ENDL);
	out.add("#include \"core/stream.h\"" OUT_ENDL);
	out.add("#include \"engine/reflection.h\"" OUT_ENDL);
	out.add("#include \"engine/world.h\"" OUT_ENDL);
	out.add(OUT_ENDL);
	out.add("#define LS_ENTITY_ARG(frame, name, world) \\" OUT_ENDL);
	out.add("\tLS_ARG(frame, i32, name##_index); \\" OUT_ENDL);
	out.add("\tLS_ARG(frame, World*, world)" OUT_ENDL);
	out.add("#define LS_COMPONENT_ARG(frame, type, name, context) \\" OUT_ENDL);
	out.add("\tLS_ARG(frame, i32, name##_index); \\" OUT_ENDL);
	out.add("\tLS_ARG(frame, type, context)" OUT_ENDL OUT_ENDL);
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

void emitGeneratedWorldModuleAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		Component& first_component = m.components[0];
		out.add("static void lumscript_world_", m.id);
		L("(ls_runtime* runtime, ls_call_frame frame) {");
		L("LS_ARG(frame, World*, world);");
		L("IModule* module = world->getModule(reflection::getComponentType(\"", first_component.id, "\"));");
			L("if (!module) { LS_RESULT(frame, u8(0)); return; }");
			L("LS_RESULT(frame, u8(1));");
		emitResult(out, "module");
		L("}" OUT_ENDL);
	}
}

void emitGeneratedComponentEntityAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("static void lumscript_entity_", c.id, "(ls_runtime* runtime, ls_call_frame frame) {");
			L("LS_ENTITY_ARG(frame, entity, world);");
			L("const ComponentType component_type = reflection::getComponentType(\"", c.id, "\");");
			L("IModule* module = world->getModule(component_type);");
			L("if (!module || !world->hasComponent(EntityRef(entity_index), component_type)) {");
			L("LS_RESULT(frame, u8(0));");
			L("return;");
			L("}");
			L("LS_RESULT(frame, u8(1));");
			emitResult(out, "entity_index");
			emitResult(out, "module");
			L("}" OUT_ENDL);
		}
	}
}

void emitGeneratedModuleWrappers(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Module& m : data.modules) {
		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			serializeLumScriptModuleWrapper(out, m, f, *wrapper_idx);
			++*wrapper_idx;
		}
	}
}

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
				out.add("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", functionScriptName(f), "\")}, &");
				appendWrapperName(out, c, f, *wrapper_idx);
				L(");");
				++*wrapper_idx;
			}
			for (Property& p : c.properties) {
				if (isSupportedLumScriptPropertyGetter(p)) {
					out.add("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", p.getter_name, "\")}, &");
					appendPropertyWrapperName(out, c, p, false, *wrapper_idx);
					L(");");
					++*wrapper_idx;
				}
				if (isSupportedLumScriptPropertySetter(p)) {
					out.add("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", p.setter_name, "\")}, &");
					appendPropertyWrapperName(out, c, p, true, *wrapper_idx);
					L(");");
					++*wrapper_idx;
				}
			}
			for (ArrayProperty& a : c.arrays) {
				L("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", a.id, "Count\")}, &");
				appendArrayCountWrapperName(out, c, a, *wrapper_idx);
				L(");");
				++*wrapper_idx;
				L("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", a.id, "\")}, &");
				appendArrayItemWrapperName(out, c, a, *wrapper_idx);
				L(");");
				++*wrapper_idx;
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p)) {
						out.add("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", p.getter_name, "\")}, &");
						appendArrayChildWrapperName(out, c, a, p, false, *wrapper_idx);
						L(");");
						++*wrapper_idx;
					}
					if (isSupportedLumScriptArrayChildSetter(p)) {
						out.add("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", p.setter_name, "\")}, &");
						appendArrayChildWrapperName(out, c, a, p, true, *wrapper_idx);
						L(");");
						++*wrapper_idx;
					}
				}
			}
		}
	}
}

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

void appendLumScriptDeclArgType(OutputStream& out, const Arg& arg) {
	if (isLumScriptStringArg(arg)) {
		out.add("string");
		return;
	}
	appendLumScriptDeclType(out, arg.type);
}

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

void appendLumScriptDeclArgName(OutputStream& out, StringView name, i32 idx) {
	if (name.size() > 0) {
		out.add(name);
		return;
	}
	out.add("arg", idx + 1);
}

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

void serializeCoreImports(MetaData& data) {
	OutputStream out;

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
		L("}" OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendLowercase(path, e.name);
		path.append(".lum");
		writeFile(path, out);
	};

	for (Enum& e : data.enums) output_enum(e);
	for (Module& m : data.modules) {
		for (Enum& e : m.enums) output_enum(e);
	}

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
		L("}" OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendLowercase(path, s.name);
		path.append(".lum");
		writeFile(path, out);
	};

	for (Struct& s : data.structs) output_struct(s);

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

	L("static void registerGeneratedEngineImport(HashMap<NativeFunctionKey, ls_native_fn, NativeFunctionKeyHash>& functions) {");
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", c.id, "\")}, &lumscript_entity_", c.id, ");");
		}
	}
	wrapper_idx = 0;
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;

		bool any_function = false;
		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			out.add("functions.insert({StringView(\"core:", m.id, "\"), StringView(\"", functionScriptName(f), "\")}, &");
			appendModuleWrapperName(out, m, f, wrapper_idx);
			L(");");
			++wrapper_idx;
			any_function = true;
		}
		if (any_function) {
			L("functions.insert({StringView(\"core:", m.id, "\"), StringView(\"", m.id, "\")}, &lumscript_world_", m.id, ");");
		}
	}
	emitGeneratedComponentImportRegistrations(out, data, &wrapper_idx);

	L("}" OUT_ENDL);
	L("} // namespace Lumix::LumScript::generated");
	L("#undef LS_ENTITY_ARG");
	L("#undef LS_COMPONENT_ARG");
	formatCPP(out);
	writeFile("src/lumscript/lumscript_capi.gen.h", out);

	serializeCoreImports(data);
	g_meta_data = nullptr;
}

} // anonymous namespace

META_PLUGIN(serializeLumScriptMeta)
