#include "meta.h"

#define OUT_ENDL "\r\n"
#define L(...) out.add(__VA_ARGS__, OUT_ENDL)

namespace {

enum class LumScriptType {
	UNKNOWN,
	VOID_T,
	BOOL_T,
	I32_T,
	F32_T,
	VEC3_T,
	DVEC3_T,
	QUAT_T,
	ENTITY_T,
	ENUM_T,
	PATH_T
};

template <int CAPACITY>
struct StaticString {
	template <typename... Args> StaticString(Args&&... args) {
		buffer[0] = 0;
		int dummy[] = {
			(append(args),0)...
		};
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

	operator const char* () const { return buffer; }

	char buffer[CAPACITY];
	int length = 0;
};

static MetaData* g_meta_data = nullptr;

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

// Emits C++ enum type name suitable for casts (uses fully-qualified metadata name when available).
void appendEnumCPPType(OutputStream& out, StringView type) {
	const Enum* e = findEnumByTypeName(type);
	out.add(e ? e->full : type);
}

// Maps reflected C++ type names to the compact LumScript type category.
LumScriptType getLumScriptType(StringView type) {
	// Single source of truth for supported LumScript-facing C++ types.
	if (equal(type, "void")) return LumScriptType::VOID_T;
	if (equal(type, "bool")) return LumScriptType::BOOL_T;
	if (equal(type, "i32") || equal(type, "int") || equal(type, "u32")) return LumScriptType::I32_T;
	if (equal(type, "float")) return LumScriptType::F32_T;
	if (equal(type, "Vec3")) return LumScriptType::VEC3_T;
	if (equal(type, "DVec3")) return LumScriptType::DVEC3_T;
	if (equal(type, "Quat")) return LumScriptType::QUAT_T;
	if (equal(type, "EntityRef") || equal(type, "EntityPtr")) return LumScriptType::ENTITY_T;
	if (equal(type, "Path")) return LumScriptType::PATH_T;
	if (findEnumByTypeName(type)) return LumScriptType::ENUM_T;
	return LumScriptType::UNKNOWN;
}

// Returns true when a reflected type can be represented in LumScript bindings.
bool isSupportedLumScriptType(StringView type) {
	return getLumScriptType(type) != LumScriptType::UNKNOWN;
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

// Validates argument shape and type for generated wrapper calls.
bool isSupportedLumScriptArg(const Arg& arg) {
	return !arg.is_ref && ((!arg.is_ptr && isSupportedLumScriptType(arg.type)) || isLumScriptStringArg(arg));
}

// Returns true when an argument is supported by generic function wrappers (excluding enums).
bool isSupportedLumScriptFunctionArg(const Arg& arg) {
	if (isLumScriptPathArg(arg)) return false;
	if (arg.is_ref) return false;
	if (isLumScriptStringArg(arg)) return true;
	if (arg.is_ptr) return false;
	const LumScriptType type = getLumScriptType(arg.type);
	return type != LumScriptType::UNKNOWN && type != LumScriptType::ENUM_T && type != LumScriptType::PATH_T;
}

// Resolves the exposed script name, preferring explicit alias when present.
StringView functionScriptName(Function& f) {
	return f.attributes.alias.size() > 0 ? f.attributes.alias : f.name;
}

// Emits generated code for an LumScript C API `ls_type` expression.
void appendLumScriptType(OutputStream& out, StringView type) {
	switch (getLumScriptType(type)) {
		case LumScriptType::VOID_T: out.add("ls_type_make(LS_TYPE_VOID)"); break;
		case LumScriptType::BOOL_T: out.add("ls_type_make(LS_TYPE_BOOL)"); break;
		case LumScriptType::I32_T: out.add("ls_type_make(LS_TYPE_I32)"); break;
		case LumScriptType::F32_T: out.add("ls_type_make(LS_TYPE_F32)"); break;
		case LumScriptType::VEC3_T: out.add("ls_type_make_struct(lsStringView(\"Vec3\"), -1, 0)"); break;
		case LumScriptType::DVEC3_T: out.add("ls_type_make_struct(lsStringView(\"DVec3\"), -1, 0)"); break;
		case LumScriptType::QUAT_T: out.add("ls_type_make_struct(lsStringView(\"Quat\"), -1, 0)"); break;
		case LumScriptType::ENTITY_T: out.add("nativeType(module, ls_make_qualified_name(module, lsStringView(\"entity\"), lsStringView(\"Entity\")), lsStringView(\"engine:entity/Entity\"))"); break;
		case LumScriptType::ENUM_T: out.add("ls_type_make(LS_TYPE_I32)"); break;
		case LumScriptType::PATH_T: out.add("ls_type_make(LS_TYPE_STRING)"); break;
		default: out.add("ls_type_make(LS_TYPE_INVALID)"); break;
	}
}

// Emits argument `ls_type`, with special handling for string arguments.
void appendLumScriptArgType(OutputStream& out, const Arg& arg) {
	if (isLumScriptStringArg(arg)) out.add("ls_type_make(LS_TYPE_STRING)");
	else appendLumScriptType(out, arg.type);
}

// Emits a native component handle `ls_type` expression.
void appendComponentHandleType(OutputStream& out, Component& c, const char* visible_namespace_expr) {
	out.add("nativeType(module, ls_make_qualified_name(module, ", visible_namespace_expr, ", lsStringView(\"", c.name, "\")), lsStringView(\"engine:", c.id, "/", c.name, "\"))");
}

// Emits a native array-item handle `ls_type` expression.
void appendArrayItemHandleType(OutputStream& out, Component& c, ArrayProperty& a, const char* visible_namespace_expr) {
	out.add("nativeType(module, ls_make_qualified_name(module, ", visible_namespace_expr, ", lsStringView(\"", c.name, a.name, "ArrayItem\")), lsStringView(\"engine:", c.id, "/", c.name, a.name, "ArrayItem\"))");
}

// Emits conversion code from runtime `ls_value` to native function argument.
void appendArgValue(OutputStream& out, const Arg& arg, i32 idx) {
	if (isLumScriptStringArg(arg)) {
		// TODO
		//out.add("lumscript_string_arg_", idx);
		return;
	}
	if (isLumScriptPathArg(arg)) {
		// TODO
		//out.add("Path(lumscript_string_arg_", idx, ")");
		return;
	}
	switch (getLumScriptType(arg.type)) {
		case LumScriptType::BOOL_T: out.add("ls_to_bool(runtime, ", -idx, ")"); break;
		case LumScriptType::I32_T:
			if (equal(arg.type, "u32")) out.add("ls_to_u32(runtime, ", -idx, ")");
			else out.add("ls_to_i32(runtime, ", -idx, ")");
			break;
		case LumScriptType::F32_T: out.add("ls_to_f32(runtime, ", -idx, ")"); break;
		
		// TODO
		case LumScriptType::VEC3_T: out.add("{}"); break;
		case LumScriptType::DVEC3_T: out.add("{}"); break;
		case LumScriptType::QUAT_T: out.add("{}"); break;
		case LumScriptType::ENTITY_T:
			if (equal(arg.type, "EntityRef")) out.add("{}");
			else out.add("EntityPtr{}");
			break;
			case LumScriptType::ENUM_T:
				out.add("(");
				appendEnumCPPType(out, arg.type);
				out.add(")args[", idx, "].i");
				break;
			case LumScriptType::PATH_T:
				out.add("Path()");
				break;
			default: break;
		}
	}

// Emits conversion code from native return value back to runtime `ls_value`.
// TODO
void appendReturnValue(OutputStream& out, StringView type, const char* value) {
	if (getLumScriptType(type) == LumScriptType::VOID_T) return;
	switch (getLumScriptType(type)) {
		case LumScriptType::BOOL_T:
			L("ls_push_bool(runtime, ", value, ");");
			break;
		case LumScriptType::I32_T:
			L("ls_push_i32(runtime, (i32)", value, ");");
			break;
		case LumScriptType::F32_T:
			L("ls_push_f32(runtime, ", value, ");");
			break;
		case LumScriptType::VEC3_T:
			//L("*result = makeVec3Value(", value, ");");
			break;
		case LumScriptType::DVEC3_T:
			//L("*result = makeDVec3Value(", value, ");");
			break;
		case LumScriptType::QUAT_T:
			//L("*result = makeQuatValue(", value, ");");
			break;
		case LumScriptType::ENTITY_T:
			//L("result->type = ls_type_make_native(lsStringView(\"engine:entity/Entity\"), -1, 0);");
			//L("result->i = ", value, ".index;");
			break;
			case LumScriptType::ENUM_T:
				//L("*result = ls_value_make_i32((i32)", value, ");");
				break;
			case LumScriptType::PATH_T:
				//L("static thread_local char lumscript_path_result[512];");
				//L("copyString(lumscript_path_result, ", value, ".c_str());");
				//L("*result = ls_value_make_string(lsStringView(lumscript_path_result));");
				break;
			default:
				break;
		}
}

// Returns true when return type and all arguments are wrapper-compatible.
bool isSupportedLumScriptFunction(Function& f) {
	const LumScriptType ret_type = getLumScriptType(f.return_type);
	if (ret_type == LumScriptType::UNKNOWN || ret_type == LumScriptType::ENUM_T || ret_type == LumScriptType::PATH_T) return false;
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
	L("World* world = (World*)userdata;");
	// TODO store module instead of world in the component struct
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");

	i32 string_arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isLumScriptStringArg(arg)) {
			++string_arg_idx;
			return;
		}
		// TODO
		/*L("char lumscript_string_arg_", string_arg_idx, "[128];");
		L("copyString(lumscript_string_arg_", string_arg_idx, ", args[", string_arg_idx, "].string.begin);");
		++string_arg_idx;*/
	});
	if (!equal(f.return_type, "void")) out.add("auto ret = ");
	out.add("module->", f.name, "(");
	i32 arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		appendArgValue(out, arg, arg_idx);
		++arg_idx;
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret");
	L("}" OUT_ENDL);
}

// Emits the native wrapper function body for one reflected component property accessor.
void serializeLumScriptPropertyWrapper(OutputStream& out, Module& m, Component& c, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L("(ls_runtime* runtime) {");
	L("World* world = (World*)userdata;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");

	i32 string_arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (!isLumScriptStringArg(arg) && !isLumScriptPathArg(arg)) {
			++string_arg_idx;
			return;
		}
		/*L("char lumscript_string_arg_", string_arg_idx, "[128];");
		L("copyString(lumscript_string_arg_", string_arg_idx, ", args[", string_arg_idx, "].string.begin);");
		++string_arg_idx;*/
		// TODO
	});
	if (!is_setter) out.add("auto ret = ");
	out.add("module->", is_setter ? p.setter_name : p.getter_name, "(");
	i32 arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		// TODO
		/*if (isLumScriptStringArg(arg)) out.add("lumscript_string_arg_", arg_idx);
		else if (isLumScriptPathArg(arg)) out.add("Path(lumscript_string_arg_", arg_idx, ")");
		else*/ if (getLumScriptType(arg.type) == LumScriptType::ENUM_T) {
			out.add("(");
			appendEnumCPPType(out, arg.type);
			out.add(")args[", arg_idx, "].i");
		}
		else appendArgValue(out, arg, arg_idx);
		++arg_idx;
	});
	L(");");
	if (!is_setter) appendReturnValue(out, p.type, "ret");
	L("}" OUT_ENDL);
}

// Emits registration code that exposes one property accessor wrapper to the script module.
void serializeLumScriptPropertyRegistration(OutputStream& out, Component& c, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	StringView script_name = is_setter ? p.setter_name : p.getter_name;
	out.add("functions.insert(StringView(\"", script_name, "\"), &");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L(");");
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

// Emits a native module handle ls_type expression.
void appendModuleHandleType(OutputStream& out, Module& m, const char* visible_namespace_expr) {
	out.add("nativeType(module, ls_make_qualified_name(module, ", visible_namespace_expr, ", lsStringView(\"", m.name, "\")), lsStringView(\"engine:", m.id, "/", m.name, "\"))");
}

// Emits the native wrapper function body for one reflected module method.
void serializeLumScriptModuleWrapper(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("static void ");
	appendModuleWrapperName(out, m, f, idx);
	L("(ls_runtime* runtime) {");
	L("auto* module = static_cast<", m.name, "*>(ls_to_ptr(runtime, -1));");

	i32 src_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		const i32 arg_idx = src_idx + 1;
		if (isLumScriptStringArg(arg)) {
			/*L("char lumscript_string_arg_", arg_idx, "[128];");
			L("copyString(lumscript_string_arg_", arg_idx, ", args[", arg_idx, "].string.begin);");*/
			// TODO
		}
		++src_idx;
	});

	if (!equal(f.return_type, "void")) out.add("auto ret = ");
	out.add("module->", f.name, "(");
	src_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		const i32 arg_idx = src_idx + 1;
		// TODO
		/*if (isLumScriptStringArg(arg)) out.add("lumscript_string_arg_", arg_idx);
		else*/ appendArgValue(out, arg, arg_idx);
		++src_idx;
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret");
	L("}" OUT_ENDL);
}

// Emits wrapper that returns array element count.
void serializeLumScriptArrayCountWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static void ");
	appendArrayCountWrapperName(out, c, a, idx);
	L("(ls_runtime* runtime) {");
	L("World* world = (World*)userdata;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("const i32 count = module->get", a.name, "Count(EntityRef(args[0].i));");
	L("*result = ls_value_make_i32(count);");
	L("}" OUT_ENDL);
}

// Emits wrapper that returns an array item handle from component handle + index.
void serializeLumScriptArrayItemWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static void ");
	appendArrayItemWrapperName(out, c, a, idx);
	L("(ls_runtime* runtime) {");
	L("World* world = (World*)userdata;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	out.add("const i32 count = module->get", a.name, "Count(EntityRef(args[0].i));" OUT_ENDL);
	L("if (args[1].i < 0 || args[1].i >= count) {");
	L("*result = ls_value_make_null();");
	L("return true;");
	L("}");
	out.add("result->type = ls_type_make_native(lsStringView(\"engine:", c.id, "/", c.name, a.name, "ArrayItem\"), -1, 0);" OUT_ENDL);
	L("result->i = args[0].i;");
	L("result->i64 = args[1].i;");
	L("result->ptr = world;");
	L("}" OUT_ENDL);
}

// Emits wrapper for array child getter/setter accessors.
void serializeLumScriptArrayChildWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static void ");
	appendArrayChildWrapperName(out, c, a, p, is_setter, idx);
	L("(ls_runtime* runtime) {");
	L("World* world = (World*)userdata;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");

	i32 src_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (src_idx < 2 || (!isLumScriptStringArg(arg) && !isLumScriptPathArg(arg))) {
			++src_idx;
			return;
		}
		/*L("char lumscript_string_arg_", src_idx - 1, "[128];");
		L("copyString(lumscript_string_arg_", src_idx - 1, ", args[", src_idx - 1, "].string.begin);");
		++src_idx;*/
		// TODO
	});

	if (!is_setter) out.add("auto ret = ");
	out.add("module->", is_setter ? p.setter_name : p.getter_name, "(");
	out.add("EntityRef(args[0].i), (i32)args[0].i64");
	i32 arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (arg_idx >= 2) {
			out.add(", ");
			const i32 script_idx = arg_idx - 1;
			// TODO
			/*if (isLumScriptStringArg(arg)) out.add("lumscript_string_arg_", script_idx);
			else if (isLumScriptPathArg(arg)) out.add("Path(lumscript_string_arg_", script_idx, ")");
			else*/ if (getLumScriptType(arg.type) == LumScriptType::ENUM_T) {
				out.add("(");
				appendEnumCPPType(out, arg.type);
				out.add(")args[", script_idx, "].i");
			}
			else appendArgValue(out, arg, script_idx);
		}
		++arg_idx;
	});
	L(");");
	if (!is_setter) appendReturnValue(out, p.type, "ret");
	L("}" OUT_ENDL);
}

// Emits registration code that exposes array wrappers to the script module.
void serializeLumScriptArrayRegistration(OutputStream& out, Component& c, ArrayProperty& a, i32 count_idx, i32 item_idx) {
	L("{");
	L("ls_type params[] = {");
	out.add("\t");
	appendComponentHandleType(out, c, "alias");
	out.add("," OUT_ENDL);
	L("};");
	out.add("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"", a.id, "Count\")), ls_type_make(LS_TYPE_I32), params, lengthOf(params), &");
	appendArrayCountWrapperName(out, c, a, count_idx);
	L(", world);");
	L("registered = true;");
	L("}");
	L("{");
	L("ls_type params[] = {");
	out.add("\t");
	appendComponentHandleType(out, c, "alias");
	out.add("," OUT_ENDL);
	L("\tls_type_make(LS_TYPE_I32),");
	L("};");
	out.add("ls_type return_type = ");
	appendArrayItemHandleType(out, c, a, "alias");
	out.add(";" OUT_ENDL);
	L("return_type.nullable = true;");
	out.add("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"", a.id, "\")), return_type, params, lengthOf(params), &");
	appendArrayItemWrapperName(out, c, a, item_idx);
	L(", world);");
	L("registered = true;");
	L("}");
}

// Emits registration code for array child accessor wrappers.
void serializeLumScriptArrayChildRegistration(OutputStream& out, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	StringView script_name = is_setter ? p.setter_name : p.getter_name;
	L("{");
	L("ls_type params[] = {");
	out.add("\t");
	appendArrayItemHandleType(out, c, a, "alias");
	out.add("," OUT_ENDL);
	i32 arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (arg_idx >= 2) {
			out.add("\t");
			appendLumScriptArgType(out, arg);
			out.add("," OUT_ENDL);
		}
		++arg_idx;
	});
	L("};");
	out.add("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"", script_name, "\")), ");
	if (is_setter) out.add("ls_type_make(LS_TYPE_VOID)");
	else appendLumScriptType(out, p.type);
	out.add(", params, lengthOf(params), &");
	appendArrayChildWrapperName(out, c, a, p, is_setter, idx);
	L(", world);");
	L("registered = true;");
	L("}");
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
		}
		else {
			if (startsWith(include_path, "src/")) include_path = withoutPrefix(include_path, 4);
			out.add("#include \"", include_path, "\"" OUT_ENDL);
		}
	}
	out.add(OUT_ENDL);
}

// Emits shared runtime helpers used by generated wrappers and registrations.
void emitGeneratedRuntimeHelpers(OutputStream& out) {
	L("static ls_string_view lsStringView(const char* value) { return {value, value + strlen(value)}; }");
	L("static bool equalStrings(ls_string_view value, const char* text) {");
	L("size_t len = strlen(text);");
	L("return size_t(value.end - value.begin) == len && memcmp(value.begin, text, len) == 0;");
	L("}");
	L("static bool startsWith(ls_string_view value, const char* text) {");
	L("size_t len = strlen(text);");
	L("return size_t(value.end - value.begin) >= len && memcmp(value.begin, text, len) == 0;");
	L("}");
	L("static bool empty(ls_string_view value) { return value.begin == value.end; }");
	L("static ls_string_view withoutLeft(ls_string_view value, size_t count) { return {value.begin + count, value.end}; }");
	L("static ls_string_view makeEngineName(ls_module* module, ls_string_view alias, const char* name) { return ls_make_qualified_name(module, alias, lsStringView(name)); }");
	L("static ls_string_view makeEngineName(ls_module* module, const char* alias, const char* name) { return ls_make_qualified_name(module, lsStringView(alias), lsStringView(name)); }");
	L("static ls_type nativeType(ls_module* module, ls_string_view visible_name, ls_string_view id) {");
	L("const int idx = ls_module_add_native_type(module, visible_name, id);");
	L("return ls_type_make_native(visible_name, idx, 0);");
	L("}");
	L("static ls_type nativeType(ls_module* module, ls_string_view visible_name, const char* id) {");
	L("return nativeType(module, visible_name, lsStringView(id));");
	L("}" OUT_ENDL);
}

// Emits stable wrapper symbol name for world->module accessors.
void appendWorldModuleWrapperName(OutputStream& out, Module& m) {
	out.add("lumscript_world_", m.id);
}

// Emits wrappers that expose world-level module handles, one per reflected module.
void emitGeneratedWorldModuleAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		Component& first_component = m.components[0];
		out.add("static void ");
		appendWorldModuleWrapperName(out, m);
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
			L("if (!world->hasComponent(EntityRef(entity_idx), component_type)) {");
			L("ls_push_null(runtime);");
			L("return;");
			L("}");
			L("ls_push_i32(runtime, entity_idx);");
			L("ls_push_ptr(runtime, world);");
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

// Emits registration branches for reflected module imports.
void emitGeneratedModuleImportRegistrations(OutputStream& out, MetaData& data, i32* wrapper_idx) {
	for (Module& m : data.modules) {
		bool has_supported_function = false;
		for (Function& f : m.functions) {
			if (isSupportedLumScriptFunction(f)) {
				has_supported_function = true;
				break;
			}
		}
		if (!has_supported_function) continue;
		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			out.add("functions.insert(StringView(\"core:", m.id, ".", functionScriptName(f), "\"), &");
			appendModuleWrapperName(out, m, f, *wrapper_idx);
			L(");");
			++*wrapper_idx;
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

			/*for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				out.add("functions.insert(StringView(\"", functionScriptName(f), "\"), &");
				appendWrapperName(out, c, f, *wrapper_idx);
				L(");");
				++*wrapper_idx;
			}
			for (Property& p : c.properties) {
				if (isSupportedLumScriptPropertyGetter(p)) {
					serializeLumScriptPropertyRegistration(out, c, p, false, *wrapper_idx);
					++*wrapper_idx;
				}
				if (isSupportedLumScriptPropertySetter(p)) {
					serializeLumScriptPropertyRegistration(out, c, p, true, *wrapper_idx);
					++*wrapper_idx;
				}
			}
			for (ArrayProperty& a : c.arrays) {
				serializeLumScriptArrayRegistration(out, c, a, *wrapper_idx, *wrapper_idx + 1);
				*wrapper_idx += 2;
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p)) {
						serializeLumScriptArrayChildRegistration(out, c, a, p, false, *wrapper_idx);
						++*wrapper_idx;
					}
					if (isSupportedLumScriptArrayChildSetter(p)) {
						serializeLumScriptArrayChildRegistration(out, c, a, p, true, *wrapper_idx);
						++*wrapper_idx;
					}
				}
			}*/
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
		case LumScriptType::VEC3_T: out.add("Vec3"); break;
		case LumScriptType::DVEC3_T: out.add("DVec3"); break;
		case LumScriptType::QUAT_T: out.add("Quat"); break;
		case LumScriptType::ENTITY_T: out.add("Entity"); break;
		case LumScriptType::ENUM_T: {
			const Enum* e = findEnumByTypeName(type);
			out.add(e ? e->name : type);
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

// Emits one declaration line for a reflected function in a component import context.
void emitComponentFunctionDecl(OutputStream& out, Component& c, Function& f) {
	out.add("extern fn ", functionScriptName(f), "(");
	i32 arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		out.add(arg.name, " : ");
		if (is_first && isLumScriptEntityType(arg.type)) out.add(c.name);
		else appendLumScriptDeclArgType(out, arg);
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
		out.add(arg.name, " : ");
		if (is_first && isLumScriptEntityType(arg.type)) out.add(c.name);
		else appendLumScriptDeclArgType(out, arg);
		++arg_idx;
	});
	out.add(") : ");
	if (is_setter) out.add("void");
	else appendLumScriptDeclType(out, p.type);
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
			out.add(", ", arg.name, " : ");
			appendLumScriptDeclArgType(out, arg);
		}
		++arg_idx;
	});
	out.add(") : ");
	if (is_setter) out.add("void");
	else appendLumScriptDeclType(out, p.type);
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
	auto output_enum = [&](Enum& e){
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
		StaticString<256> path("data/scripts/core/", e.name, ".lum");
		writeFile(path, out);
	};

	for (Enum& e : data.enums) output_enum(e);
	for (Module& m : data.modules) {
		for (Enum& e : m.enums) output_enum(e);
	}

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
		StaticString<256> path("data/scripts/core/", m.id ,".lum");
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
			L("import \"core:Entity\"" OUT_ENDL);
			L("struct ", c.name, " { entity : i32; world : cptr; }" OUT_ENDL);
			
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
			StaticString<256> path("data/scripts/core/", c.id, ".lum");
			writeFile(path, out);
		}
	}
}

// Entry point: generates the full LumScript C API header from metadata.
void serializeLumScriptMeta(MetaData& data) {
	g_meta_data = &data;
	OutputStream out;
	emitGeneratedHeader(out, data);
	
	L("namespace Lumix::LumScript::generated {" OUT_ENDL);

	//emitGeneratedRuntimeHelpers(out);
	emitGeneratedWorldModuleAccessors(out, data);
	emitGeneratedComponentEntityAccessors(out, data);
	i32 wrapper_idx = 0;
	emitGeneratedModuleWrappers(out, data, &wrapper_idx);
	//emitGeneratedComponentWrappers(out, data, &wrapper_idx);

	// register native functions
	L("static void registerGeneratedEngineImport(HashMap<StringView, ls_native_fn>& functions) {");
	// component getters
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("functions.insert(StringView(\"core:entity.", c.id, "\"), &lumscript_entity_", c.id, ");");
		}
	}
	// modules
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		out.add("functions.insert(StringView(\"core:world.", m.id, "\"), &");
		appendWorldModuleWrapperName(out, m);
		L(");");
	}
	wrapper_idx = 0;
	emitGeneratedModuleImportRegistrations(out, data, &wrapper_idx);
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
