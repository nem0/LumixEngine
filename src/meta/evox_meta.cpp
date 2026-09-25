#include "meta.h"
#include <stdio.h>

#define OUT_ENDL "\r\n"
#define L(...) out.add(__VA_ARGS__, OUT_ENDL)

namespace {

enum class EvoxType { UNKNOWN, VOID_T, BOOL_T, I8_T, U8_T, I16_T, U16_T, I32_T, U32_T, I64_T, U64_T, F32_T, VEC2_T, VEC3_T, DVEC3_T, VEC4_T, COLOR_T, QUAT_T, ENTITY_T, ENUM_T, STRUCT_T, OBJECT_T, PATH_T, STRING_T };

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

template <int CAPACITY> void appendCamelCase(StaticString<CAPACITY>& out, StringView value) {
	bool capitalize = false;
	for (const char* c = value.begin; c != value.end; ++c) {
		if (*c == '_') {
			capitalize = true;
			continue;
		}
		const char ch = capitalize && *c >= 'a' && *c <= 'z' ? char(*c - 'a' + 'A') : *c;
		out.append(StringView{&ch, &ch + 1});
		capitalize = false;
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

bool isWorldType(StringView type) {
	type = objectBaseType(type);
	return equal(type, "World") || equal(type, "Lumix::World");
}

const Object* findObjectByTypeName(StringView type) {
	if (!g_meta_data) return nullptr;
	type = objectBaseType(type);
	for (Object& o : g_meta_data->objects) {
		if (equal(type, o.name) || equal(type, o.full)) return &o;
	}
	return nullptr;
}

bool isResourceObject(const Object& object) {
	return equal(object.base, "Resource");
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

EvoxType getScalarType(StringView type) {
	if (equal(type, "bool")) return EvoxType::BOOL_T;
	if (equal(type, "i8")) return EvoxType::I8_T;
	if (equal(type, "u8")) return EvoxType::U8_T;
	if (equal(type, "i16")) return EvoxType::I16_T;
	if (equal(type, "u16")) return EvoxType::U16_T;
	if (equal(type, "i32") || equal(type, "int")) return EvoxType::I32_T;
	if (equal(type, "u32")) return EvoxType::U32_T;
	if (equal(type, "i64")) return EvoxType::I64_T;
	if (equal(type, "u64")) return EvoxType::U64_T;
	if (equal(type, "float")) return EvoxType::F32_T;
	return EvoxType::UNKNOWN;
}

bool isIntegerType(EvoxType type) {
	switch (type) {
		case EvoxType::I8_T: case EvoxType::U8_T:
		case EvoxType::I16_T: case EvoxType::U16_T:
		case EvoxType::I32_T: case EvoxType::U32_T:
		case EvoxType::I64_T: case EvoxType::U64_T: return true;
		default: return false;
	}
}

// Aliases currently support scalar value types. Resolve chains with a bounded
// walk so malformed metadata (cycles or ambiguous names) stays unsupported.
StringView resolveScalarAlias(StringView type) {
	if (!g_meta_data) return type;
	bool resolved = false;
	for (i32 depth = 0; depth <= g_meta_data->aliases.size; ++depth) {
		const TypeAlias* match = nullptr;
		for (const TypeAlias& alias : g_meta_data->aliases) {
			if (!equal(alias.name, type)) continue;
			if (match) return {};
			match = &alias;
		}
		if (!match) {
			if (!resolved || getScalarType(type) != EvoxType::UNKNOWN) return type;
			return {};
		}
		type = match->type;
		resolved = true;
	}
	return {};
}

bool isWrappedAlias(StringView type) {
	const StringView underlying = resolveScalarAlias(type);
	return underlying.size() > 0 && !equal(type, underlying);
}

EvoxType getEvoxType(StringView type) {
	type = resolveScalarAlias(type);
	const EvoxType scalar_type = getScalarType(type);
	if (scalar_type != EvoxType::UNKNOWN) return scalar_type;
	if (equal(type, "void")) return EvoxType::VOID_T;
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
	// World uses the handwritten core:world unit, but shares the object handle ABI.
	if (isWorldType(type) || findObjectByTypeName(type)) return EvoxType::OBJECT_T;
	return EvoxType::UNKNOWN;
}

void appendEnumValue(OutputStream& out, const Enum& e, const Enumerator& value) {
	char buffer[32];
	switch (getEvoxType(e.underlying_type)) {
		case EvoxType::U8_T: case EvoxType::U16_T:
		case EvoxType::U32_T: case EvoxType::U64_T:
			snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value.value);
			break;
		default:
			snprintf(buffer, sizeof(buffer), "%lld", (long long)value.value);
			break;
	}
	out.add(buffer);
}

bool isSupportedEvoxType(StringView type);

bool isSupportedEvoxType(StringView type, EvoxType t) {
	if (isSpanType(type)) return isSupportedEvoxType(spanElementBaseType(type));
	if (t == EvoxType::UNKNOWN) return false;
	if (t == EvoxType::ENUM_T) return isIntegerType(getEvoxType(findEnumByTypeName(type)->underlying_type));
	if (t != EvoxType::STRUCT_T) return true;
	const Struct* s = findStructByTypeName(type);
	if (!s) return false;
	for (const StructVar& v : s->vars) {
		const EvoxType field_type = getEvoxType(v.type);
		if (field_type == EvoxType::UNKNOWN) return false;
		if (field_type == EvoxType::ENUM_T && !isSupportedEvoxType(v.type, field_type)) return false;
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
	if (evox_type == EvoxType::ENUM_T) return isSupportedEvoxType(type, evox_type);
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
	if (arg.is_ref && !arg.is_const) return type != EvoxType::UNKNOWN && type != EvoxType::ENUM_T && type != EvoxType::PATH_T;
	return type != EvoxType::PATH_T && isSupportedEvoxType(arg.type, type);
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

void appendPropertyScriptName(OutputStream& out, Property& p, bool is_setter) {
	out.add(is_setter ? "set" : "get");
	if (!p.is_var) {
		out.add(p.name);
		return;
	}

	bool uppercase = true;
	for (const char* c = p.name.begin; c != p.name.end; ++c) {
		if (*c == '_') {
			uppercase = true;
			continue;
		}
		char value = *c;
		if (uppercase && value >= 'a' && value <= 'z') value = char(value - 'a' + 'A');
		out.add(value);
		uppercase = false;
	}
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
			if (equal(type, "EntityRef")) out.add("EntityRef(", name, ".index)");
			else out.add("EntityPtr(", name, "_has_value ? ", name, ".index : -1)");
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
			if (equal(type, "EntityPtr")) {
				L("EX_ARG(frame, u8, ", name, "_has_value);");
				L("EX_ARG(frame, ExEntity, ", name, ");");
			}
			else L("EX_ARG(frame, ExEntity, ", name, ");");
			break;
		case EvoxType::COLOR_T:
			L("EX_ARG(frame, i32, ", name, "_r);");
			L("EX_ARG(frame, i32, ", name, "_g);");
			L("EX_ARG(frame, i32, ", name, "_b);");
			L("EX_ARG(frame, i32, ", name, "_a);");
			break;
		case EvoxType::ENUM_T: {
			StaticString<256> value_name(name, "_value");
			emitFrameValueRead(out, resolveScalarAlias(findEnumByTypeName(type)->underlying_type), makeStringView(value_name.buffer));
			break;
		}
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

void emitMutableRefCopy(OutputStream& out, StringView type, StringView dst, StringView src, bool to_native, const char* world_expr = nullptr) {
	const EvoxType evox_type = getEvoxType(type);
	if (evox_type == EvoxType::ENTITY_T) {
		if (to_native) L(dst, " = EntityPtr(", src, ".index);");
		else {
			L(dst, ".index = ", src, ".index;");
			L(dst, ".padding = 0;");
			L(dst, ".world = ", world_expr ? world_expr : "nullptr", ";");
		}
		return;
	}
	if (evox_type == EvoxType::STRUCT_T) {
		const Struct* s = findStructByTypeName(type);
		if (s) {
			for (const StructVar& field : s->vars) {
				StaticString<256> dst_field(dst, dst.size() > 0 && dst[dst.size() - 1] == '>' ? "" : ".", field.name);
				StaticString<256> src_field(src, src.size() > 0 && src[src.size() - 1] == '>' ? "" : ".", field.name);
				emitMutableRefCopy(out, field.type,
					StringView{dst_field.buffer, dst_field.buffer + dst_field.length},
					StringView{src_field.buffer, src_field.buffer + src_field.length},
					to_native, world_expr);
			}
			return;
		}
	}
	L(dst, " = ", src, ";");
}

void emitArgRead(OutputStream& out, const Arg& arg) {
	if (getEvoxType(arg.type) == EvoxType::OBJECT_T) {
		emitFrameValueRead(out, arg.type, arg.name);
		return;
	}
	if (arg.is_ref && !arg.is_const) {
		if (isExternCompatibleEvoxType(arg.type)) {
			L("EX_ARG(frame, ", arg.type, "*, ", arg.name, ");");
			return;
		}
		L("EX_ARG(frame, Evox_", arg.type, "*, ", arg.name, ");");
		L(arg.type, " ", arg.name, "_value{};");
		StaticString<256> value_name(arg.name, "_value");
		StaticString<256> source_name(arg.name, "->");
		emitMutableRefCopy(out, arg.type, StringView{value_name.buffer, value_name.buffer + value_name.length}, StringView{source_name.buffer, source_name.buffer + source_name.length}, true);
		return;
	}
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
	if (getEvoxType(arg.type) == EvoxType::OBJECT_T && arg.is_ref) out.add("*", arg.name);
	else if (arg.is_ref && !arg.is_const) {
		if (isExternCompatibleEvoxType(arg.type)) out.add("*", arg.name);
		else out.add(arg.name, "_value");
	}
	else if (isEvoxStringArg(arg)) out.add("evox_string_arg_", arg.name);
	else if (isEvoxPathArg(arg)) out.add("Path(StringView{", arg.name, ".begin, (u64)", arg.name, ".length})");
	else if (getEvoxType(arg.type) == EvoxType::STRING_T) out.add("StringView{", arg.name, ".begin, (u64)", arg.name, ".length}");
	else appendValueExpression(out, arg.type, arg.name);
}

void emitResult(OutputStream& out, const char* value) {
	L("EX_RESULT(frame, ", value, ");");
}

i32 evoxTypeAlignment(StringView type);
i32 evoxTypeSize(StringView type);

i32 evoxTypeAlignment(StringView type) {
	switch (getEvoxType(type)) {
		case EvoxType::VOID_T:
		case EvoxType::BOOL_T:
		case EvoxType::I8_T:
		case EvoxType::U8_T: return 1;
		case EvoxType::I16_T:
		case EvoxType::U16_T: return 2;
		case EvoxType::I32_T:
		case EvoxType::U32_T:
		case EvoxType::F32_T:
		case EvoxType::VEC2_T:
		case EvoxType::VEC3_T:
		case EvoxType::VEC4_T:
		case EvoxType::COLOR_T:
		case EvoxType::QUAT_T: return 4;
		case EvoxType::ENUM_T: return evoxTypeAlignment(findEnumByTypeName(type)->underlying_type);
		case EvoxType::I64_T:
		case EvoxType::U64_T:
		case EvoxType::DVEC3_T:
		case EvoxType::ENTITY_T:
		case EvoxType::PATH_T:
		case EvoxType::STRING_T:
		case EvoxType::OBJECT_T: return 8;
		case EvoxType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			if (!s) return 1;
			i32 alignment = 1;
			for (const StructVar& v : s->vars) {
				const i32 field_alignment = evoxTypeAlignment(v.type);
				if (field_alignment > alignment) alignment = field_alignment;
			}
			return alignment;
		}
		case EvoxType::UNKNOWN: return 1;
	}
	return 1;
}

i32 evoxTypeSize(StringView type) {
	switch (getEvoxType(type)) {
		case EvoxType::VOID_T: return 0;
		case EvoxType::BOOL_T:
		case EvoxType::I8_T:
		case EvoxType::U8_T: return 1;
		case EvoxType::I16_T:
		case EvoxType::U16_T: return 2;
		case EvoxType::I32_T:
		case EvoxType::U32_T:
		case EvoxType::F32_T: return 4;
		case EvoxType::ENUM_T: return evoxTypeSize(findEnumByTypeName(type)->underlying_type);
		case EvoxType::I64_T:
		case EvoxType::U64_T:
		case EvoxType::VEC2_T: return 8;
		case EvoxType::VEC3_T: return 12;
		case EvoxType::DVEC3_T: return 24;
		case EvoxType::VEC4_T:
		case EvoxType::COLOR_T:
		case EvoxType::QUAT_T:
		case EvoxType::ENTITY_T:
		case EvoxType::PATH_T:
		case EvoxType::STRING_T: return 16;
		case EvoxType::OBJECT_T: return 8;
		case EvoxType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			if (!s) return 0;
			i32 size = 0;
			for (const StructVar& v : s->vars) {
				const i32 alignment = evoxTypeAlignment(v.type);
				size = (size + alignment - 1) & ~(alignment - 1);
				size += evoxTypeSize(v.type);
			}
			const i32 alignment = evoxTypeAlignment(type);
			return (size + alignment - 1) & ~(alignment - 1);
		}
		case EvoxType::UNKNOWN: return 0;
	}
	return 0;
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
		case EvoxType::BOOL_T:
		case EvoxType::I8_T:
		case EvoxType::U8_T:
		case EvoxType::I16_T:
		case EvoxType::U16_T:
		case EvoxType::I32_T:
		case EvoxType::U32_T:
		case EvoxType::I64_T:
		case EvoxType::U64_T: emitResult(out, value); break;
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
			if (equal(type, "EntityPtr")) {
				StaticString<256> valid(value, ".isValid()");
				L("EX_RESULT(frame, u8(", valid.buffer, ")); ");
			}
			L("EX_RESULT(frame, ExEntity(", index.buffer, ", ", world_expr ? world_expr : "nullptr", "));");
			break;
		}
		case EvoxType::ENUM_T: {
			const StringView underlying = resolveScalarAlias(findEnumByTypeName(type)->underlying_type);
			StaticString<256> v("(", underlying, ")", value);
			appendReturnValue(out, underlying, v.buffer, world_expr);
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
			i32 offset = 0;
			for (const StructVar& v : s->vars) {
				if (getEvoxType(v.type) == EvoxType::VOID_T) continue;
				const i32 alignment = evoxTypeAlignment(v.type);
				const i32 field_offset = (offset + alignment - 1) & ~(alignment - 1);
				if (field_offset > offset) L("frame.result += ", (i32)(field_offset - offset), ";");
				StaticString<128> field_value(value, ".", v.name);
				appendReturnValue(out, v.type, field_value.buffer, world_expr);
				offset = field_offset + evoxTypeSize(v.type);
			}
			const i32 size = evoxTypeSize(type);
			if (size > offset) L("frame.result += ", (i32)(size - offset), ";");
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
	if (!p.is_var && p.getter_name.size() == 0) return false;
	const EvoxType type = getEvoxType(p.type);
	if (!isSupportedEvoxType(p.type, type)) return false;
	bool supported = true;
	forEachArg(p.getter_args, [&](const Arg& arg, bool) {
		if (!isSupportedEvoxPropertyArg(arg)) supported = false;
	});
	return supported;
}

bool isSupportedEvoxPropertySetter(Property& p) {
	if (!p.is_var && p.setter_name.size() == 0) return false;
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

void appendWrapperName(OutputStream& out, Component& c, Function& f) {
	// Do not use the generated function order here: inserting a declaration
	// must not rename every wrapper that follows it in the generated file.
	StaticString<2048> signature(c.id, "::", functionScriptName(f), "(", f.args, ")->", f.return_type);
	const XXH64_hash_t hash = XXH3_64bits(signature.buffer, signature.length);
	out.add("evox_", c.id, "_", functionScriptName(f), "_", hash);
}

void appendPropertyWrapperName(OutputStream& out, Component& c, Property& p, bool is_setter) {
	// Property wrappers must use the same order-independent naming scheme as
	// ordinary function wrappers. Include the accessor signature so distinct
	// properties/accessors cannot collide.
	StaticString<256> var_accessor("");
	if (p.is_var) {
		var_accessor.append(is_setter ? "set" : "get");
		bool uppercase = true;
		for (const char* c = p.name.begin; c != p.name.end; ++c) {
			if (*c == '_') {
				uppercase = true;
				continue;
			}
			char value = *c;
			if (uppercase && value >= 'a' && value <= 'z') value = char(value - 'a' + 'A');
			var_accessor.append(StringView{&value, &value + 1});
			uppercase = false;
		}
	}
	const StringView accessor = p.is_var ? StringView(var_accessor.buffer, var_accessor.buffer + var_accessor.length) : (is_setter ? p.setter_name : p.getter_name);
	const StringView args = is_setter ? p.setter_args : p.getter_args;
	StaticString<2048> signature(c.id, "::", accessor, "(", args, ")->", p.type);
	const XXH64_hash_t hash = XXH3_64bits(signature.buffer, signature.length);
	out.add("evox_", c.id, "_", accessor, "_", hash);
}

void serializeEvoxWrapper(OutputStream& out, Module& m, Component& c, Function& f) {
	out.add("static void ");
	appendWrapperName(out, c, f);
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

void serializeEvoxPropertyWrapper(OutputStream& out, Module& m, Component& c, Property& p, bool is_setter) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendPropertyWrapperName(out, c, p, is_setter);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	if (p.is_var) {
		L("EX_ARG(frame, ExComponent, component);");
		L(m.name, "* module = static_cast<", m.name, "*>(component.module);");
		if (is_setter) {
			emitFrameValueRead(out, p.type, makeStringView("value"));
			L("module->get", c.name, "(EntityRef(component.index)).", p.name, " = value;");
		}
		else {
			L("const auto& value = module->get", c.name, "(EntityRef(component.index)).", p.name, ";");
			appendReturnValue(out, p.type, "value", "&module->getWorld()");
		}
	}
	else {
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
	}
	L("}" OUT_ENDL);
}

template <int CAPACITY> void appendStableWrapperHash(OutputStream& out, const StaticString<CAPACITY>& signature) {
	const XXH64_hash_t hash = XXH3_64bits(signature.buffer, signature.length);
	out.add("_", hash);
}

void appendArrayCountWrapperName(OutputStream& out, Component& c, ArrayProperty& a) {
	StaticString<2048> s(c.id, "::", a.id, "::count");
	out.add("evox_", c.id, "_", a.id, "_count"); appendStableWrapperHash(out, s);
}

void appendArrayItemWrapperName(OutputStream& out, Component& c, ArrayProperty& a) {
	StaticString<2048> s(c.id, "::", a.id, "::item");
	out.add("evox_", c.id, "_", a.id, "_item"); appendStableWrapperHash(out, s);
}

void appendArrayMutationWrapperName(OutputStream& out, Component& c, ArrayProperty& a, bool is_add) {
	const char* op = is_add ? "add" : "remove";
	StaticString<2048> s(c.id, "::", a.id, "::", op, "(i32)->void");
	out.add("evox_", c.id, "_", a.id, "_", op); appendStableWrapperHash(out, s);
}

void appendArrayChildWrapperName(OutputStream& out, Component& c, ArrayProperty& a, Property& p, bool is_setter) {
	const StringView name = is_setter ? p.setter_name : p.getter_name;
	const StringView args = is_setter ? p.setter_args : p.getter_args;
	StaticString<2048> s(c.id, "::", a.id, "::", name, "(", args, ")->", p.type);
	out.add("evox_", c.id, "_", a.id, "_", name); appendStableWrapperHash(out, s);
}

void appendModuleWrapperName(OutputStream& out, Module& m, Function& f) {
	StaticString<2048> s(m.id, "::", functionScriptName(f), "(", f.args, ")->", f.return_type);
	out.add("evox_", m.id, "_", functionScriptName(f)); appendStableWrapperHash(out, s);
}

void appendObjectWrapperName(OutputStream& out, Object& o, Function& f) {
	StaticString<2048> s(o.name, "::", functionScriptName(f), "(", f.args, ")->", f.return_type);
	out.add("evox_object_", o.name, "_", functionScriptName(f)); appendStableWrapperHash(out, s);
}

void appendSourceUnitPath(StaticString<256>& out, const char* filename, StringView leaf) {
	const char* begin = filename;
	if (begin[0] == 's' && begin[1] == 'r' && begin[2] == 'c' && begin[3] == '/') begin += 4;
	const char* end = filename;
	while (*end) ++end;
	const char* slash = end;
	while (slash > begin && slash[-1] != '/') --slash;
	if (slash > begin) {
		out.append(StringView{begin, slash - 1});
		out.append("/");
	}
	appendLowercase(out, leaf);
}

void appendModuleUnitName(StaticString<256>& out, const Module& m) {
	appendSourceUnitPath(out, m.filename, StringView{"module", "module" + 6});
}

template <int CAPACITY> void writeEvoxFile(const StaticString<CAPACITY>& path, OutputStream& out) {
	char filename[CAPACITY + sizeof(".evox") - 1];
	snprintf(filename, sizeof(filename), "%s.evox", path.buffer);
	writeFile(filename, out);
}

void appendObjectUnitName(StaticString<256>& out, const Object& o) {
	appendSourceUnitPath(out, o.filename, o.name);
}

void appendSpanIteratorCountName(OutputStream& out, Module& m, Function& f) {
	StaticString<2048> s(m.id, "::", functionScriptName(f), "(", f.args, ")->", f.return_type, "::count");
	out.add("evox_", m.id, "_", functionScriptName(f), "_count"); appendStableWrapperHash(out, s);
}

void appendSpanIteratorGetName(OutputStream& out, Module& m, Function& f) {
	StaticString<2048> s(m.id, "::", functionScriptName(f), "(", f.args, ")->", f.return_type, "::get");
	out.add("evox_", m.id, "_", functionScriptName(f), "_get"); appendStableWrapperHash(out, s);
}

void serializeEvoxObjectWrapper(OutputStream& out, Object& o, Function& f) {
	out.add("static void ");
	appendObjectWrapperName(out, o, f);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", o.full, "*, object);");
	forEachArg(f.args, [&](const Arg& arg, bool) { emitArgRead(out, arg); });
	if (!equal(f.return_type, "void")) out.add(isReferenceType(f.return_type) ? "auto& ret = " : "auto ret = ");
	out.add("object->", f.name, "(");
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendArgExpression(out, arg);
	});
	L(");");
	const char* return_world_expr = nullptr;
	if (isEvoxEntityType(f.return_type)) {
		forEachArg(f.args, [&](const Arg& arg, bool) {
			if (equal(arg.type, "World") && equal(arg.name, "world")) return_world_expr = "world";
		});
	}
	appendReturnValue(out, f.return_type, "ret", return_world_expr);
	L("}" OUT_ENDL);
}

void serializeEvoxResourceWrappers(OutputStream& out, Object& o) {
	L("static void evox_resource_load_", o.name, "(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, Engine*, engine);");
	L("EX_STRING_ARG(frame, path);");
	L(o.full, "* resource = engine->getResourceManager().load<", o.full, ">(Path(StringView{path.begin, (u64)path.length}));");
	L("EX_RESULT(frame, resource);");
	L("}" OUT_ENDL);

	L("static void evox_resource_unload_", o.name, "(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", o.full, "*, resource);");
	L("if (resource) resource->decRefCount();");
	L("}" OUT_ENDL);

	L("static void evox_resource_isReady_", o.name, "(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", o.full, "*, resource);");
	L("EX_RESULT(frame, resource && resource->isReady());");
	L("}" OUT_ENDL);

	L("static void evox_resource_isFailure_", o.name, "(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", o.full, "*, resource);");
	L("EX_RESULT(frame, resource && resource->isFailure());");
	L("}" OUT_ENDL);
}

void serializeEvoxSpanIteratorWrappers(OutputStream& out, Module& m, Function& f) {
	const StringView element = spanElementBaseType(f.return_type);
	out.add("static void "); appendSpanIteratorCountName(out, m, f); L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ", m.name, "*, module);");
	forEachArg(f.args, [&](const Arg& arg, bool) { emitArgRead(out, arg); });
	L("const auto ret = module->", f.name, "(");
	forEachArg(f.args, [&](const Arg& arg, bool first) { if (!first) out.add(", "); appendArgExpression(out, arg); });
	L(");");
	L("EX_RESULT(frame, (i32)ret.size());");
	L("}" OUT_ENDL);
	out.add("static void "); appendSpanIteratorGetName(out, m, f); L("(ex_runtime* runtime, ex_call_frame frame) {");
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

void serializeEvoxModuleWrapper(OutputStream& out, Module& m, Function& f) {
	if (isSpanType(f.return_type)) return;
	out.add("static void ");
	appendModuleWrapperName(out, m, f);
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
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!arg.is_ref || arg.is_const || getEvoxType(arg.type) == EvoxType::OBJECT_T || isExternCompatibleEvoxType(arg.type)) return;
		StaticString<256> value_name(arg.name, "_value");
		StaticString<256> target_name(arg.name, "->");
		emitMutableRefCopy(out, arg.type, StringView{target_name.buffer, target_name.buffer + target_name.length}, StringView{value_name.buffer, value_name.buffer + value_name.length}, false, "&module->getWorld()");
	});
	appendReturnValue(out, f.return_type, "ret", "&module->getWorld()");
	L("}" OUT_ENDL);
}

void serializeEvoxArrayCountWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a) {
	out.add("static void ");
	appendArrayCountWrapperName(out, c, a);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ExComponent, component);");
	L(m.name, "* module = static_cast<", m.name, "*>(component.module);");
	L("const i32 count = module->get", a.name, "Count(EntityRef(component.index));");
	L("EX_RESULT(frame, count);");
	L("}" OUT_ENDL);
}

void serializeEvoxArrayMutationWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, bool is_add) {
	const char* op = is_add ? "add" : "remove";
	out.add("static void ");
	appendArrayMutationWrapperName(out, c, a, is_add);
	L("(ex_runtime* runtime, ex_call_frame frame) {");
	L("EX_ARG(frame, ExComponent, component);");
	L(m.name, "* module = static_cast<", m.name, "*>(component.module);");
	L("EX_ARG(frame, i32, item_idx);");
	L("module->", op, a.name, "(EntityRef(component.index), item_idx);");
	L("}" OUT_ENDL);
}

void serializeEvoxArrayItemWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a) {
	out.add("static void ");
	appendArrayItemWrapperName(out, c, a);
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

void serializeEvoxArrayChildWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, Property& p, bool is_setter) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendArrayChildWrapperName(out, c, a, p, is_setter);
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
	out.add("#include \"engine/resource_manager.h\"" OUT_ENDL);
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

	auto isMutableRefTypeUsed = [&](StringView type) {
		bool used = false;
		for (Module& module : data.modules) {
			for (Function& function : module.functions) {
				forEachArg(function.args, [&](const Arg& arg, bool) { if (arg.is_ref && !arg.is_const && equal(arg.type, type)) used = true; });
			}
			for (Component& component : module.components) {
				for (Function& function : component.functions) {
					forEachArg(function.args, [&](const Arg& arg, bool) { if (arg.is_ref && !arg.is_const && equal(arg.type, type)) used = true; });
				}
			}
		}
		for (Object& object : data.objects) {
			for (Function& function : object.functions) {
				forEachArg(function.args, [&](const Arg& arg, bool) { if (arg.is_ref && !arg.is_const && equal(arg.type, type)) used = true; });
			}
		}
		return used;
	};
	for (Struct& s : data.structs) {
		if (isExternCompatibleEvoxType(s.name) || !isMutableRefTypeUsed(s.name)) continue;
		out.add("struct Evox_", s.name, " {" OUT_ENDL);
		for (const StructVar& field : s.vars) {
			out.add("\t");
			if (getEvoxType(field.type) == EvoxType::ENTITY_T) out.add("ExEntity");
			else if (getEvoxType(field.type) == EvoxType::STRUCT_T && !isExternCompatibleEvoxType(field.type)) out.add("Evox_", field.type);
			else if (getEvoxType(field.type) == EvoxType::VEC2_T || getEvoxType(field.type) == EvoxType::VEC3_T || getEvoxType(field.type) == EvoxType::DVEC3_T ||
				getEvoxType(field.type) == EvoxType::VEC4_T || getEvoxType(field.type) == EvoxType::COLOR_T || getEvoxType(field.type) == EvoxType::QUAT_T ||
				getEvoxType(field.type) == EvoxType::OBJECT_T) out.add("Lumix::", field.type);
			else out.add(field.type);
			L(" ", field.name, ";");
		}
		L("};");
	}
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
			L("static void evox_entity_create_", c.id, "(ex_runtime* runtime, ex_call_frame frame) {");
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

void emitGeneratedModuleWrappers(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		for (Function& f : m.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			if (isSpanType(f.return_type)) {
				serializeEvoxSpanIteratorWrappers(out, m, f);
			}
			else {
				serializeEvoxModuleWrapper(out, m, f);
			}
		}
	}
}

void emitGeneratedObjectWrappers(OutputStream& out, MetaData& data) {
	for (Object& o : data.objects) {
		if (isResourceObject(o)) serializeEvoxResourceWrappers(out, o);
		for (Function& f : o.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			serializeEvoxObjectWrapper(out, o, f);
		}
	}
}

void emitGeneratedComponentWrappers(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			for (Function& f : c.functions) {
				if (!isSupportedEvoxFunction(f)) continue;
				serializeEvoxWrapper(out, m, c, f);
			}
			for (Property& p : c.properties) {
				if (isSupportedEvoxPropertyGetter(p)) {
					serializeEvoxPropertyWrapper(out, m, c, p, false);
				}
				if (isSupportedEvoxPropertySetter(p)) {
					serializeEvoxPropertyWrapper(out, m, c, p, true);
				}
			}
			for (ArrayProperty& a : c.arrays) {
				serializeEvoxArrayCountWrapper(out, m, c, a);
				serializeEvoxArrayItemWrapper(out, m, c, a);
				serializeEvoxArrayMutationWrapper(out, m, c, a, true);
				serializeEvoxArrayMutationWrapper(out, m, c, a, false);
				for (Property& p : a.children) {
					if (isSupportedEvoxArrayChildGetter(p)) {
						serializeEvoxArrayChildWrapper(out, m, c, a, p, false);
					}
					if (isSupportedEvoxArrayChildSetter(p)) {
						serializeEvoxArrayChildWrapper(out, m, c, a, p, true);
					}
				}
			}
		}
	}
}

void emitGeneratedComponentImportRegistrations(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		StaticString<256> module_unit("core:");
		StaticString<256> module_path("");
		appendModuleUnitName(module_path, m);
		module_unit.append(module_path.buffer);
		module_unit.length -= 7; // remove /module; components live beside the module script
		module_unit.buffer[module_unit.length] = 0;
		for (Component& c : m.components) {
			const int module_unit_length = module_unit.length;
			module_unit.append("/");
			appendLowercase(module_unit, c.id);
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
			if (!has_supported_function && !has_supported_property && !has_supported_array) {
				// Restore the module path before skipping components without
				// script-visible members; otherwise the next component inherits
				// this component's name.
				module_unit.length = module_unit_length;
				module_unit.buffer[module_unit.length] = 0;
				continue;
			}

			for (Function& f : c.functions) {
				if (!isSupportedEvoxFunction(f)) continue;
				out.add("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", functionScriptName(f), "\")}, &");
				appendWrapperName(out, c, f);
				L(");");
			}
			for (Property& p : c.properties) {
				if (isSupportedEvoxPropertyGetter(p)) {
					out.add("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"");
					appendPropertyScriptName(out, p, false);
					out.add("\")}, &");
					appendPropertyWrapperName(out, c, p, false);
					L(");");
				}
				if (isSupportedEvoxPropertySetter(p)) {
					out.add("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"");
					appendPropertyScriptName(out, p, true);
					out.add("\")}, &");
					appendPropertyWrapperName(out, c, p, true);
					L(");");
				}
			}
			for (ArrayProperty& a : c.arrays) {
				L("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", a.id, "Count\")}, &");
				appendArrayCountWrapperName(out, c, a);
				L(");");
				L("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", a.id, "\")}, &");
				appendArrayItemWrapperName(out, c, a);
				L(");");
				L("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"add", a.name, "\")}, &");
				appendArrayMutationWrapperName(out, c, a, true);
				L(");");
				L("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"remove", a.name, "\")}, &");
				appendArrayMutationWrapperName(out, c, a, false);
				L(");");
				for (Property& p : a.children) {
					if (isSupportedEvoxArrayChildGetter(p)) {
						out.add("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", p.getter_name, "\")}, &");
						appendArrayChildWrapperName(out, c, a, p, false);
						L(");");
					}
					if (isSupportedEvoxArrayChildSetter(p)) {
						out.add("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", p.setter_name, "\")}, &");
						appendArrayChildWrapperName(out, c, a, p, true);
						L(");");
					}
				}
			}
			module_unit.length = module_unit_length;
			module_unit.buffer[module_unit.length] = 0;
		}
	}
}

void emitGeneratedObjectImportRegistrations(OutputStream& out, MetaData& data) {
	for (Object& o : data.objects) {
		StaticString<256> unit("core:");
		appendObjectUnitName(unit, o);
		if (isResourceObject(o)) {
			L("functions.insert({StringView(\"", unit.buffer, "\"), StringView(\"load\")}, &evox_resource_load_", o.name, ");");
			L("functions.insert({StringView(\"", unit.buffer, "\"), StringView(\"unload\")}, &evox_resource_unload_", o.name, ");");
			L("functions.insert({StringView(\"", unit.buffer, "\"), StringView(\"isReady\")}, &evox_resource_isReady_", o.name, ");");
			L("functions.insert({StringView(\"", unit.buffer, "\"), StringView(\"isFailure\")}, &evox_resource_isFailure_", o.name, ");");
		}
		for (Function& f : o.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			out.add("functions.insert({StringView(\"", unit.buffer, "\"), StringView(\"", functionScriptName(f), "\")}, &");
			appendObjectWrapperName(out, o, f);
			L(");");
		}
	}
}

void appendEvoxDeclType(OutputStream& out, StringView type) {
	if (isWrappedAlias(type)) {
		out.add(type);
		return;
	}
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
		case EvoxType::I8_T: out.add("i8"); break;
		case EvoxType::U8_T: out.add("u8"); break;
		case EvoxType::I16_T: out.add("i16"); break;
		case EvoxType::U16_T: out.add("u16"); break;
		case EvoxType::I64_T: out.add("i64"); break;
		case EvoxType::U64_T: out.add("u64"); break;
		case EvoxType::I32_T: out.add("i32"); break;
		case EvoxType::U32_T: out.add("u32"); break;
		case EvoxType::F32_T: out.add("f32"); break;
		case EvoxType::VEC2_T: out.add("Vec2"); break;
		case EvoxType::VEC3_T: out.add("Vec3"); break;
		case EvoxType::DVEC3_T: out.add("DVec3"); break;
		case EvoxType::VEC4_T: out.add("Vec4"); break;
		case EvoxType::COLOR_T: out.add("Color"); break;
		case EvoxType::QUAT_T: out.add("Quat"); break;
		case EvoxType::ENTITY_T:
			if (equal(type, "EntityPtr")) out.add("?Entity");
			else out.add("Entity");
			break;
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
			out.add(isWorldType(type) ? makeStringView("World") : o ? o->name : objectBaseType(type));
			break;
		}
		case EvoxType::PATH_T: out.add("[]const u8"); break;
		case EvoxType::STRING_T: out.add("[]const u8"); break;
		default: out.add("void"); break;
	}
}

void appendEvoxDeclArgType(OutputStream& out, const Arg& arg) {
	if (arg.is_ref && !arg.is_const && getEvoxType(arg.type) != EvoxType::OBJECT_T) out.add("*");
	if (isEvoxStringArg(arg)) {
		out.add("[]const u8");
		return;
	}
	appendEvoxDeclType(out, arg.type);
}

void appendEvoxImportType(OutputStream& out, StringView type) {
	if (isSpanType(type)) type = spanElementBaseType(type);
	if (isWrappedAlias(type)) {
		// Wrapped aliases are emitted alongside their declaring source unit.
		// Keep imports consistent with that location (e.g. SoundHandle is
		// generated as core:audio/soundhandle, not core:soundhandle).
		for (const TypeAlias& alias : g_meta_data->aliases) {
			if (!equal(alias.name, type)) continue;
			StaticString<256> name("");
			appendSourceUnitPath(name, alias.filename, alias.name);
			out.add(name.buffer);
			return;
		}
		StaticString<256> name("");
		appendLowercase(name, type);
		out.add(name.buffer);
		return;
	}
	if (isWorldType(type)) {
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
			if (e) {
				StaticString<256> name("");
				appendSourceUnitPath(name, e->filename, e->name);
				out.add(name.buffer);
			} else out.add(type);
			break;
		}
		case EvoxType::STRUCT_T: {
			const Struct* s = findStructByTypeName(type);
			if (s) {
				StaticString<256> name("");
				appendSourceUnitPath(name, s->filename, s->name);
				out.add(name.buffer);
			} else out.add(type);
			break;
		}
		case EvoxType::OBJECT_T: {
			const Object* o = findObjectByTypeName(type);
			if (o) {
				StaticString<256> name("");
				appendObjectUnitName(name, *o);
				out.add(name.buffer);
			}
			break;
		}
		default: out.add(type); break;
	}
}

void appendEvoxDeclArgName(OutputStream& out, StringView name, i32 idx) {
	if (equal(name, "type")) {
		out.add("type_");
		return;
	}
	if (name.size() > 0) {
		out.add(name);
		return;
	}
	out.add("arg", idx + 1);
}

void emitComponentPropertyDecl(OutputStream& out, Component& c, Property& p, bool is_setter) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("extern fn ");
	appendPropertyScriptName(out, p, is_setter);
	out.add("(");
	if (p.is_var) {
		out.add("entity : ", c.name);
		if (is_setter) out.add(", value : ");
		if (is_setter) appendEvoxDeclType(out, p.type);
		if (accessor_args.size() > 0) out.add(", ");
	}
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
	if (e.values.size == 0 || !isIntegerType(getEvoxType(e.underlying_type))) return;
	out.add("enum ", e.name, " : ");
	appendEvoxDeclType(out, resolveScalarAlias(e.underlying_type));
	out.add(" {" OUT_ENDL);
	for (i32 i = 0, c = e.values.size; i < c; ++i) {
		Enumerator& en = e.values[i];
		out.add("\t", en.name, " = ");
		appendEnumValue(out, e, en);
		if (i != c - 1) out.add(",");
		out.add(OUT_ENDL);
	}
	out.add("};" OUT_ENDL OUT_ENDL);
}

void serializeCoreImports(MetaData& data) {
	OutputStream out;

	for (const TypeAlias& alias : data.aliases) {
		if (!isWrappedAlias(alias.name)) continue;
		out.length = 0;
		L("// Generated by meta.cpp" OUT_ENDL);
		// A single scalar field has the same size/alignment and call-frame
		// representation as the native alias, including refs and span elements.
		// Flatten chains to the scalar while keeping each alias a distinct type.
		out.add("extern struct ", alias.name, " { value : ");
		appendEvoxDeclType(out, resolveScalarAlias(alias.name));
		L("; }" OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendSourceUnitPath(path, alias.filename, alias.name);
		writeEvoxFile(path, out);
	}

	auto output_enum = [&](Enum& e) {
		if (e.values.size == 0) return;
		if (!isIntegerType(getEvoxType(e.underlying_type))) {
			logInfo("Evox: skipped enum ", e.full, " because its underlying type ", e.underlying_type, " is not supported");
			return;
		}

		out.length = 0;
		L("// Generated by meta.cpp" OUT_ENDL);
		out.add("enum ", e.name, " : ");
		appendEvoxDeclType(out, resolveScalarAlias(e.underlying_type));
		L(" {");
		for (i32 i = 0, c = e.values.size; i < c; ++i) {
			const Enumerator& en = e.values[i];
			out.add("\t", en.name, " = ");
			appendEnumValue(out, e, en);
			if (i != c - 1) out.add(",");
			out.add(OUT_ENDL);
		}
		L("}" OUT_ENDL);
		StaticString<256> path("data/scripts/core/");
		appendSourceUnitPath(path, e.filename, e.name);
		writeEvoxFile(path, out);
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
				type != EvoxType::QUAT_T && type != EvoxType::ENTITY_T && type != EvoxType::ENUM_T && type != EvoxType::STRUCT_T && !isWrappedAlias(v.type)) {
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
		appendSourceUnitPath(path, s.filename, s.name);
		writeEvoxFile(path, out);
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
		if (!has_supported_function && !isResourceObject(o)) continue;

		out.length = 0;
		L("// Generated by meta.cpp");
		StaticString<256> object_unit("");
		appendObjectUnitName(object_unit, o);
		out.add("// import core:", object_unit.buffer, " as ", o.name, OUT_ENDL OUT_ENDL);

		StringView imported_types[32];
		i32 imported_types_count = 0;
		if (isResourceObject(o)) {
			L("import \"core:engine/engine\"");
		}
		auto emitImportForType = [&](StringView type) {
			if (isSpanType(type)) type = spanElementBaseType(type);
			const EvoxType import_type = getEvoxType(type);
			if (import_type != EvoxType::VEC2_T && import_type != EvoxType::VEC3_T && import_type != EvoxType::DVEC3_T && import_type != EvoxType::VEC4_T &&
				import_type != EvoxType::COLOR_T && import_type != EvoxType::QUAT_T && import_type != EvoxType::ENTITY_T && import_type != EvoxType::ENUM_T &&
				import_type != EvoxType::STRUCT_T && import_type != EvoxType::OBJECT_T && !isWrappedAlias(type)) return;
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
		if (isResourceObject(o)) {
			L("extern fn load(engine : Engine, path : []const u8) : ", o.name, ";");
			L("extern fn unload(resource : ", o.name, ") : void;");
			L("extern fn isReady(resource : ", o.name, ") : bool;");
			L("extern fn isFailure(resource : ", o.name, ") : bool;");
		}
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
		appendObjectUnitName(path, o);
		writeEvoxFile(path, out);
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
		L("// import core:", m.id, "/module as ", m.id, OUT_ENDL);
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
				import_type != EvoxType::STRUCT_T && import_type != EvoxType::OBJECT_T && !isWrappedAlias(type) && !equal(type, "World")) {
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
		appendModuleUnitName(path, m);
		writeEvoxFile(path, out);
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
						StaticString<256> component_unit("core:");
			appendSourceUnitPath(component_unit, m.filename, c.id);
			L("// import ", component_unit.buffer, " as ", c.id, OUT_ENDL);

			StringView imported_types[32];
			i32 imported_types_count = 0;
			auto emitImportForType = [&](StringView type) {
				if (type.size() == 0) return;
				if (type.begin[0] == '?') type = {type.begin + 1, type.end};
				if (isSpanType(type)) type = spanElementBaseType(type);
				const EvoxType import_type = getEvoxType(type);
				if (import_type != EvoxType::VEC2_T && import_type != EvoxType::VEC3_T && import_type != EvoxType::DVEC3_T && import_type != EvoxType::VEC4_T &&
					import_type != EvoxType::COLOR_T && import_type != EvoxType::QUAT_T && import_type != EvoxType::ENTITY_T && import_type != EvoxType::ENUM_T &&
					import_type != EvoxType::STRUCT_T && import_type != EvoxType::OBJECT_T && !isWrappedAlias(type))
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

			StaticString<256> getter_name("");
			appendCamelCase(getter_name, c.id);
			L("extern fn ", getter_name.buffer, "(e : Entity) : ?", c.name, ";");
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
				L("extern fn add", a.name, "(component : ", c.name, ", idx : i32) : void;");
				L("extern fn remove", a.name, "(component : ", c.name, ", idx : i32) : void;");
				for (Property& p : a.children) {
					if (isSupportedEvoxArrayChildGetter(p)) emitArrayChildPropertyDecl(out, c, a, p, false);
					if (isSupportedEvoxArrayChildSetter(p)) emitArrayChildPropertyDecl(out, c, a, p, true);
				}
			}
			out.add(OUT_ENDL);
			StaticString<256> path("data/scripts/core/");
			appendSourceUnitPath(path, m.filename, c.id);
			writeEvoxFile(path, out);
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
	emitGeneratedModuleWrappers(out, data);
	emitGeneratedComponentWrappers(out, data);
	emitGeneratedObjectWrappers(out, data);

	L("static void registerGeneratedEngineImport(HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>& functions) {");
	for (Module& m : data.modules) {
		StaticString<256> module_unit("core:");
		StaticString<256> module_path("");
		appendModuleUnitName(module_path, m);
		module_unit.append(module_path.buffer);
		module_unit.length -= 7; // remove /module; component units are sibling scripts
		module_unit.buffer[module_unit.length] = 0;
		for (Component& c : m.components) {
			StaticString<256> component_unit("");
			component_unit.append(module_unit.buffer);
			component_unit.append("/");
			appendLowercase(component_unit, c.id);
			L("functions.insert({StringView(\"", component_unit.buffer, "\"), StringView(\"create", c.name, "\")}, &evox_entity_create_", c.id, ");");
			StaticString<256> getter_name("");
			appendCamelCase(getter_name, c.id);
			L("functions.insert({StringView(\"", component_unit.buffer, "\"), StringView(\"", getter_name.buffer, "\")}, &evox_entity_", c.id, ");");
		}
	}
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		StaticString<256> module_unit("core:");
		StaticString<256> module_path("");
		appendModuleUnitName(module_path, m);
		module_unit.append(module_path.buffer);

		bool any_function = false;
		for (Function& f : m.functions) {
			if (!isSupportedEvoxFunction(f)) continue;
			if (isSpanType(f.return_type)) {
				out.add("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", functionScriptName(f), "Count\")}, &"); appendSpanIteratorCountName(out, m, f); L(");");
				out.add("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", functionScriptName(f), "Get\")}, &"); appendSpanIteratorGetName(out, m, f); L(");");
			}
			else {
				out.add("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", functionScriptName(f), "\")}, &");
				appendModuleWrapperName(out, m, f);
				L(");");
			}
			any_function = true;
		}
		if (any_function) {
			L("functions.insert({StringView(\"", module_unit.buffer, "\"), StringView(\"", m.id, "\")}, &evox_world_", m.id, ");");
		}
	}
	emitGeneratedComponentImportRegistrations(out, data);
	emitGeneratedObjectImportRegistrations(out, data);

	L("}" OUT_ENDL);
	L("} // namespace Lumix::Evox::generated");
	formatCPP(out);
	writeFile("src/evox/evox_capi.gen.h", out);

	serializeCoreImports(data);
	g_meta_data = nullptr;
}

} // anonymous namespace

META_PLUGIN(serializeEvoxMeta)
