#include "meta.h"
#include <stdio.h>

#define OUT_ENDL "\r\n"
#define L(...) out.add(__VA_ARGS__, OUT_ENDL)

namespace {

enum class EvoxType { UNKNOWN, VOID_T, BOOL_T, U8_T, I32_T, F32_T, VEC2_T, VEC3_T, DVEC3_T, VEC4_T, COLOR_T, QUAT_T, ENTITY_T, ENUM_T, STRUCT_T, OBJECT_T, PATH_T, STRING_T };

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

StringView objectBaseType(StringView type) {
	while (type.size() > 0 && (type[type.size() - 1] == '*' || type[type.size() - 1] == '&')) --type.end;
	return type;
}

const Object* findObjectByTypeName(StringView type) {
	if (!g_meta_data) return nullptr;
	type = objectBaseType(type);
	for (Object& o : g_meta_data->objects) {
		if (equal(type, o.name) || equal(type, o.full)) return &o;
	}
	return nullptr;
}

bool isObjectPointerType(StringView type) {
	return type.size() > 0 && type[type.size() - 1] == '*';
}

bool isReferenceType(StringView type) {
	return type.size() > 0 && type[type.size() - 1] == '&';
}

bool isSpanType(StringView type) {
	return type.size() > 6 && type[0] == 'S' && type[1] == 'p' && type[2] == 'a' && type[3] == 'n' && type[4] == '<' && type[type.size() - 1] == '>';
}

StringView spanElementType(StringView type) {
	StringView element{type.begin + 5, type.end - 1};
	while (element.size() > 0 && (*element.begin == ' ' || *element.begin == '\t')) ++element.begin;
	while (element.size() > 0 && (element[element.size() - 1] == ' ' || element[element.size() - 1] == '\t')) --element.end;
	return element;
}

StringView spanElementBaseType(StringView type) {
	StringView element = spanElementType(type);
	if (element.size() > 6 && element[0] == 'c' && element[1] == 'o' && element[2] == 'n' && element[3] == 's' && element[4] == 't' &&
		(element[5] == ' ' || element[5] == '\t')) {
		element.begin += 6;
	}
	return element;
}

EvoxType getEvoxType(StringView type) {
	if (equal(type, "void")) return EvoxType::VOID_T;
	if (equal(type, "bool")) return EvoxType::BOOL_T;
	if (equal(type, "u8")) return EvoxType::U8_T;
	if (equal(type, "i32") || equal(type, "int") || equal(type, "u32")) return EvoxType::I32_T;
	if (equal(type, "float")) return EvoxType::F32_T;
	if (equal(type, "Vec2")) return EvoxType::VEC2_T;
	if (equal(type, "Vec3")) return EvoxType::VEC3_T;
	if (equal(type, "DVec3")) return EvoxType::DVEC3_T;
	if (equal(type, "Vec4")) return EvoxType::VEC4_T;
	if (equal(type, "Color")) return EvoxType::COLOR_T;
	if (equal(type, "Quat")) return EvoxType::QUAT_T;
	if (equal(type, "EntityRef") || equal(type, "EntityPtr")) return EvoxType::ENTITY_T;
	if (equal(type, "Path")) return EvoxType::PATH_T;
	if (equal(type, "StringView")) return EvoxType::STRING_T;
	if (findEnumByTypeName(type)) return EvoxType::ENUM_T;
	if (findStructByTypeName(type)) return EvoxType::STRUCT_T;
	if (findObjectByTypeName(type)) return EvoxType::OBJECT_T;
	return EvoxType::UNKNOWN;
}

bool isSupportedEvoxType(StringView type);

bool isSupportedEvoxType(StringView type, EvoxType t) {
	if (isSpanType(type)) return isSupportedEvoxType(spanElementBaseType(type));
	if (t == EvoxType::UNKNOWN) return false;
	if (t != EvoxType::STRUCT_T) return true;
	const Struct* s = findStructByTypeName(type);
	if (!s) return false;
	for (const StructVar& v : s->vars) {
		const EvoxType field_type = getEvoxType(v.type);
		if (field_type == EvoxType::UNKNOWN) return false;
		if (field_type == EvoxType::PATH_T || field_type == EvoxType::OBJECT_T) return false;
		if (field_type == EvoxType::STRUCT_T && !isSupportedEvoxType(v.type, field_type)) return false;
	}
	return true;
}

bool isSupportedEvoxType(StringView type) {
	if (isSpanType(type)) return isSupportedEvoxType(spanElementBaseType(type));
	return isSupportedEvoxType(type, getEvoxType(type));
}

bool isExternCompatibleEvoxType(StringView type) {
	if (isSpanType(type)) return isExternCompatibleEvoxType(spanElementBaseType(type));
	const EvoxType evox_type = getEvoxType(type);
	// StringView is represented as []const u8 in Evox, so it is ABI-compatible
	// with fields in an extern struct.
	if (evox_type == EvoxType::ENTITY_T || evox_type == EvoxType::PATH_T || evox_type == EvoxType::OBJECT_T || evox_type == EvoxType::UNKNOWN) {
		return false;
	}
	if (evox_type != EvoxType::STRUCT_T) return true;
	const Struct* s = findStructByTypeName(type);
	if (!s) return false;
	for (const StructVar& v : s->vars) {
		if (!isExternCompatibleEvoxType(v.type)) return false;
	}
	return true;
}

bool isEvoxStringArg(const Arg& arg) {
	return arg.is_const && arg.is_ptr && equal(arg.type, "char");
}

bool isEvoxPathArg(const Arg& arg) {
	return arg.is_const && arg.is_ref && equal(arg.type, "Path");
}

bool isEvoxEntityType(StringView type) {
	return getEvoxType(type) == EvoxType::ENTITY_T;
}

bool isSupportedEvoxFunctionArg(const Arg& arg) {
	if (isEvoxStringArg(arg)) return true;
	if (isEvoxPathArg(arg)) return true;
	if (isSpanType(arg.type)) return isExternCompatibleEvoxType(arg.type);
	const EvoxType type = getEvoxType(arg.type);
	if (type == EvoxType::OBJECT_T) return arg.is_ptr || arg.is_ref;
	if (arg.is_ptr) return false;
	if (arg.is_ref && !arg.is_const) return false;
	return type != EvoxType::UNKNOWN && type != EvoxType::ENUM_T && type != EvoxType::PATH_T;
}

bool hasUnsupportedEvoxFunctionArg(Function& f) {
	bool unsupported = false;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedEvoxFunctionArg(arg)) unsupported = true;
	});
	return unsupported;
}

StringView functionScriptName(Function& f) {
	return f.attributes.alias.size() > 0 ? f.attributes.alias : f.name;
}

void logUnsupportedEvoxFunctionArgs(const char* scope, StringView owner, Function& f) {
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedEvoxFunctionArg(arg)) {
			logInfo("Evox: skipped ", scope, " ", owner, ".", functionScriptName(f), " because arg ", arg.name, " of type ", arg.type, " is not supported");
		}
	});
}

void appendValueExpression(OutputStream& out, StringView type, StringView name) {
	switch (getEvoxType(type)) {
		case EvoxType::ENTITY_T:
			out.add(equal(type, "EntityRef") ? "EntityRef(" : "EntityPtr(", name, ".index)");
			break;
		case EvoxType::COLOR_T:
			out.add("Color(u8(", name, "_r), u8(", name, "_g), u8(", name, "_b), u8(", name, "_a))");
			break;
		case EvoxType::ENUM_T: {
			const Enum* e = findEnumByTypeName(type);
			out.add("(", e ? e->full : type, ")", name, "_value");
			break;
		}
		case EvoxType::STRING_T:
			out.add("StringView{", name, ".begin, (u64)", name, ".length}");
			break;
		default: out.add(name); break;
	}
}

void emitFrameValueRead(OutputStream& out, StringView type, StringView name) {
	switch (getEvoxType(type)) {
		case EvoxType::ENTITY_T:
			L("EX_ARG(frame, ExEntity, ", name, ");");
			break;
		case EvoxType::COLOR_T:
			L("EX_ARG(frame, i32, ", name, "_r);");
			L("EX_ARG(frame, i32, ", name, "_g);");
			L("EX_ARG(frame, i32, ", name, "_b);");
			L("EX_ARG(frame, i32, ", name, "_a);");
			break;
		case EvoxType::ENUM_T:
			L("EX_ARG(frame, i32, ", name, "_value);");
			break;
		case EvoxType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			if (!s) break;
			out.add(s->full);
			L(" ", name, "{};");
			for (const StructVar& v : s->vars) {
				if (getEvoxType(v.type) == EvoxType::VOID_T) continue;
				StaticString<256> field_name(name, "_", v.name);
				const StringView field_name_view{field_name.buffer, field_name.buffer + field_name.length};
				emitFrameValueRead(out, v.type, field_name_view);
				out.add(name, ".", v.name, " = ");
				appendValueExpression(out, v.type, field_name_view);
				L(";");
			}
			break;
		}
		case EvoxType::OBJECT_T: {
			const Object* o = findObjectByTypeName(type);
			out.add("EX_ARG(frame, ", o ? o->full : objectBaseType(type), "*, ", name, ");" OUT_ENDL);
			break;
		}
		case EvoxType::STRING_T:
			L("EX_STRING_ARG(frame, ", name, ");");
			break;
		default:
			out.add("EX_ARG(frame, ", type, ", ", name, ");" OUT_ENDL);
			break;
	}
}

void emitArgRead(OutputStream& out, const Arg& arg) {
	if (isSpanType(arg.type)) {
		L("EX_ARG(frame, ex_slice, ", arg.name, "_slice);");
		out.add(arg.type, " ", arg.name, "(reinterpret_cast<", spanElementType(arg.type), "*>(", arg.name, "_slice.data), ", arg.name, "_slice.length);" OUT_ENDL);
	}
	else if (isEvoxStringArg(arg)) {
		L("EX_STRING_ARG(frame, ", arg.name, ");");
		L("char evox_string_arg_", arg.name, "[128];");
		L("copyString(Span(evox_string_arg_", arg.name, "), StringView{", arg.name, ".begin, (u64)", arg.name, ".length});");
	}
	else if (isEvoxPathArg(arg)) L("EX_STRING_ARG(frame, ", arg.name, ");");
	else if (getEvoxType(arg.type) == EvoxType::STRING_T) L("EX_STRING_ARG(frame, ", arg.name, ");");
	else emitFrameValueRead(out, arg.type, arg.name);
}

void appendArgExpression(OutputStream& out, const Arg& arg) {
	if (isEvoxStringArg(arg)) out.add("evox_string_arg_", arg.name);
	else if (isEvoxPathArg(arg)) out.add("Path(StringView{", arg.name, ".begin, (u64)", arg.name, ".length})");
	else if (getEvoxType(arg.type) == EvoxType::STRING_T) out.add("StringView{", arg.name, ".begin, (u64)", arg.name, ".length}");
	else if (getEvoxType(arg.type) == EvoxType::OBJECT_T && arg.is_ref) out.add("*", arg.name);
	else appendValueExpression(out, arg.type, arg.name);
}

void emitResult(OutputStream& out, const char* value) {
	L("EX_RESULT(frame, ", value, ");");
}

void appendReturnValue(OutputStream& out, StringView type, const char* value, const char* world_expr = nullptr) {
	if (isSpanType(type)) {
		L("ex_slice result{const_cast<u8*>(reinterpret_cast<const u8*>((", value, ").begin())), (i64)(", value, ").size()};");
		L("EX_RESULT(frame, result);");
		return;
	}
	const EvoxType evox_type = getEvoxType(type);
	if (evox_type == EvoxType::VOID_T) return;
	switch (evox_type) {
		case EvoxType::BOOL_T: emitResult(out, value); break;
		case EvoxType::U8_T: emitResult(out, value); break;
		case EvoxType::I32_T: {
			StaticString<256> v("(i32)", value);
			emitResult(out, v);
			break;
		}
		case EvoxType::F32_T: emitResult(out, value); break;
		case EvoxType::VEC2_T: emitResult(out, value); break;
		case EvoxType::VEC3_T: emitResult(out, value); break;
		case EvoxType::DVEC3_T: emitResult(out, value); break;
		case EvoxType::VEC4_T: emitResult(out, value); break;
		case EvoxType::COLOR_T: {
			StaticString<256> r("(i32)", value, ".r"); emitResult(out, r);
			StaticString<256> g("(i32)", value, ".g"); emitResult(out, g);
			StaticString<256> b("(i32)", value, ".b"); emitResult(out, b);
			StaticString<256> a("(i32)", value, ".a"); emitResult(out, a);
			break;
		}
		case EvoxType::QUAT_T: emitResult(out, value); break;
		case EvoxType::ENTITY_T: {
			StaticString<256> index(value, ".index");
			L("EX_RESULT(frame, ExEntity(", index.buffer, ", ", world_expr ? world_expr : "nullptr", "));");
			break;
		}
		case EvoxType::ENUM_T: {
			StaticString<256> v("(i32)", value);
			emitResult(out, v);
			break;
		}
		case EvoxType::PATH_T:
			L("ex_result_string(runtime, &frame, ex_string_view{", value, ".c_str(), (i64)", value, ".length()});");
			break;
		case EvoxType::STRING_T:
			L("ex_result_string(runtime, &frame, ex_string_view{", value, ".data, (i64)", value, ".length});");
			break;
		case EvoxType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			if (!s) break;
			for (const StructVar& v : s->vars) {
				if (getEvoxType(v.type) == EvoxType::VOID_T) continue;
				StaticString<128> field_value(value, ".", v.name);
				appendReturnValue(out, v.type, field_value.buffer, world_expr);
			}
			break;
		}
		case EvoxType::OBJECT_T:
			if (isObjectPointerType(type)) {
				StaticString<256> present("u8(", value, " != nullptr)");
				emitResult(out, present);
				emitResult(out, value);
			}
			else {
				StaticString<256> ptr("&", value);
				emitResult(out, ptr);
			}
			break;
	}
}

bool isSupportedEvoxFunction(Function& f) {
	const EvoxType ret_type = getEvoxType(f.return_type);
	if (!isSupportedEvoxType(f.return_type, ret_type)) return false;
	if (ret_type == EvoxType::OBJECT_T && !isObjectPointerType(f.return_type) && !isReferenceType(f.return_type)) return false;
	bool supported = true;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedEvoxFunctionArg(arg)) supported = false;
	});
	return supported;
}

bool isSupportedEvoxPropertyArg(const Arg& arg) {
	if (isEvoxPathArg(arg)) return true;
	if (isEvoxStringArg(arg)) return true;
	if (isSpanType(arg.type)) return isExternCompatibleEvoxType(arg.type);
	if (getEvoxType(arg.type) == EvoxType::OBJECT_T) return arg.is_ptr || arg.is_ref;
	if (arg.is_ptr) return false;
	if (arg.is_ref && !arg.is_const) return false;
	return isSupportedEvoxType(arg.type);
}

bool isSupportedEvoxPropertyGetter(Property& p) {
	if (p.getter_name.size() == 0) return false;
	const EvoxType type = getEvoxType(p.type);
	if (!isSupportedEvoxType(p.type, type)) return false;
	bool supported = true;
	forEachArg(p.getter_args, [&](const Arg& arg, bool) {
		if (!isSupportedEvoxPropertyArg(arg)) supported = false;
	});
	return supported;
}

bool isSupportedEvoxPropertySetter(Property& p) {
	if (p.setter_name.size() == 0) return false;
	bool supported = true;
	forEachArg(p.setter_args, [&](const Arg& arg, bool) {
		if (!isSupportedEvoxPropertyArg(arg)) supported = false;
	});
	return supported;
}

bool hasArrayAccessorPrefixArgs(StringView args) {
	i32 count = 0;
	bool ok = true;
	forEachArg(args, [&](const Arg& arg, bool) {
		if (count == 0 && !isEvoxEntityType(arg.type)) ok = false;
		if (count == 1 && !(equal(arg.type, "i32") || equal(arg.type, "int") || equal(arg.type, "u32"))) ok = false;
		++count;
	});
	return ok && count >= 2;
}

bool isSupportedEvoxArrayChildGetter(Property& p) {
	if (p.getter_name.size() == 0) return false;
	const EvoxType type = getEvoxType(p.type);
	if (!isSupportedEvoxType(p.type, type)) return false;
	if (!hasArrayAccessorPrefixArgs(p.getter_args)) return false;
	i32 idx = 0;
	bool supported = true;
	forEachArg(p.getter_args, [&](const Arg& arg, bool) {
		if (idx >= 2 && !isSupportedEvoxPropertyArg(arg)) supported = false;
		++idx;
	});
	return supported;
}

bool isSupportedEvoxArrayChildSetter(Property& p) {
	if (p.setter_name.size() == 0) return false;
	if (!hasArrayAccessorPrefixArgs(p.setter_args)) return false;
	i32 idx = 0;
	bool supported = true;
	forEachArg(p.setter_args, [&](const Arg& arg, bool) {
		if (idx >= 2 && !isSupportedEvoxPropertyArg(arg)) supported = false;
		++idx;
	});
	return supported;
}

void appendWrapperName(OutputStream& out, Component& c, Function& f, i32 idx) {
	out.add("evox_", c.id, "_", functionScriptName(f), "_", idx);
}

void appendPropertyWrapperName(OutputStream& out, Component& c, Property& p, bool is_setter, i32 idx) {
	out.add("evox_", c.id, "_", is_setter ? p.setter_name : p.getter_name, "_", idx);
}

void serializeEvoxWrapper(OutputStream& out, Module& m, Component& c, Function& f, i32 idx) {
	out.add("static void ");
	appendWrapperName(out, c, f, idx);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (is_first) {
			L("EX_ARG(frame, ExComponent, ", arg.name, ");");
			L(m.name, "* module = static_cast<", m.name, "*>(", arg.name, ".module);");
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

void serializeEvoxPropertyWrapper(OutputStream& out, Module& m, Component& c, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	forEachArg(accessor_args, [&](const Arg& arg, bool is_first) {
		if (is_first) {
			L("EX_ARG(frame, ExComponent, ", arg.name, ");");
			L(m.name, "* module = static_cast<", m.name, "*>(", arg.name, ".module);");
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
	out.add("evox_", c.id, "_", a.id, "_count_", idx);
}

void appendArrayItemWrapperName(OutputStream& out, Component& c, ArrayProperty& a, i32 idx) {
	out.add("evox_", c.id, "_", a.id, "_item_", idx);
}

void appendArrayChildWrapperName(OutputStream& out, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	out.add("evox_", c.id, "_", a.id, "_", is_setter ? p.setter_name : p.getter_name, "_", idx);
}

void appendModuleWrapperName(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("evox_", m.id, "_", functionScriptName(f), "_", idx);
}

void appendObjectWrapperName(OutputStream& out, Object& o, Function& f, i32 idx) {
	out.add("evox_object_", o.name, "_", functionScriptName(f), "_", idx);
}

void appendSpanIteratorCountName(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("evox_", m.id, "_", functionScriptName(f), "_count_", idx);
}

void appendSpanIteratorGetName(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("evox_", m.id, "_", functionScriptName(f), "_get_", idx);
}

void serializeEvoxObjectWrapper(OutputStream& out, Object& o, Function& f, i32 idx) {
	out.add("static void ");
	appendObjectWrapperName(out, o, f, idx);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", o.full, "*, object);");
	forEachArg(f.args, [&](const Arg& arg, bool) { emitArgRead(out, arg); });
	if (!equal(f.return_type, "void")) out.add("auto ret = ");
	out.add("object->", f.name, "(");
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendArgExpression(out, arg);
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret");
	L("}" OUT_ENDL);
}

void serializeEvoxSpanIteratorWrappers(OutputStream& out, Module& m, Function& f, i32 idx) {
	const StringView element = spanElementBaseType(f.return_type);
	out.add("static void "); appendSpanIteratorCountName(out, m, f, idx); L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", m.name, "*, module);");
	forEachArg(f.args, [&](const Arg& arg, bool) { emitArgRead(out, arg); });
	L("const auto ret = module->", f.name, "(");
	forEachArg(f.args, [&](const Arg& arg, bool first) { if (!first) out.add(", "); appendArgExpression(out, arg); });
	L(");");
	L("EX_RESULT(frame, (i32)ret.size());");
	L("}" OUT_ENDL);
	out.add("static void "); appendSpanIteratorGetName(out, m, f, idx + 1); L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", m.name, "*, module);");
	forEachArg(f.args, [&](const Arg& arg, bool) { emitArgRead(out, arg); });
	L("EX_ARG(frame, i32, index);");
	L("const auto ret = module->", f.name, "(");
	forEachArg(f.args, [&](const Arg& arg, bool first) { if (!first) out.add(", "); appendArgExpression(out, arg); });
	L(");");
	L("ASSERT(index >= 0 && index < (i32)ret.size());");
	L("const ", element, "& value = ret[index];");
	appendReturnValue(out, element, "value", "&module->getWorld()");
	L("}" OUT_ENDL);
}

void serializeEvoxModuleWrapper(OutputStream& out, Module& m, Function& f, i32 idx) {
	if (isSpanType(f.return_type)) return;
	out.add("static void ");
	appendModuleWrapperName(out, m, f, idx);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", m.name, "*, module);");
	forEachArg(f.args, [&](const Arg& arg, bool) { emitArgRead(out, arg); });

	if (!equal(f.return_type, "void")) out.add(isReferenceType(f.return_type) ? "auto& ret = " : "auto ret = ");
	out.add("module->", f.name, "(");
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendArgExpression(out, arg);
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

void serializeEvoxArrayCountWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static void ");
	appendArrayCountWrapperName(out, c, a, idx);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ExComponent, component);");
	L(m.name, "* module = static_cast<", m.name, "*>(component.module);");
	L("const i32 count = module->get", a.name, "Count(EntityRef(component.index));");
	L("EX_RESULT(frame, count);");
	L("}" OUT_ENDL);
}

void serializeEvoxArrayItemWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static void ");
	appendArrayItemWrapperName(out, c, a, idx);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ExComponent, component);");
	L(m.name, "* module = static_cast<", m.name, "*>(component.module);");
	L("EX_ARG(frame, i32, item_idx);");
	L("const i32 count = module->get", a.name, "Count(EntityRef(component.index));");
	L("if (item_idx < 0 || item_idx >= count) {");
	L("EX_RESULT(frame, u8(0));");
	L("EX_RESULT(frame, i32(0));");
	L("EX_RESULT(frame, i32(0));");
	L("EX_RESULT(frame, (void*)nullptr);");
	L("return;");
	L("}");
	L("EX_RESULT(frame, u8(1));");
	emitResult(out, "component.index");
	emitResult(out, "item_idx");
	emitResult(out, "module");
	L("}" OUT_ENDL);
}

void serializeEvoxArrayChildWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendArrayChildWrapperName(out, c, a, p, is_setter, idx);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, i32, entity_idx);");
	L("EX_ARG(frame, i32, item_idx);");
	L("EX_ARG(frame, ", m.name, "*, module);");
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
	out.add("#include \"evox/capi.h\"" OUT_ENDL);
	out.add("#include <string.h>" OUT_ENDL);
	out.add("#include \"core/stream.h\"" OUT_ENDL);
	out.add("#include \"engine/reflection.h\"" OUT_ENDL);
	out.add("#include \"engine/world.h\"" OUT_ENDL);
	out.add(OUT_ENDL);

	out.add("struct ExEntity { i32 index; u32 padding; Lumix::World* world; ExEntity() = default; explicit ExEntity(i32 index, Lumix::World* world) : index(index), padding(0), world(world) {} };" OUT_ENDL);
	out.add("struct ExComponent { i32 index; u32 padding; void* module; ExComponent() = default; explicit ExComponent(i32 index, void* module) : index(index), padding(0), module(module) {} };" OUT_ENDL);
	out.add("static_assert(offsetof(ExEntity, world) == 8);" OUT_ENDL);
	out.add("static_assert(offsetof(ExComponent, module) == 8);" OUT_ENDL);
	out.add("static_assert(sizeof(ExEntity) == 16);" OUT_ENDL);
	out.add("static_assert(sizeof(ExComponent) == 16);" OUT_ENDL OUT_ENDL);

	StringView included_paths[512];
	i32 included_path_count = 0;
	auto emitInclude = [&](const char* filename) {
		StringView include_path = makeStringView(filename);
		for (i32 i = 0; i < included_path_count; ++i) {
			if (equal(included_paths[i], include_path)) return;
		}
		included_paths[included_path_count++] = include_path;
		// Generated header lives under src/evox, so plugin includes need one extra "..".
		if (startsWith(include_path, "plugins/")) {
			out.add("#include \"../", include_path, "\"" OUT_ENDL);
		}
		else {
			if (startsWith(include_path, "src/")) include_path = withoutPrefix(include_path, 4);
			out.add("#include \"", include_path, "\"" OUT_ENDL);
		}
	};
	for (Module& m : data.modules) emitInclude(m.filename);
	for (Object& o : data.objects) emitInclude(o.filename);
	out.add(OUT_ENDL);
}

void emitGeneratedWorldModuleAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		Component& first_component = m.components[0];
		out.add("static void evox_world_", m.id);
		L("(ex_runtime* runtime, ex_call_frame frame) {");
		L("EX_ARG(frame, World*, world);");
		L("IModule* module = world->getModule(reflection::getComponentType(\"", first_component.id, "\"));");
			L("if (!module) {");
			L("EX_RESULT(frame, u8(0));");
			L("EX_RESULT(frame, (void*)nullptr);");
			L("return;");
			L("}");
			L("EX_RESULT(frame, u8(1));");
		emitResult(out, "module");
		L("}" OUT_ENDL);
	}
}

void emitGeneratedComponentEntityAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("static void evox_entity_", c.id, "(ex_runtime* runtime, ex_call_frame frame) {");
			L("EX_ARG(frame, ExEntity, entity);");
			L("World* world = entity.world;");
			L("const ComponentType component_type = reflection::getComponentType(\"", c.id, "\");");
			L("IModule* module = world ? world->getModule(component_type) : nullptr;");
			L("if (!world || entity.index < 0 || !world->hasEntity(EntityRef(entity.index)) || !module || !world->hasComponent(EntityRef(entity.index), component_type)) {");
			L("EX_RESULT(frame, u8(0));");
			L("EX_RESULT(frame, ExComponent(i32(0), (void*)nullptr));");
			L("return;");
			L("}");
			L("EX_RESULT(frame, u8(1));");
			L("EX_RESULT(frame, ExComponent(entity.index, module));");
			L("}" OUT_ENDL);
		}
	}
}

void emitGeneratedComponentCreators(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("static void evox_entity_create", c.name, "(ex_runtime* runtime, ex_call_frame frame) {");
			L("EX_ARG(frame, ExEntity, entity);");
			L("World* world = entity.world;");
			L("const ComponentType component_type = reflection::getComponentType(\"", c.id, "\");");
			L("IModule* module = world ? world->getModule(component_type) : nullptr;");
			L("if (!world || entity.index < 0 || !world->hasEntity(EntityRef(entity.index)) || !module) {");
			L("EX_RESULT(frame, u8(0));");
			L("EX_RESULT(frame, ExComponent(i32(0), (void*)nullptr));");
			L("return;");
			L("}");
			L("if (!world->hasComponent(EntityRef(entity.index), component_type)) {");
			L("world->createComponent(component_type, EntityRef(entity.index));");
			L("}");
			L("if (!world->hasComponent(EntityRef(entity.index), component_type)) {");
			L("EX_RESULT(frame, u8(0));");
			L("EX_RESULT(frame, ExComponent(i32(0), (void*)nullptr));");
			L("return;");
			L("}");
			L("EX_RESULT(frame, u8(1));");
			L("EX_RESULT(frame, ExComponent(entity.index, module));");
			L("}" OUT_ENDL);
		}
	}
}

void emitGeneratedModuleWrappers(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Module& m : data.modules) {
		for (Function& f : m.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			if (isSpanType(f.return_type)) {
				serializeEvoxSpanIteratorWrappers(out, m, f, *wrapper_idx);
				*wrapper_idx += 2;
			}
			else {
				serializeEvoxModuleWrapper(out, m, f, *wrapper_idx);
				++*wrapper_idx;
			}
		}
	}
}

void emitGeneratedObjectWrappers(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Object& o : data.objects) {
		for (Function& f : o.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			serializeEvoxObjectWrapper(out, o, f, *wrapper_idx);
			++*wrapper_idx;
		}
	}
}

void emitGeneratedComponentWrappers(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			for (Function& f : c.functions) {
				if (!isSupportedEvoxFunction(f)) continue;
				serializeEvoxWrapper(out, m, c, f, *wrapper_idx);
				++*wrapper_idx;
			}
			for (Property& p : c.properties) {
				if (isSupportedEvoxPropertyGetter(p)) {
					serializeEvoxPropertyWrapper(out, m, c, p, false, *wrapper_idx);
					++*wrapper_idx;
				}
				if (isSupportedEvoxPropertySetter(p)) {
					serializeEvoxPropertyWrapper(out, m, c, p, true, *wrapper_idx);
					++*wrapper_idx;
				}
			}
			for (ArrayProperty& a : c.arrays) {
				serializeEvoxArrayCountWrapper(out, m, c, a, *wrapper_idx);
				++*wrapper_idx;
				serializeEvoxArrayItemWrapper(out, m, c, a, *wrapper_idx);
				++*wrapper_idx;
				for (Property& p : a.children) {
					if (isSupportedEvoxArrayChildGetter(p)) {
						serializeEvoxArrayChildWrapper(out, m, c, a, p, false, *wrapper_idx);
						++*wrapper_idx;
					}
					if (isSupportedEvoxArrayChildSetter(p)) {
						serializeEvoxArrayChildWrapper(out, m, c, a, p, true, *wrapper_idx);
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
				if (isSupportedEvoxFunction(f)) {
					has_supported_function = true;
					break;
				}
			}
			bool has_supported_property = false;
			for (Property& p : c.properties) {
				if (isSupportedEvoxPropertyGetter(p) || isSupportedEvoxPropertySetter(p)) {
					has_supported_property = true;
					break;
				}
			}
			bool has_supported_array = false;
			for (ArrayProperty& a : c.arrays) {
				bool array_supported = false;
				for (Property& p : a.children) {
					if (isSupportedEvoxArrayChildGetter(p) || isSupportedEvoxArrayChildSetter(p)) {
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
				if (!isSupportedEvoxFunction(f)) continue;
				out.add("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", functionScriptName(f), "\")}, &");
				appendWrapperName(out, c, f, *wrapper_idx);
				L(");");
				++*wrapper_idx;
			}
			for (Property& p : c.properties) {
				if (isSupportedEvoxPropertyGetter(p)) {
					out.add("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", p.getter_name, "\")}, &");
					appendPropertyWrapperName(out, c, p, false, *wrapper_idx);
					L(");");
					++*wrapper_idx;
				}
				if (isSupportedEvoxPropertySetter(p)) {
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
					if (isSupportedEvoxArrayChildGetter(p)) {
						out.add("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", p.getter_name, "\")}, &");
						appendArrayChildWrapperName(out, c, a, p, false, *wrapper_idx);
						L(");");
						++*wrapper_idx;
					}
					if (isSupportedEvoxArrayChildSetter(p)) {
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

void emitGeneratedObjectImportRegistrations(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Object& o : data.objects) {
		StaticString<256> unit("core:");
		appendLowercase(unit, o.name);
		for (Function& f : o.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			out.add("functions.insert({StringView(\"", unit.buffer, "\"), StringView(\"", functionScriptName(f), "\")}, &");
			appendObjectWrapperName(out, o, f, *wrapper_idx);
			L(");");
			++*wrapper_idx;
		}
	}
}

void appendEvoxDeclType(OutputStream& out, StringView type) {
	if (isSpanType(type)) {
		out.add("[]");
		if (spanElementType(type).size() >= 6 && spanElementType(type)[0] == 'c' && spanElementType(type)[1] == 'o' && spanElementType(type)[2] == 'n' && spanElementType(type)[3] == 's' && spanElementType(type)[4] == 't' &&
			(spanElementType(type)[5] == ' ' || spanElementType(type)[5] == '\t')) out.add("const ");
		appendEvoxDeclType(out, spanElementBaseType(type));
		return;
	}
	switch (getEvoxType(type)) {
		case EvoxType::VOID_T: out.add("void"); break;
		case EvoxType::BOOL_T: out.add("bool"); break;
		case EvoxType::U8_T: out.add("u8"); break;
		case EvoxType::I32_T: out.add("i32"); break;
		case EvoxType::F32_T: out.add("f32"); break;
		case EvoxType::VEC2_T: out.add("Vec2"); break;
		case EvoxType::VEC3_T: out.add("Vec3"); break;
		case EvoxType::DVEC3_T: out.add("DVec3"); break;
		case EvoxType::VEC4_T: out.add("Vec4"); break;
		case EvoxType::COLOR_T: out.add("Color"); break;
		case EvoxType::QUAT_T: out.add("Quat"); break;
		case EvoxType::ENTITY_T: out.add("Entity"); break;
		case EvoxType::ENUM_T: {
			const Enum* e = findEnumByTypeName(type);
			out.add(e ? e->name : type);
			break;
		}
		case EvoxType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			out.add(s ? s->name : type);
			break;
		}
		case EvoxType::OBJECT_T: {
			const Object* o = findObjectByTypeName(type);
			if (isObjectPointerType(type)) out.add("?");
			out.add(o ? o->name : objectBaseType(type));
			break;
		}
		case EvoxType::PATH_T: out.add("[]const u8"); break;
		case EvoxType::STRING_T: out.add("[]const u8"); break;
		default: out.add("void"); break;
	}
}

void appendEvoxDeclArgType(OutputStream& out, const Arg& arg) {
	if (isEvoxStringArg(arg)) {
		out.add("[]const u8");
		return;
	}
	appendEvoxDeclType(out, arg.type);
}

void appendEvoxImportType(OutputStream& out, StringView type) {
	if (isSpanType(type)) type = spanElementBaseType(type);
	if (equal(type, "World")) {
		out.add("world");
		return;
	}
	switch (getEvoxType(type)) {
		case EvoxType::VEC2_T: out.add("vec2"); break;
		case EvoxType::VEC3_T: out.add("vec3"); break;
		case EvoxType::DVEC3_T: out.add("dvec3"); break;
		case EvoxType::VEC4_T: out.add("vec4"); break;
		case EvoxType::COLOR_T: out.add("color"); break;
		case EvoxType::QUAT_T: out.add("quat"); break;
		case EvoxType::ENTITY_T: out.add("entity"); break;
		case EvoxType::ENUM_T: {
			const Enum* e = findEnumByTypeName(type);
			out.add(e ? e->name : type);
			break;
		}
		case EvoxType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			out.add(s ? s->name : type);
			break;
		}
		case EvoxType::OBJECT_T: {
			const Object* o = findObjectByTypeName(type);
			StaticString<256> name("");
			if (o) appendLowercase(name, o->name);
			out.add(name.buffer);
			break;
		}
		default: out.add(type); break;
	}
}

void appendEvoxDeclArgName(OutputStream& out, StringView name, i32 idx) {
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
		appendEvoxDeclArgName(out, arg.name, arg_idx);
		out.add(" : ");
		if (is_first && isEvoxEntityType(arg.type))
			out.add(c.name);
		else
			appendEvoxDeclArgType(out, arg);
		++arg_idx;
	});
	out.add(") : ");
	if (is_setter)
		out.add("void");
	else
		appendEvoxDeclType(out, p.type);
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
			appendEvoxDeclArgName(out, arg.name, arg_idx);
			out.add(" : ");
			appendEvoxDeclArgType(out, arg);
		}
		++arg_idx;
	});
	out.add(") : ");
	if (is_setter)
		out.add("void");
	else
		appendEvoxDeclType(out, p.type);
	out.add(";" OUT_ENDL);
}

void emitEvoxReferenceEnum(OutputStream& out, Enum& e) {
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
		path.append(".evox");
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
			const EvoxType type = getEvoxType(v.type);
			if (type != EvoxType::VEC2_T && type != EvoxType::VEC3_T && type != EvoxType::DVEC3_T && type != EvoxType::VEC4_T && type != EvoxType::COLOR_T &&
				type != EvoxType::QUAT_T && type != EvoxType::ENTITY_T && type != EvoxType::ENUM_T && type != EvoxType::STRUCT_T) {
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
			appendEvoxImportType(out, v.type);
			out.add("\"" OUT_ENDL);
		}
		L(isExternCompatibleEvoxType(s.name) ? "extern struct " : "struct ", s.name, " {");
		for (StructVar& v : s.vars) {
			out.add("\t", v.name, " : ");
			appendEvoxDeclType(out, v.type);
			L(";");
		}
		L("}" OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendLowercase(path, s.name);
		path.append(".evox");
		writeFile(path, out);
	};

	for (Struct& s : data.structs) output_struct(s);

	for (Object& o : data.objects) {
		bool has_supported_function = false;
		for (Function& f : o.functions) {
			if (isSupportedEvoxFunction(f)) {
				has_supported_function = true;
				break;
			}
		}
		if (!has_supported_function) continue;

		out.length = 0;
		L("// Generated by meta.cpp");
		StaticString<256> object_unit("");
		appendLowercase(object_unit, o.name);
		out.add("// import core:", object_unit.buffer, " as ", o.name, OUT_ENDL OUT_ENDL);

		StringView imported_types[32];
		i32 imported_types_count = 0;
		auto emitImportForType = [&](StringView type) {
			if (isSpanType(type)) type = spanElementBaseType(type);
			const EvoxType import_type = getEvoxType(type);
			if (import_type != EvoxType::VEC2_T && import_type != EvoxType::VEC3_T && import_type != EvoxType::DVEC3_T && import_type != EvoxType::VEC4_T &&
				import_type != EvoxType::COLOR_T && import_type != EvoxType::QUAT_T && import_type != EvoxType::ENTITY_T && import_type != EvoxType::ENUM_T &&
				import_type != EvoxType::STRUCT_T && import_type != EvoxType::OBJECT_T) return;
			const Object* imported_object = findObjectByTypeName(type);
			if (imported_object == &o) return;
			StringView base_type = objectBaseType(type);
			for (i32 i = 0; i < imported_types_count; ++i) {
				if (equal(imported_types[i], base_type)) return;
			}
			imported_types[imported_types_count++] = base_type;
			out.add("import \"core:");
			appendEvoxImportType(out, type);
			out.add("\"" OUT_ENDL);
		};
		for (Function& f : o.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			emitImportForType(f.return_type);
			forEachArg(f.args, [&](const Arg& arg, bool) { emitImportForType(arg.type); });
		}

		L("struct ", o.name, " { ptr : cptr; }" OUT_ENDL);
		for (Function& f : o.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			out.add("extern fn ", functionScriptName(f), "(object : ", o.name);
			i32 arg_idx = 0;
			forEachArg(f.args, [&](const Arg& arg, bool) {
				out.add(", ");
				appendEvoxDeclArgName(out, arg.name, arg_idx++);
				out.add(" : ");
				appendEvoxDeclArgType(out, arg);
			});
			out.add(") : ");
			appendEvoxDeclType(out, f.return_type);
			out.add(";" OUT_ENDL);
		}
		out.add(OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendLowercase(path, o.name);
		path.append(".evox");
		writeFile(path, out);
	}

	for (Module& m : data.modules) {
		bool has_supported_function = false;
		for (Function& f : m.functions) {
			if (isSupportedEvoxFunction(f)) {
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
			if (isSpanType(type)) type = spanElementBaseType(type);
			const EvoxType import_type = getEvoxType(type);
			if (import_type != EvoxType::VEC2_T && import_type != EvoxType::VEC3_T && import_type != EvoxType::DVEC3_T && import_type != EvoxType::VEC4_T &&
				import_type != EvoxType::COLOR_T && import_type != EvoxType::QUAT_T && import_type != EvoxType::ENTITY_T && import_type != EvoxType::ENUM_T &&
				import_type != EvoxType::STRUCT_T && import_type != EvoxType::OBJECT_T && !equal(type, "World")) {
				return;
			}
			if (equal(type, m.name)) return;
			for (i32 i = 0; i < imported_types_count; ++i) {
				if (equal(imported_types[i], type)) return;
			}
			imported_types[imported_types_count++] = type;
			out.add("import \"core:");
			appendEvoxImportType(out, type);
			out.add("\"" OUT_ENDL);
		};
		emitImportForType(StringView{"World", "World" + 5});
		for (Function& f : m.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			emitImportForType(f.return_type);
			forEachArg(f.args, [&](const Arg& arg, bool) { emitImportForType(arg.type); });
		}

		L("struct ", m.name, " { module : cptr; }" OUT_ENDL);
		L("extern fn ", m.id, "(w : World) : ?", m.name, ";");

		for (Function& f : m.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			if (isSpanType(f.return_type)) {
				const StringView element = spanElementBaseType(f.return_type);
				out.add("extern fn ", functionScriptName(f), "Count(module : ", m.name);
				forEachArg(f.args, [&](const Arg& arg, bool) { out.add(", ", arg.name, " : "); appendEvoxDeclArgType(out, arg); });
				out.add(") : i32;" OUT_ENDL);
				out.add("extern fn ", functionScriptName(f), "Get(module : ", m.name);
				forEachArg(f.args, [&](const Arg& arg, bool) { out.add(", ", arg.name, " : "); appendEvoxDeclArgType(out, arg); });
				out.add(", index : i32) : "); appendEvoxDeclType(out, element); out.add(";" OUT_ENDL);
				out.add("struct ", functionScriptName(f), "Iterator { next : fn(iter : *", functionScriptName(f), "Iterator, out : *");
				appendEvoxDeclType(out, element); out.add(") : bool; module : ", m.name);
				forEachArg(f.args, [&](const Arg& arg, bool) { out.add("; ", arg.name, " : "); appendEvoxDeclArgType(out, arg); });
				out.add("; index : i32; }" OUT_ENDL);
				out.add("fn ", functionScriptName(f), "Next(iter : *", functionScriptName(f), "Iterator, out : *");
				appendEvoxDeclType(out, element); out.add(") : bool { iter.index += 1; if iter.index >= ", functionScriptName(f), "Count(iter.module");
				forEachArg(f.args, [&](const Arg& arg, bool) { out.add(", iter.", arg.name); });
				out.add(") { return false; } out.* = ", functionScriptName(f), "Get(iter.module");
				forEachArg(f.args, [&](const Arg& arg, bool) { out.add(", iter.", arg.name); });
				out.add(", iter.index); return true; }" OUT_ENDL);
				out.add("fn ", functionScriptName(f), "(module : ", m.name);
				forEachArg(f.args, [&](const Arg& arg, bool) { out.add(", ", arg.name, " : "); appendEvoxDeclArgType(out, arg); });
				out.add(") : ", functionScriptName(f), "Iterator { return { ", functionScriptName(f), "Next, module");
				forEachArg(f.args, [&](const Arg& arg, bool) { out.add(", ", arg.name); });
				out.add(", -1 }; }" OUT_ENDL);
				continue;
			}
			out.add("extern fn ", functionScriptName(f), "(module : ", m.name);
			forEachArg(f.args, [&](const Arg& arg, bool) {
				out.add(", ", arg.name, " : ");
				appendEvoxDeclArgType(out, arg);
			});
			out.add(") : ");
			appendEvoxDeclType(out, f.return_type);
			out.add(";" OUT_ENDL);
		}
		out.add(OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendLowercase(path, m.id);
		path.append(".evox");
		writeFile(path, out);
	}

	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			bool has_supported_function = false;
			for (Function& f : c.functions) {
				if (!isSupportedEvoxFunction(f)) continue;
				has_supported_function = true;
				break;
			}
			bool has_supported_property = false;
			for (Property& p : c.properties) {
				if (isSupportedEvoxPropertyGetter(p) || isSupportedEvoxPropertySetter(p)) {
					has_supported_property = true;
					break;
				}
			}
			bool has_supported_array = false;
			for (ArrayProperty& a : c.arrays) {
				for (Property& p : a.children) {
					if (isSupportedEvoxArrayChildGetter(p) || isSupportedEvoxArrayChildSetter(p)) {
						has_supported_array = true;
						break;
					}
				}
				if (has_supported_array) break;
			}
			out.length = 0;
			L("// Generated by meta.cpp");
			L("// import core:", c.id, " as ", c.id, OUT_ENDL);

			StringView imported_types[32];
			i32 imported_types_count = 0;
			auto emitImportForType = [&](StringView type) {
				if (type.size() == 0) return;
				if (type.begin[0] == '?') type = {type.begin + 1, type.end};
				if (isSpanType(type)) type = spanElementBaseType(type);
				const EvoxType import_type = getEvoxType(type);
				if (import_type != EvoxType::VEC2_T && import_type != EvoxType::VEC3_T && import_type != EvoxType::DVEC3_T && import_type != EvoxType::VEC4_T &&
					import_type != EvoxType::COLOR_T && import_type != EvoxType::QUAT_T && import_type != EvoxType::ENTITY_T && import_type != EvoxType::ENUM_T &&
					import_type != EvoxType::STRUCT_T && import_type != EvoxType::OBJECT_T)
					return;
				if (equal(type, c.name)) return;
				for (i32 i = 0; i < imported_types_count; ++i) {
					if (equal(imported_types[i], type)) return;
				}
				imported_types[imported_types_count++] = type;
				out.add("import \"core:");
				appendEvoxImportType(out, type);
				out.add("\"" OUT_ENDL);
			};
			emitImportForType(StringView{"Entity", "Entity" + 6});
			for (Function& f : c.functions) {
				if (!isSupportedEvoxFunction(f)) continue;
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
					if (isSupportedEvoxArrayChildGetter(p) || isSupportedEvoxArrayChildSetter(p)) {
						has_array_child = true;
						break;
					}
				}
				if (!has_array_child) continue;
				L("struct ", c.name, a.name, "ArrayItem { entity : i32; idx : i32; module : cptr; }" OUT_ENDL);
			}

			L("extern fn ", c.id, "(e : Entity) : ?", c.name, ";");
			L("extern fn create", c.name, "(e : Entity) : ?", c.name, ";");

			for (Function& f : c.functions) {
				if (!isSupportedEvoxFunction(f)) continue;
				out.add("extern fn ", functionScriptName(f), "(");
				i32 arg_idx = 0;
				forEachArg(f.args, [&](const Arg& arg, bool is_first) {
					if (!is_first) out.add(", ");
					appendEvoxDeclArgName(out, arg.name, arg_idx);
					out.add(" : ");
					if (is_first && isEvoxEntityType(arg.type))
						out.add(c.name);
					else
						appendEvoxDeclArgType(out, arg);
					++arg_idx;
				});
				out.add(") : ");
				appendEvoxDeclType(out, f.return_type);
				out.add(";" OUT_ENDL);
			}
			for (Property& p : c.properties) {
				if (isSupportedEvoxPropertyGetter(p)) emitComponentPropertyDecl(out, c, p, false);
				if (isSupportedEvoxPropertySetter(p)) emitComponentPropertyDecl(out, c, p, true);
			}
			for (ArrayProperty& a : c.arrays) {
				bool has_array_child = false;
				for (Property& p : a.children) {
					if (isSupportedEvoxArrayChildGetter(p) || isSupportedEvoxArrayChildSetter(p)) {
						has_array_child = true;
						break;
					}
				}
				if (!has_array_child) continue;
				L("extern fn ", a.id, "Count(component : ", c.name, ") : i32;");
				L("extern fn ", a.id, "(component : ", c.name, ", idx : i32) : ?", c.name, a.name, "ArrayItem;");
				for (Property& p : a.children) {
					if (isSupportedEvoxArrayChildGetter(p)) emitArrayChildPropertyDecl(out, c, a, p, false);
					if (isSupportedEvoxArrayChildSetter(p)) emitArrayChildPropertyDecl(out, c, a, p, true);
				}
			}
			out.add(OUT_ENDL);
			StaticString<256> path("data/scripts/core/");
			appendLowercase(path, c.id);
			path.append(".evox");
			writeFile(path, out);
		}
	}
}

void serializeEvoxMeta(MetaData& data) {
	g_meta_data = &data;
	for (Object& o : data.objects) {
		for (Function& f : o.functions) {
			if (hasUnsupportedEvoxFunctionArg(f)) {
				logUnsupportedEvoxFunctionArgs("object function", o.name, f);
			}
		}
	}
	for (Module& m : data.modules) {
		for (Function& f : m.functions) {
			if (hasUnsupportedEvoxFunctionArg(f)) {
				logUnsupportedEvoxFunctionArgs("module function", m.id, f);
			}
		}
		for (Component& c : m.components) {
			for (Function& f : c.functions) {
				if (hasUnsupportedEvoxFunctionArg(f)) {
					logUnsupportedEvoxFunctionArgs("component function", c.id, f);
				}
			}
		}
	}
	OutputStream out;
	emitGeneratedHeader(out, data);

	L("namespace Lumix::Evox::generated {" OUT_ENDL);

	emitGeneratedWorldModuleAccessors(out, data);
	emitGeneratedComponentEntityAccessors(out, data);
	emitGeneratedComponentCreators(out, data);
	i32 wrapper_idx = 0;
	emitGeneratedModuleWrappers(out, data, &wrapper_idx);
	emitGeneratedComponentWrappers(out, data, &wrapper_idx);
	emitGeneratedObjectWrappers(out, data, &wrapper_idx);

	L("static void registerGeneratedEngineImport(HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>& functions) {");
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"create", c.name, "\")}, &evox_entity_create", c.name, ");");
			L("functions.insert({StringView(\"core:", c.id, "\"), StringView(\"", c.id, "\")}, &evox_entity_", c.id, ");");
		}
	}
	wrapper_idx = 0;
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;

		bool any_function = false;
		for (Function& f : m.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			if (isSpanType(f.return_type)) {
				out.add("functions.insert({StringView(\"core:", m.id, "\"), StringView(\"", functionScriptName(f), "Count\")}, &"); appendSpanIteratorCountName(out, m, f, wrapper_idx); L(");"); ++wrapper_idx;
				out.add("functions.insert({StringView(\"core:", m.id, "\"), StringView(\"", functionScriptName(f), "Get\")}, &"); appendSpanIteratorGetName(out, m, f, wrapper_idx); L(");"); ++wrapper_idx;
			}
			else {
				out.add("functions.insert({StringView(\"core:", m.id, "\"), StringView(\"", functionScriptName(f), "\")}, &");
				appendModuleWrapperName(out, m, f, wrapper_idx);
				L(");");
				++wrapper_idx;
			}
			any_function = true;
		}
		if (any_function) {
			L("functions.insert({StringView(\"core:", m.id, "\"), StringView(\"", m.id, "\")}, &evox_world_", m.id, ");");
		}
	}
	emitGeneratedComponentImportRegistrations(out, data, &wrapper_idx);
	emitGeneratedObjectImportRegistrations(out, data, &wrapper_idx);

	L("}" OUT_ENDL);
	L("} // namespace Lumix::Evox::generated");
	formatCPP(out);
	writeFile("src/evox/evox_capi.gen.h", out);

	serializeCoreImports(data);
	g_meta_data = nullptr;
}

} // anonymous namespace

META_PLUGIN(serializeEvoxMeta)
