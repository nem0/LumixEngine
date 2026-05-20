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
		out.add("lumscript_string_arg_", idx);
		return;
	}
	if (isLumScriptPathArg(arg)) {
		out.add("Path(lumscript_string_arg_", idx, ")");
		return;
	}
	switch (getLumScriptType(arg.type)) {
		case LumScriptType::BOOL_T: out.add("(bool)args[", idx, "].b"); break;
		case LumScriptType::I32_T:
			if (equal(arg.type, "u32")) out.add("(u32)args[", idx, "].i");
			else out.add("args[", idx, "].i");
			break;
		case LumScriptType::F32_T: out.add("args[", idx, "].f"); break;
		case LumScriptType::VEC3_T: out.add("toVec3(args[", idx, "])"); break;
		case LumScriptType::DVEC3_T: out.add("toDVec3(args[", idx, "])"); break;
		case LumScriptType::QUAT_T: out.add("toQuat(args[", idx, "])"); break;
		case LumScriptType::ENTITY_T:
			if (equal(arg.type, "EntityRef")) out.add("EntityRef(args[", idx, "].i)");
			else out.add("EntityPtr(args[", idx, "].i)");
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
void appendReturnValue(OutputStream& out, StringView type, const char* value) {
	if (getLumScriptType(type) == LumScriptType::VOID_T) return;
	L("if (result) {");
		switch (getLumScriptType(type)) {
		case LumScriptType::BOOL_T:
			L("*result = ls_value_make_bool(", value, ");");
			break;
		case LumScriptType::I32_T:
			L("*result = ls_value_make_i32((i32)", value, ");");
			break;
		case LumScriptType::F32_T:
			L("*result = ls_value_make_f32(", value, ");");
			break;
		case LumScriptType::VEC3_T:
			L("*result = makeVec3Value(", value, ");");
			break;
		case LumScriptType::DVEC3_T:
			L("*result = makeDVec3Value(", value, ");");
			break;
		case LumScriptType::QUAT_T:
			L("*result = makeQuatValue(", value, ");");
			break;
		case LumScriptType::ENTITY_T:
			L("result->type = ls_type_make_native(lsStringView(\"engine:entity/Entity\"), -1, 0);");
			L("result->i = ", value, ".index;");
			break;
			case LumScriptType::ENUM_T:
				L("*result = ls_value_make_i32((i32)", value, ");");
				break;
			case LumScriptType::PATH_T:
				L("static thread_local char lumscript_path_result[512];");
				L("copyString(lumscript_path_result, ", value, ".c_str());");
				L("*result = ls_value_make_string(lsStringView(lumscript_path_result));");
				break;
			default:
				break;
		}
	L("}");
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
	out.add("static int ");
	appendWrapperName(out, c, f, idx);
	L("(const ls_value* args, size_t arg_count, ls_value* result, void* userdata) {");
	L("(void)arg_count;");
	L("World* world = (World*)userdata;");
	L("if (!world) return false;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");

	i32 string_arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isLumScriptStringArg(arg)) {
			++string_arg_idx;
			return;
		}
		L("char lumscript_string_arg_", string_arg_idx, "[128];");
		L("copyString(lumscript_string_arg_", string_arg_idx, ", args[", string_arg_idx, "].string.begin);");
		++string_arg_idx;
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
	L("return true;");
	L("}" OUT_ENDL);
}

// Emits the native wrapper function body for one reflected component property accessor.
void serializeLumScriptPropertyWrapper(OutputStream& out, Module& m, Component& c, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static int ");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L("(const ls_value* args, size_t arg_count, ls_value* result, void* userdata) {");
	L("(void)arg_count;");
	L("World* world = (World*)userdata;");
	L("if (!world) return false;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");

	i32 string_arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (!isLumScriptStringArg(arg) && !isLumScriptPathArg(arg)) {
			++string_arg_idx;
			return;
		}
		L("char lumscript_string_arg_", string_arg_idx, "[128];");
		L("copyString(lumscript_string_arg_", string_arg_idx, ", args[", string_arg_idx, "].string.begin);");
		++string_arg_idx;
	});
	if (!is_setter) out.add("auto ret = ");
	out.add("module->", is_setter ? p.setter_name : p.getter_name, "(");
	i32 arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		if (isLumScriptStringArg(arg)) out.add("lumscript_string_arg_", arg_idx);
		else if (isLumScriptPathArg(arg)) out.add("Path(lumscript_string_arg_", arg_idx, ")");
		else if (getLumScriptType(arg.type) == LumScriptType::ENUM_T) {
			out.add("(");
			appendEnumCPPType(out, arg.type);
			out.add(")args[", arg_idx, "].i");
		}
		else appendArgValue(out, arg, arg_idx);
		++arg_idx;
	});
	L(");");
	if (!is_setter) appendReturnValue(out, p.type, "ret");
	L("return true;");
	L("}" OUT_ENDL);
}

// Emits registration code that exposes one wrapper to the script module.
void serializeLumScriptFunctionRegistration(OutputStream& out, Component& c, Function& f, i32 idx) {
	L("{");
	L("ls_type params[] = {");
	forEachArg(f.args, [&](const Arg& arg, bool first) {
		out.add("\t");
		if (first && isLumScriptEntityType(arg.type)) appendComponentHandleType(out, c, "alias");
		else appendLumScriptArgType(out, arg);
		out.add("," OUT_ENDL);
	});
	L("};");
	out.add("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"", functionScriptName(f), "\")), ");
	appendLumScriptType(out, f.return_type);
	out.add(", params, lengthOf(params), &");
	appendWrapperName(out, c, f, idx);
	L(", nullptr);");
	L("registered = true;");
	L("}");
}

// Emits registration code that exposes one property accessor wrapper to the script module.
void serializeLumScriptPropertyRegistration(OutputStream& out, Component& c, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	StringView script_name = is_setter ? p.setter_name : p.getter_name;
	L("{");
	L("ls_type params[] = {");
	forEachArg(accessor_args, [&](const Arg& arg, bool first) {
		out.add("\t");
		if (first && isLumScriptEntityType(arg.type)) appendComponentHandleType(out, c, "alias");
		else appendLumScriptArgType(out, arg);
		out.add("," OUT_ENDL);
	});
	L("};");
	out.add("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"", script_name, "\")), ");
	if (is_setter) out.add("ls_type_make(LS_TYPE_VOID)");
	else appendLumScriptType(out, p.type);
	out.add(", params, lengthOf(params), &");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L(", world);");
	L("registered = true;");
	L("}");
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
	out.add("static int ");
	appendModuleWrapperName(out, m, f, idx);
	L("(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
	L("(void)arg_count;");
	L("auto* module = static_cast<", m.name, "*>(args[0].ptr);");
	L("if (!module) return false;");

	i32 src_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		const i32 arg_idx = src_idx + 1;
		if (isLumScriptStringArg(arg)) {
			L("char lumscript_string_arg_", arg_idx, "[128];");
			L("copyString(lumscript_string_arg_", arg_idx, ", args[", arg_idx, "].string.begin);");
		}
		++src_idx;
	});

	if (!equal(f.return_type, "void")) out.add("auto ret = ");
	out.add("module->", f.name, "(");
	src_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool is_first) {
		if (!is_first) out.add(", ");
		const i32 arg_idx = src_idx + 1;
		if (isLumScriptStringArg(arg)) out.add("lumscript_string_arg_", arg_idx);
		else appendArgValue(out, arg, arg_idx);
		++src_idx;
	});
	L(");");
	appendReturnValue(out, f.return_type, "ret");
	L("return true;");
	L("}" OUT_ENDL);
}

// Emits registration code that exposes one reflected module method to LumScript.
void serializeLumScriptModuleFunctionRegistration(OutputStream& out, Module& m, Function& f, i32 idx) {
	L("{");
	L("ls_type params[] = {");
	out.add("\t");
	appendModuleHandleType(out, m, "alias");
	out.add("," OUT_ENDL);
	forEachArg(f.args, [&](const Arg& arg, bool) {
		out.add("\t");
		appendLumScriptArgType(out, arg);
		out.add("," OUT_ENDL);
	});
	L("};");
	out.add("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"", functionScriptName(f), "\")), ");
	appendLumScriptType(out, f.return_type);
	out.add(", params, lengthOf(params), &");
	appendModuleWrapperName(out, m, f, idx);
	L(", nullptr);");
	L("registered = true;");
	L("}");
}

// Emits wrapper that returns array element count.
void serializeLumScriptArrayCountWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static int ");
	appendArrayCountWrapperName(out, c, a, idx);
	L("(const ls_value* args, size_t arg_count, ls_value* result, void* userdata) {");
	L("(void)arg_count;");
	L("World* world = (World*)userdata;");
	L("if (!world || !result) return false;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");
	L("const i32 count = module->get", a.name, "Count(EntityRef(args[0].i));");
	L("*result = ls_value_make_i32(count);");
	L("return true;");
	L("}" OUT_ENDL);
}

// Emits wrapper that returns an array item handle from component handle + index.
void serializeLumScriptArrayItemWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static int ");
	appendArrayItemWrapperName(out, c, a, idx);
	L("(const ls_value* args, size_t arg_count, ls_value* result, void* userdata) {");
	L("(void)arg_count;");
	L("World* world = (World*)userdata;");
	L("if (!world || !result) return false;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");
	out.add("const i32 count = module->get", a.name, "Count(EntityRef(args[0].i));" OUT_ENDL);
	L("if (args[1].i < 0 || args[1].i >= count) {");
	L("*result = ls_value_make_null();");
	L("return true;");
	L("}");
	out.add("result->type = ls_type_make_native(lsStringView(\"engine:", c.id, "/", c.name, a.name, "ArrayItem\"), -1, 0);" OUT_ENDL);
	L("result->i = args[0].i;");
	L("result->i64 = args[1].i;");
	L("result->ptr = world;");
	L("return true;");
	L("}" OUT_ENDL);
}

// Emits wrapper for array child getter/setter accessors.
void serializeLumScriptArrayChildWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static int ");
	appendArrayChildWrapperName(out, c, a, p, is_setter, idx);
	L("(const ls_value* args, size_t arg_count, ls_value* result, void* userdata) {");
	L("(void)arg_count;");
	L("World* world = (World*)userdata;");
	L("if (!world) return false;");
	L("auto* module = static_cast<", m.name, "*>(world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");

	i32 src_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (src_idx < 2 || (!isLumScriptStringArg(arg) && !isLumScriptPathArg(arg))) {
			++src_idx;
			return;
		}
		L("char lumscript_string_arg_", src_idx - 1, "[128];");
		L("copyString(lumscript_string_arg_", src_idx - 1, ", args[", src_idx - 1, "].string.begin);");
		++src_idx;
	});

	if (!is_setter) out.add("auto ret = ");
	out.add("module->", is_setter ? p.setter_name : p.getter_name, "(");
	out.add("EntityRef(args[0].i), (i32)args[0].i64");
	i32 arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (arg_idx >= 2) {
			out.add(", ");
			const i32 script_idx = arg_idx - 1;
			if (isLumScriptStringArg(arg)) out.add("lumscript_string_arg_", script_idx);
			else if (isLumScriptPathArg(arg)) out.add("Path(lumscript_string_arg_", script_idx, ")");
			else if (getLumScriptType(arg.type) == LumScriptType::ENUM_T) {
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
	L("return true;");
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
	L("namespace Lumix::LumScript::generated {" OUT_ENDL);
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
L("}");
	L("static Vec3 toVec3(const ls_value& value) {");
	L("return Vec3(value.composite[0], value.composite[1], value.composite[2]);");
	L("}");
	L("static DVec3 toDVec3(const ls_value& value) {");
	L("return DVec3((double)value.composite[0], (double)value.composite[1], (double)value.composite[2]);");
	L("}");
	L("static Quat toQuat(const ls_value& value) {");
	L("return Quat(value.composite[0], value.composite[1], value.composite[2], value.composite[3]);");
	L("}");
	L("static ls_value makeVec3Value(const Vec3& value) {");
	L("ls_value res = ls_value_make_void();");
	L("res.type = ls_type_make_struct(lsStringView(\"Vec3\"), -1, 0);");
	L("res.composite[0] = value.x;");
	L("res.composite[1] = value.y;");
	L("res.composite[2] = value.z;");
	L("return res;");
	L("}");
	L("static ls_value makeDVec3Value(const DVec3& value) {");
	L("ls_value res = ls_value_make_void();");
	L("res.type = ls_type_make_struct(lsStringView(\"DVec3\"), -1, 0);");
	L("res.composite[0] = (float)value.x;");
	L("res.composite[1] = (float)value.y;");
	L("res.composite[2] = (float)value.z;");
	L("return res;");
	L("}");
	L("static ls_value makeQuatValue(const Quat& value) {");
	L("ls_value res = ls_value_make_void();");
	L("res.type = ls_type_make_struct(lsStringView(\"Quat\"), -1, 0);");
	L("res.composite[0] = value.x;");
	L("res.composite[1] = value.y;");
	L("res.composite[2] = value.z;");
	L("res.composite[3] = value.w;");
	L("return res;");
	L("}" OUT_ENDL);
}

// Emits built-in world/entity utility wrappers exposed by engine imports.
void emitGeneratedCoreEntityWorldBindings(OutputStream& out) {
	L("static int lumscript_world_createEntity(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || !result) return false;");
	L("EntityRef entity = world->createEntity({0, 0, 0}, Quat::IDENTITY);");
	L("result->type = ls_type_make_native(lsStringView(\"engine:entity/Entity\"), -1, 0);");
	L("result->i = entity.index;");
	L("result->ptr = world;");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_world_destroyEntity(const ls_value* args, size_t arg_count, ls_value*, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || args[1].i < 0) return false;");
	L("world->destroyEntity(EntityRef(args[1].i));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_world_hasEntity(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || !result) return false;");
	L("*result = ls_value_make_bool(args[1].i >= 0 && world->hasEntity(EntityRef(args[1].i)));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_world_findByName(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || !result) return false;");
	L("char name[128];");
	L("copyString(name, args[1].string.begin);");
	L("const EntityPtr entity = world->findByName(INVALID_ENTITY, name);");
	L("if (!entity.isValid()) {");
	L("*result = ls_value_make_null();");
	L("return true;");
	L("}");
	L("result->type = ls_type_make_native(lsStringView(\"engine:entity/Entity\"), -1, 0);");
	L("result->i = entity.index;");
	L("result->ptr = world;");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_entity_destroy(const ls_value* args, size_t arg_count, ls_value*, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;");
	L("world->destroyEntity(EntityRef(args[0].i));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_entity_isValid(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!result) return false;");
	L("*result = ls_value_make_bool(world && args[0].i >= 0 && world->hasEntity(EntityRef(args[0].i)));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_entity_setPosition(const ls_value* args, size_t arg_count, ls_value*, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;");
	L("world->setPosition(EntityRef(args[0].i), toDVec3(args[1]));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_entity_getPosition(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;");
	L("*result = makeDVec3Value(world->getPosition(EntityRef(args[0].i)));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_entity_setRotation(const ls_value* args, size_t arg_count, ls_value*, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;");
	L("world->setRotation(EntityRef(args[0].i), toQuat(args[1]));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_entity_getRotation(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;");
	L("*result = makeQuatValue(world->getRotation(EntityRef(args[0].i)));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_entity_setScale(const ls_value* args, size_t arg_count, ls_value*, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;");
	L("world->setScale(EntityRef(args[0].i), toVec3(args[1]));");
	L("return true;");
	L("}" OUT_ENDL);
	L("static int lumscript_entity_getScale(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
	L("(void)arg_count;");
	L("World* world = (World*)args[0].ptr;");
	L("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;");
	L("*result = makeVec3Value(Vec3(world->getScale(EntityRef(args[0].i))));");
	L("return true;");
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
		out.add("static int ");
		appendWorldModuleWrapperName(out, m);
		L("(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
		L("(void)arg_count;");
		L("World* world = (World*)args[0].ptr;");
		L("if (!result) return false;");
		L("if (!world) { *result = ls_value_make_null(); return true; }");
		L("IModule* module = world->getModule(reflection::getComponentType(\"", first_component.id, "\"));");
		L("if (!module) { *result = ls_value_make_null(); return true; }");
		L("result->type = ls_type_make_native(lsStringView(\"engine:", m.id, "/", m.name, "\"), -1, 0);");
		L("result->ptr = module;");
		L("result->i = 0;");
		L("return true;");
		L("}" OUT_ENDL);
	}
}

// Emits per-component entity-to-component accessor wrappers.
void emitGeneratedComponentEntityAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("static int lumscript_entity_", c.id, "(const ls_value* args, size_t arg_count, ls_value* result, void*) {");
			L("(void)arg_count;");
			L("World* world = (World*)args[0].ptr;");
			L("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;");
			L("const ComponentType component_type = reflection::getComponentType(\"", c.id, "\");");
			L("if (!world->hasComponent(EntityRef(args[0].i), component_type)) {");
			L("*result = ls_value_make_null();");
			L("return true;");
			L("}");
			L("result->type = ls_type_make_native(lsStringView(\"engine:", c.id, "/", c.name, "\"), -1, 0);");
			L("result->i = args[0].i;");
			L("result->ptr = world;");
			L("return true;");
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

// Emits one lazy enum-registration branch for a specific enum.
void emitMetaEnumBranch(OutputStream& out, Enum& e) {
	if (e.values.size == 0) return;
	L("if (equalStrings(name, \"", e.name, "\")) {");
	L("const ls_enum_member members[] = {");
	for (Enumerator& en : e.values) {
		L("\t{lsStringView(\"", en.name, "\"), ", en.value, "},");
	}
	L("};");
	L("ls_module_add_enum(module, ls_make_qualified_name(module, alias, lsStringView(\"", e.name, "\")), members, lengthOf(members));");
	L("return true;");
	L("}");
}

// Emits enum registration dispatcher for global and module-local enums.
void emitGeneratedMetaEnumRegistration(OutputStream& out, MetaData& data) {
	L("static bool registerGeneratedMetaEnum(ls_module* module, ls_string_view name, ls_string_view alias) {");
	// Merge global meta enums and module-local enums into one lookup table.
	for (Enum& e : data.enums) emitMetaEnumBranch(out, e);
	for (Module& m : data.modules) {
		for (Enum& e : m.enums) emitMetaEnumBranch(out, e);
	}
	L("return false;");
	L("}" OUT_ENDL);
}

// Emits registration branch for `engine:entity` import.
void emitGeneratedEntityImportRegistration(OutputStream& out, MetaData& data) {
	// engine:entity import registers base entity API plus nullable component accessors.
	L("if (equalStrings(name, \"entity\")) {");
	L("nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\");");
	L("{");
	L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\") };");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"destroy\")), ls_type_make(LS_TYPE_VOID), params, lengthOf(params), &lumscript_entity_destroy, nullptr);");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"isValid\")), ls_type_make(LS_TYPE_BOOL), params, lengthOf(params), &lumscript_entity_isValid, nullptr);");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"getPosition\")), ls_type_make_struct(lsStringView(\"DVec3\"), -1, 0), params, lengthOf(params), &lumscript_entity_getPosition, nullptr);");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"getRotation\")), ls_type_make_struct(lsStringView(\"Quat\"), -1, 0), params, lengthOf(params), &lumscript_entity_getRotation, nullptr);");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"getScale\")), ls_type_make_struct(lsStringView(\"Vec3\"), -1, 0), params, lengthOf(params), &lumscript_entity_getScale, nullptr);");
	L("}");
	L("{");
	L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\"), ls_type_make_struct(lsStringView(\"DVec3\"), -1, 0) };");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"setPosition\")), ls_type_make(LS_TYPE_VOID), params, lengthOf(params), &lumscript_entity_setPosition, nullptr);");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"setScale\")), ls_type_make(LS_TYPE_VOID), params, lengthOf(params), &lumscript_entity_setScale, nullptr);");
	L("}");
	L("{");
	L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\"), ls_type_make_struct(lsStringView(\"Quat\"), -1, 0) };");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"setRotation\")), ls_type_make(LS_TYPE_VOID), params, lengthOf(params), &lumscript_entity_setRotation, nullptr);");
	L("}");
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("{");
			L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\") };");
			L("ls_type return_type = nativeType(module, ls_make_qualified_name(module, lsStringView(\"", c.id, "\"), lsStringView(\"", c.name, "\")), \"engine:", c.id, "/", c.name, "\");");
			L("return_type.nullable = true;");
			L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"", c.id, "\")), return_type, params, lengthOf(params), &lumscript_entity_", c.id, ", nullptr);");
			L("}");
		}
	}
	L("return true;");
	L("}");
}

// Emits registration branch for `engine:world` import.
void emitGeneratedWorldImportRegistration(OutputStream& out, MetaData& data) {
	// engine:world import depends on entity handle type for return/argument types.
	L("if (equalStrings(name, \"world\")) {");
	L("nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\");");
	L("nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\");");
	L("{");
	L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\") };");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"createEntity\")), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\"), params, lengthOf(params), &lumscript_world_createEntity, nullptr);");
	L("}");
	L("{");
	L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\") };");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"destroyEntity\")), ls_type_make(LS_TYPE_VOID), params, lengthOf(params), &lumscript_world_destroyEntity, nullptr);");
	L("}");
	L("{");
	L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\") };");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"hasEntity\")), ls_type_make(LS_TYPE_BOOL), params, lengthOf(params), &lumscript_world_hasEntity, nullptr);");
	L("}");
	L("{");
	L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), ls_type_make(LS_TYPE_STRING) };");
	L("ls_type return_type = nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\");");
	L("return_type.nullable = true;");
	L("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"findByName\")), return_type, params, lengthOf(params), &lumscript_world_findByName, nullptr);");
	L("}");
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		L("{");
		L("ls_type params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\") };");
		L("ls_type return_type = nativeType(module, ls_make_qualified_name(module, lsStringView(\"", m.id, "\"), lsStringView(\"", m.name, "\")), \"engine:", m.id, "/", m.name, "\");");
		L("return_type.nullable = true;");
		out.add("ls_module_add_native_function(module, ls_make_qualified_name(module, alias, lsStringView(\"", m.id, "\")), return_type, params, lengthOf(params), &");
		appendWorldModuleWrapperName(out, m);
		L(", nullptr);");
		L("}");
	}
	L("return true;");
	L("}");
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
		L("if (equalStrings(name, \"", m.id, "\")) {");
		out.add("nativeType(module, makeEngineName(module, alias, \"", m.name, "\"), \"engine:", m.id, "/", m.name, "\");" OUT_ENDL);
		L("bool registered = false;");
		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			serializeLumScriptModuleFunctionRegistration(out, m, f, *wrapper_idx);
			++*wrapper_idx;
		}
		L("return registered;");
		L("}");
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
			// Wrapper indices must match previously emitted wrapper function order.
			L("if (equalStrings(name, \"", c.id, "\")) {");
			L("nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\");");
			L("nativeType(module, makeEngineName(module, alias, \"", c.name, "\"), \"engine:", c.id, "/", c.name, "\");");
			L("bool registered = false;");
			for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				serializeLumScriptFunctionRegistration(out, c, f, *wrapper_idx);
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
			}
			L("return registered;");
			L("}");
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
	out.add("fn ", functionScriptName(f), "(");
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
	out.add("fn ", script_name, "(");
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
	out.add("fn ", script_name, "(");
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

// Emits global and module-local enum declarations into engine.lum.
void emitLumScriptReferenceEnums(OutputStream& out, MetaData& data) {
	for (Enum& e : data.enums) emitLumScriptReferenceEnum(out, e);
	for (Module& m : data.modules) {
		for (Enum& e : m.enums) emitLumScriptReferenceEnum(out, e);
	}
}

// Emits declaration-only reference LumScript file for engine imports.
void serializeLumScriptReference(MetaData& data) {
	OutputStream out;
	out.add("// Generated by meta.cpp" OUT_ENDL);
	out.add("// Reference-only declarations for engine imports." OUT_ENDL OUT_ENDL);
	emitLumScriptReferenceEnums(out, data);

	L("// import engine:entity as entity");
	L("fn destroy(e : Entity) : void;");
	L("fn isValid(e : Entity) : bool;");
	L("fn getPosition(e : Entity) : DVec3;");
	L("fn getRotation(e : Entity) : Quat;");
	L("fn getScale(e : Entity) : Vec3;");
	L("fn setPosition(e : Entity, position : DVec3) : void;");
	L("fn setScale(e : Entity, scale : Vec3) : void;");
	L("fn setRotation(e : Entity, rotation : Quat) : void;");
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			L("fn ", c.id, "(arg0 : Entity) : ?", c.name, ";");
		}
	}
	out.add(OUT_ENDL);

	L("// import engine:world as world");
	L("fn createEntity(w : World) : Entity;");
	L("fn destroyEntity(w : World, e : Entity) : void;");
	L("fn hasEntity(w : World, e : Entity) : bool;");
	L("fn findByName(w : World, name : string) : ?Entity;");
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		L("fn ", m.id, "(w : World) : ?", m.id, ".", m.name, ";");
	}
	out.add(OUT_ENDL);
	for (Module& m : data.modules) {
		bool has_supported_function = false;
		for (Function& f : m.functions) {
			if (isSupportedLumScriptFunction(f)) {
				has_supported_function = true;
				break;
			}
		}
		if (!has_supported_function) continue;
		L("// import engine:", m.id, " as ", m.id);
		for (Function& f : m.functions) {
			if (!isSupportedLumScriptFunction(f)) continue;
			out.add("fn ", functionScriptName(f), "(module : ", m.name);
			forEachArg(f.args, [&](const Arg& arg, bool) {
				out.add(", ", arg.name, " : ");
				appendLumScriptDeclArgType(out, arg);
			});
			out.add(") : ");
			appendLumScriptDeclType(out, f.return_type);
			out.add(";" OUT_ENDL);
		}
		out.add(OUT_ENDL);
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
			L("// import engine:", c.id, " as ", c.id);
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
				L("fn ", a.id, "Count(component : ", c.name, ") : i32;");
				L("fn ", a.id, "(component : ", c.name, ", idx : i32) : ?", c.name, a.name, "ArrayItem;");
				for (Property& p : a.children) {
					if (isSupportedLumScriptArrayChildGetter(p)) emitArrayChildPropertyDecl(out, c, a, p, false);
					if (isSupportedLumScriptArrayChildSetter(p)) emitArrayChildPropertyDecl(out, c, a, p, true);
				}
			}
			out.add(OUT_ENDL);
		}
	}

	writeFile("data/scripts/core/engine.lum", out);
}

// Entry point: generates the full LumScript C API header from metadata.
void serializeLumScriptMeta(MetaData& data) {
	g_meta_data = &data;
	OutputStream out;
	emitGeneratedHeader(out, data);
	emitGeneratedRuntimeHelpers(out);
	emitGeneratedCoreEntityWorldBindings(out);
	emitGeneratedWorldModuleAccessors(out, data);
	emitGeneratedComponentEntityAccessors(out, data);
	i32 wrapper_idx = 0;
	emitGeneratedModuleWrappers(out, data, &wrapper_idx);
	emitGeneratedComponentWrappers(out, data, &wrapper_idx);
	emitGeneratedMetaEnumRegistration(out, data);

	L("static bool registerGeneratedEngineImport(ls_module* module, World* world, ls_string_view path, ls_string_view alias) {");
	L("if (!startsWith(path, \"engine:\")) return false;");
	L("ls_string_view name = withoutLeft(path, 7);");
	L("if (registerGeneratedMetaEnum(module, name, alias)) return true;");
	L("if (empty(alias)) return false;");
	emitGeneratedEntityImportRegistration(out, data);
	emitGeneratedWorldImportRegistration(out, data);
	wrapper_idx = 0;
	emitGeneratedModuleImportRegistrations(out, data, &wrapper_idx);
	emitGeneratedComponentImportRegistrations(out, data, &wrapper_idx);

	L("return false;");
	L("}" OUT_ENDL);
	L("} // namespace Lumix::LumScript::generated");
	formatCPP(out);
	writeFile("src/lumscript/lumscript_capi.gen.h", out);
	serializeLumScriptReference(data);
	g_meta_data = nullptr;
}

} // anonymous namespace

META_PLUGIN(serializeLumScriptMeta)
