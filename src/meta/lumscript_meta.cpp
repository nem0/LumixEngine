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

// Emits generated code for a LumScript `TypeRef` expression.
void appendLumScriptType(OutputStream& out, StringView type) {
	switch (getLumScriptType(type)) {
		case LumScriptType::VOID_T: out.add("TypeRef(TypeRef::VOID)"); break;
		case LumScriptType::BOOL_T: out.add("TypeRef(TypeRef::BOOL)"); break;
		case LumScriptType::I32_T: out.add("TypeRef(TypeRef::I32)"); break;
		case LumScriptType::F32_T: out.add("TypeRef(TypeRef::F32)"); break;
		case LumScriptType::VEC3_T: out.add("TypeRef(TypeRef::STRUCT, \"Vec3\", -1)"); break;
		case LumScriptType::DVEC3_T: out.add("TypeRef(TypeRef::STRUCT, \"DVec3\", -1)"); break;
			case LumScriptType::QUAT_T: out.add("TypeRef(TypeRef::STRUCT, \"Quat\", -1)"); break;
			case LumScriptType::ENTITY_T: out.add("nativeType(module, module.makeQualifiedName(\"entity\", \"Entity\"), \"engine:entity/Entity\")"); break;
			case LumScriptType::ENUM_T: out.add("TypeRef(TypeRef::I32)"); break;
			case LumScriptType::PATH_T: out.add("TypeRef(TypeRef::STRING)"); break;
			default: out.add("TypeRef()"); break;
		}
	}

// Emits argument `TypeRef`, with special handling for string arguments.
void appendLumScriptArgType(OutputStream& out, const Arg& arg) {
	if (isLumScriptStringArg(arg)) out.add("TypeRef(TypeRef::STRING)");
	else appendLumScriptType(out, arg.type);
}

// Emits a native component handle `TypeRef` expression.
void appendComponentHandleType(OutputStream& out, Component& c, const char* visible_namespace_expr) {
	out.add("nativeType(module, module.makeQualifiedName(", visible_namespace_expr, ", \"", c.name, "\"), \"engine:", c.id, "/", c.name, "\")");
}

// Emits a native array-item handle `TypeRef` expression.
void appendArrayItemHandleType(OutputStream& out, Component& c, ArrayProperty& a, const char* visible_namespace_expr) {
	out.add("nativeType(module, module.makeQualifiedName(", visible_namespace_expr, ", \"", c.name, a.name, "ArrayItem\"), \"engine:", c.id, "/", c.name, a.name, "ArrayItem\")");
}

// Emits conversion code from runtime `Value` to native function argument.
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
		case LumScriptType::BOOL_T: out.add("args[", idx, "].b"); break;
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

// Emits conversion code from native return value back to runtime `Value`.
void appendReturnValue(OutputStream& out, StringView type, const char* value) {
	if (getLumScriptType(type) == LumScriptType::VOID_T) return;
	L("if (result) {");
	switch (getLumScriptType(type)) {
		case LumScriptType::BOOL_T:
			L("result->type = TypeRef(TypeRef::BOOL);");
			L("result->b = ", value, ";");
			break;
		case LumScriptType::I32_T:
			L("result->type = TypeRef(TypeRef::I32);");
			L("result->i = (i32)", value, ";");
			L("result->f = (float)result->i;");
			break;
		case LumScriptType::F32_T:
			L("result->type = TypeRef(TypeRef::F32);");
			L("result->f = ", value, ";");
			L("result->i = (i32)result->f;");
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
			L("result->type = TypeRef(TypeRef::NATIVE, \"engine:entity/Entity\", -1);");
			L("result->i = ", value, ".index;");
			break;
			case LumScriptType::ENUM_T:
				L("result->type = TypeRef(TypeRef::I32);");
				L("result->i = (i32)", value, ";");
				L("result->f = (float)result->i;");
				break;
			case LumScriptType::PATH_T:
				L("static thread_local char lumscript_path_result[512];");
				L("copyString(lumscript_path_result, ", value, ".c_str());");
				L("*result = Runtime::makeString(lumscript_path_result);");
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
	out.add("static bool ");
	appendWrapperName(out, c, f, idx);
	L("(Span<const Value> args, Value* result, void* userdata) {");
	L("GeneratedEngineContext* ctx = (GeneratedEngineContext*)userdata;");
	L("if (!ctx->world) return false;");
	L("auto* module = static_cast<", m.name, "*>(ctx->world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");

	i32 string_arg_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isLumScriptStringArg(arg)) {
			++string_arg_idx;
			return;
		}
		L("char lumscript_string_arg_", string_arg_idx, "[128];");
		L("copyString(lumscript_string_arg_", string_arg_idx, ", args[", string_arg_idx, "].string);");
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
	out.add("static bool ");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L("(Span<const Value> args, Value* result, void* userdata) {");
	L("GeneratedEngineContext* ctx = (GeneratedEngineContext*)userdata;");
	L("if (!ctx->world) return false;");
	L("auto* module = static_cast<", m.name, "*>(ctx->world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");

	i32 string_arg_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (!isLumScriptStringArg(arg) && !isLumScriptPathArg(arg)) {
			++string_arg_idx;
			return;
		}
		L("char lumscript_string_arg_", string_arg_idx, "[128];");
		L("copyString(lumscript_string_arg_", string_arg_idx, ", args[", string_arg_idx, "].string);");
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
	L("TypeRef params[] = {");
	forEachArg(f.args, [&](const Arg& arg, bool first) {
		out.add("\t");
		if (first && isLumScriptEntityType(arg.type)) appendComponentHandleType(out, c, "alias");
		else appendLumScriptArgType(out, arg);
		out.add("," OUT_ENDL);
	});
	L("};");
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"", functionScriptName(f), "\"), ");
	appendLumScriptType(out, f.return_type);
	out.add(", Span<const TypeRef>(params, lengthOf(params)), &");
	appendWrapperName(out, c, f, idx);
	L(", makeContext(module, world));");
	L("registered = true;");
	L("}");
}

// Emits registration code that exposes one property accessor wrapper to the script module.
void serializeLumScriptPropertyRegistration(OutputStream& out, Component& c, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	StringView script_name = is_setter ? p.setter_name : p.getter_name;
	L("{");
	L("TypeRef params[] = {");
	forEachArg(accessor_args, [&](const Arg& arg, bool first) {
		out.add("\t");
		if (first && isLumScriptEntityType(arg.type)) appendComponentHandleType(out, c, "alias");
		else appendLumScriptArgType(out, arg);
		out.add("," OUT_ENDL);
	});
	L("};");
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"", script_name, "\"), ");
	if (is_setter) out.add("TypeRef(TypeRef::VOID)");
	else appendLumScriptType(out, p.type);
	out.add(", Span<const TypeRef>(params, lengthOf(params)), &");
	appendPropertyWrapperName(out, c, p, is_setter, idx);
	L(", makeContext(module, world));");
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

// Emits a native module handle TypeRef expression.
void appendModuleHandleType(OutputStream& out, Module& m, const char* visible_namespace_expr) {
	out.add("nativeType(module, module.makeQualifiedName(", visible_namespace_expr, ", \"", m.name, "\"), \"engine:", m.id, "/", m.name, "\")");
}

// Emits the native wrapper function body for one reflected module method.
void serializeLumScriptModuleWrapper(OutputStream& out, Module& m, Function& f, i32 idx) {
	out.add("static bool ");
	appendModuleWrapperName(out, m, f, idx);
	L("(Span<const Value> args, Value* result, void*) {");
	L("auto* module = static_cast<", m.name, "*>(args[0].ptr);");
	L("if (!module) return false;");

	i32 src_idx = 0;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		const i32 arg_idx = src_idx + 1;
		if (isLumScriptStringArg(arg)) {
			L("char lumscript_string_arg_", arg_idx, "[128];");
			L("copyString(lumscript_string_arg_", arg_idx, ", args[", arg_idx, "].string);");
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
	L("TypeRef params[] = {");
	out.add("\t");
	appendModuleHandleType(out, m, "alias");
	out.add("," OUT_ENDL);
	forEachArg(f.args, [&](const Arg& arg, bool) {
		out.add("\t");
		appendLumScriptArgType(out, arg);
		out.add("," OUT_ENDL);
	});
	L("};");
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"", functionScriptName(f), "\"), ");
	appendLumScriptType(out, f.return_type);
	out.add(", Span<const TypeRef>(params, lengthOf(params)), &");
	appendModuleWrapperName(out, m, f, idx);
	L(");");
	L("registered = true;");
	L("}");
}

// Emits wrapper that returns array element count.
void serializeLumScriptArrayCountWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static bool ");
	appendArrayCountWrapperName(out, c, a, idx);
	L("(Span<const Value> args, Value* result, void* userdata) {");
	L("GeneratedEngineContext* ctx = (GeneratedEngineContext*)userdata;");
	L("if (!ctx->world || !result) return false;");
	L("auto* module = static_cast<", m.name, "*>(ctx->world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");
	out.add("const i32 count = module->get", a.name, "Count(EntityRef(args[0].i));" OUT_ENDL);
	L("result->type = TypeRef(TypeRef::I32);");
	L("result->i = count;");
	L("result->f = (float)count;");
	L("return true;");
	L("}" OUT_ENDL);
}

// Emits wrapper that returns an array item handle from component handle + index.
void serializeLumScriptArrayItemWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, i32 idx) {
	out.add("static bool ");
	appendArrayItemWrapperName(out, c, a, idx);
	L("(Span<const Value> args, Value* result, void* userdata) {");
	L("GeneratedEngineContext* ctx = (GeneratedEngineContext*)userdata;");
	L("if (!ctx->world || !result) return false;");
	L("auto* module = static_cast<", m.name, "*>(ctx->world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");
	out.add("const i32 count = module->get", a.name, "Count(EntityRef(args[0].i));" OUT_ENDL);
	L("if (args[1].i < 0 || args[1].i >= count) {");
	L("*result = Runtime::makeNull();");
	L("return true;");
	L("}");
	out.add("result->type = TypeRef(TypeRef::NATIVE, \"engine:", c.id, "/", c.name, a.name, "ArrayItem\", -1);" OUT_ENDL);
	L("result->i = args[0].i;");
	L("result->i64 = args[1].i;");
	L("result->ptr = ctx->world;");
	L("return true;");
	L("}" OUT_ENDL);
}

// Emits wrapper for array child getter/setter accessors.
void serializeLumScriptArrayChildWrapper(OutputStream& out, Module& m, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	out.add("static bool ");
	appendArrayChildWrapperName(out, c, a, p, is_setter, idx);
	L("(Span<const Value> args, Value* result, void* userdata) {");
	L("GeneratedEngineContext* ctx = (GeneratedEngineContext*)userdata;");
	L("if (!ctx->world) return false;");
	L("auto* module = static_cast<", m.name, "*>(ctx->world->getModule(reflection::getComponentType(\"", c.id, "\")));");
	L("if (!module) return false;");

	i32 src_idx = 0;
	forEachArg(accessor_args, [&](const Arg& arg, bool) {
		if (src_idx < 2 || (!isLumScriptStringArg(arg) && !isLumScriptPathArg(arg))) {
			++src_idx;
			return;
		}
		L("char lumscript_string_arg_", src_idx - 1, "[128];");
		L("copyString(lumscript_string_arg_", src_idx - 1, ", args[", src_idx - 1, "].string);");
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
	L("TypeRef params[] = {");
	out.add("\t");
	appendComponentHandleType(out, c, "alias");
	out.add("," OUT_ENDL);
	L("};");
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"", a.id, "Count\"), TypeRef(TypeRef::I32), Span<const TypeRef>(params, lengthOf(params)), &");
	appendArrayCountWrapperName(out, c, a, count_idx);
	L(", makeContext(module, world));");
	L("registered = true;");
	L("}");
	L("{");
	L("TypeRef params[] = {");
	out.add("\t");
	appendComponentHandleType(out, c, "alias");
	out.add("," OUT_ENDL);
	L("\tTypeRef(TypeRef::I32),");
	L("};");
	out.add("TypeRef return_type = ");
	appendArrayItemHandleType(out, c, a, "alias");
	out.add(";" OUT_ENDL);
	L("return_type.nullable = true;");
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"", a.id, "\"), return_type, Span<const TypeRef>(params, lengthOf(params)), &");
	appendArrayItemWrapperName(out, c, a, item_idx);
	L(", makeContext(module, world));");
	L("registered = true;");
	L("}");
}

// Emits registration code for array child accessor wrappers.
void serializeLumScriptArrayChildRegistration(OutputStream& out, Component& c, ArrayProperty& a, Property& p, bool is_setter, i32 idx) {
	StringView accessor_args = is_setter ? p.setter_args : p.getter_args;
	StringView script_name = is_setter ? p.setter_name : p.getter_name;
	L("{");
	L("TypeRef params[] = {");
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
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"", script_name, "\"), ");
	if (is_setter) out.add("TypeRef(TypeRef::VOID)");
	else appendLumScriptType(out, p.type);
	out.add(", Span<const TypeRef>(params, lengthOf(params)), &");
	appendArrayChildWrapperName(out, c, a, p, is_setter, idx);
	L(", makeContext(module, world));");
	L("registered = true;");
	L("}");
}

// Emits generated file preamble and includes needed by produced bindings.
void emitGeneratedHeader(OutputStream& out, MetaData& data) {
	out.add("// Generated by meta.cpp" OUT_ENDL OUT_ENDL);
	out.add("#pragma once" OUT_ENDL OUT_ENDL);
	out.add("#include \"lumscript/ast.h\"" OUT_ENDL);
	out.add("#include \"lumscript/runtime.h\"" OUT_ENDL);
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
	out.add("namespace Lumix::LumScript::generated {" OUT_ENDL OUT_ENDL);
	out.add("struct GeneratedEngineContext { World* world; };" OUT_ENDL OUT_ENDL);
	out.add("static StringView makeEngineName(Module& module, StringView alias, const char* name) { return module.makeQualifiedName(alias, name); }" OUT_ENDL);
	out.add("static i32 addNativeType(Module& module, StringView name, StringView id) {" OUT_ENDL);
	out.add("for (i32 i = 0; i < module.native_types.size(); ++i) if (equalStrings(module.native_types[i].name, name)) return i;" OUT_ENDL);
	out.add("NativeTypeDecl& type = module.native_types.emplace();" OUT_ENDL);
	out.add("type.name = module.copyName(name);" OUT_ENDL);
	out.add("type.id = module.copyName(id);" OUT_ENDL);
	out.add("return module.native_types.size() - 1;" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static TypeRef nativeType(Module& module, StringView visible_name, StringView id) {" OUT_ENDL);
	out.add("const i32 idx = addNativeType(module, visible_name, id);" OUT_ENDL);
	out.add("return TypeRef(TypeRef::NATIVE, module.native_types[idx].id, idx);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static GeneratedEngineContext* makeContext(Module& module, World* world) {" OUT_ENDL);
	out.add("void* mem = module.allocator.allocate(sizeof(GeneratedEngineContext), alignof(GeneratedEngineContext));" OUT_ENDL);
	out.add("module.allocated_native_data.push(mem);" OUT_ENDL);
	out.add("GeneratedEngineContext* ctx = new (NewPlaceholder(), mem) GeneratedEngineContext;" OUT_ENDL);
	out.add("ctx->world = world;" OUT_ENDL);
	out.add("return ctx;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static Vec3 toVec3(const Value& value) {" OUT_ENDL);
	out.add("return Vec3(value.composite[0], value.composite[1], value.composite[2]);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static DVec3 toDVec3(const Value& value) {" OUT_ENDL);
	out.add("return DVec3((double)value.composite[0], (double)value.composite[1], (double)value.composite[2]);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static Quat toQuat(const Value& value) {" OUT_ENDL);
	out.add("return Quat(value.composite[0], value.composite[1], value.composite[2], value.composite[3]);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static Value makeVec3Value(const Vec3& value) {" OUT_ENDL);
	out.add("return Runtime::makeVec3(TypeRef(TypeRef::STRUCT, \"Vec3\", -1), value.x, value.y, value.z);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static Value makeDVec3Value(const DVec3& value) {" OUT_ENDL);
	out.add("return Runtime::makeVec3(TypeRef(TypeRef::STRUCT, \"DVec3\", -1), (float)value.x, (float)value.y, (float)value.z);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static Value makeQuatValue(const Quat& value) {" OUT_ENDL);
	out.add("return Runtime::makeQuat(TypeRef(TypeRef::STRUCT, \"Quat\", -1), value.x, value.y, value.z, value.w);" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
}

// Emits built-in world/entity utility wrappers exposed by engine imports.
void emitGeneratedCoreEntityWorldBindings(OutputStream& out) {
	out.add("static bool lumscript_world_createEntity(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result) return false;" OUT_ENDL);
	out.add("EntityRef entity = world->createEntity({0, 0, 0}, Quat::IDENTITY);" OUT_ENDL);
	out.add("result->type = TypeRef(TypeRef::NATIVE, \"engine:entity/Entity\", -1);" OUT_ENDL);
	out.add("result->i = entity.index;" OUT_ENDL);
	out.add("result->ptr = world;" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_world_destroyEntity(Span<const Value> args, Value*, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || args[1].i < 0) return false;" OUT_ENDL);
	out.add("world->destroyEntity(EntityRef(args[1].i));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_world_hasEntity(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result) return false;" OUT_ENDL);
	out.add("result->type = TypeRef(TypeRef::BOOL);" OUT_ENDL);
	out.add("result->b = args[1].i >= 0 && world->hasEntity(EntityRef(args[1].i));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_world_findByName(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result) return false;" OUT_ENDL);
	out.add("char name[128];" OUT_ENDL);
	out.add("copyString(name, args[1].string);" OUT_ENDL);
	out.add("const EntityPtr entity = world->findByName(INVALID_ENTITY, name);" OUT_ENDL);
	out.add("if (!entity.isValid()) {" OUT_ENDL);
	out.add("*result = Runtime::makeNull();" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("result->type = TypeRef(TypeRef::NATIVE, \"engine:entity/Entity\", -1);" OUT_ENDL);
	out.add("result->i = entity.index;" OUT_ENDL);
	out.add("result->ptr = world;" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_destroy(Span<const Value> args, Value*, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
	out.add("world->destroyEntity(EntityRef(args[0].i));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_isValid(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!result) return false;" OUT_ENDL);
	out.add("result->type = TypeRef(TypeRef::BOOL);" OUT_ENDL);
	out.add("result->b = world && args[0].i >= 0 && world->hasEntity(EntityRef(args[0].i));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_setPosition(Span<const Value> args, Value*, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
	out.add("world->setPosition(EntityRef(args[0].i), toDVec3(args[1]));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_getPosition(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
	out.add("*result = makeDVec3Value(world->getPosition(EntityRef(args[0].i)));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_setRotation(Span<const Value> args, Value*, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
	out.add("world->setRotation(EntityRef(args[0].i), toQuat(args[1]));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_getRotation(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
	out.add("*result = makeQuatValue(world->getRotation(EntityRef(args[0].i)));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_setScale(Span<const Value> args, Value*, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
	out.add("world->setScale(EntityRef(args[0].i), toVec3(args[1]));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_getScale(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
	out.add("*result = makeVec3Value(Vec3(world->getScale(EntityRef(args[0].i))));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
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
		out.add("static bool ");
		appendWorldModuleWrapperName(out, m);
		L("(Span<const Value> args, Value* result, void*) {");
		out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
		out.add("if (!result) return false;" OUT_ENDL);
		out.add("if (!world) { *result = Runtime::makeNull(); return true; }" OUT_ENDL);
		out.add("IModule* module = world->getModule(reflection::getComponentType(\"", first_component.id, "\"));" OUT_ENDL);
		out.add("if (!module) { *result = Runtime::makeNull(); return true; }" OUT_ENDL);
		out.add("result->type = TypeRef(TypeRef::NATIVE, \"engine:", m.id, "/", m.name, "\", -1);" OUT_ENDL);
		out.add("result->ptr = module;" OUT_ENDL);
		out.add("result->i = 0;" OUT_ENDL);
		out.add("return true;" OUT_ENDL);
		out.add("}" OUT_ENDL OUT_ENDL);
	}
}

// Emits per-component entity-to-component accessor wrappers.
void emitGeneratedComponentEntityAccessors(OutputStream& out, MetaData& data) {
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			out.add("static bool lumscript_entity_", c.id, "(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
			out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
			out.add("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
			out.add("const ComponentType component_type = reflection::getComponentType(\"", c.id, "\");" OUT_ENDL);
			out.add("if (!world->hasComponent(EntityRef(args[0].i), component_type)) {" OUT_ENDL);
			out.add("*result = Runtime::makeNull();" OUT_ENDL);
			out.add("return true;" OUT_ENDL);
			out.add("}" OUT_ENDL);
			out.add("result->type = TypeRef(TypeRef::NATIVE, \"engine:", c.id, "/", c.name, "\", -1);" OUT_ENDL);
			out.add("result->i = args[0].i;" OUT_ENDL);
			out.add("result->ptr = world;" OUT_ENDL);
			out.add("return true;" OUT_ENDL);
			out.add("}" OUT_ENDL OUT_ENDL);
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
	// Emit one if-branch for lazy enum registration by imported name.
	out.add("if (equalStrings(name, \"", e.name, "\")) {" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("StringView enum_name = module.makeQualifiedName(alias, \"", e.name, "\");" OUT_ENDL);
	out.add("if (!hasEnum(module, enum_name)) {" OUT_ENDL);
	out.add("EnumDecl& e = module.enums.emplace(module.allocator);" OUT_ENDL);
	out.add("e.name = enum_name;" OUT_ENDL);
	for (Enumerator& en : e.values) {
		out.add("{ EnumMember& member = e.members.emplace(); member.name = \"", en.name, "\"; member.value = ", en.value, "; }" OUT_ENDL);
	}
	out.add("}" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL);
}

// Emits enum registration dispatcher for global and module-local enums.
void emitGeneratedMetaEnumRegistration(OutputStream& out, MetaData& data) {
	out.add("static bool hasEnum(Module& module, StringView name) {" OUT_ENDL);
	out.add("for (EnumDecl& e : module.enums) if (equalStrings(e.name, name)) return true;" OUT_ENDL);
	out.add("return false;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool registerGeneratedMetaEnum(Module& module, StringView name, StringView alias) {" OUT_ENDL);
	// Merge global meta enums and module-local enums into one lookup table.
	for (Enum& e : data.enums) emitMetaEnumBranch(out, e);
	for (Module& m : data.modules) {
		for (Enum& e : m.enums) emitMetaEnumBranch(out, e);
	}
	out.add("return false;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
}

// Emits registration branch for `engine:entity` import.
void emitGeneratedEntityImportRegistration(OutputStream& out, MetaData& data) {
	// engine:entity import registers base entity API plus nullable component accessors.
	out.add("if (equalStrings(name, \"entity\")) {" OUT_ENDL);
	out.add("nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\");" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\") };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"destroy\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_destroy);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"isValid\"), TypeRef(TypeRef::BOOL), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_isValid);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getPosition\"), TypeRef(TypeRef::STRUCT, \"DVec3\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_getPosition);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getRotation\"), TypeRef(TypeRef::STRUCT, \"Quat\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_getRotation);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getScale\"), TypeRef(TypeRef::STRUCT, \"Vec3\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_getScale);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\"), TypeRef(TypeRef::STRUCT, \"DVec3\", -1) };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"setPosition\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_setPosition);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"setScale\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_setScale);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\"), TypeRef(TypeRef::STRUCT, \"Quat\", -1) };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"setRotation\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_setRotation);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			out.add("{" OUT_ENDL);
			out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\") };" OUT_ENDL);
			out.add("TypeRef return_type = nativeType(module, module.makeQualifiedName(\"", c.id, "\", \"", c.name, "\"), \"engine:", c.id, "/", c.name, "\");" OUT_ENDL);
			out.add("return_type.nullable = true;" OUT_ENDL);
			out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"", c.id, "\"), return_type, Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_", c.id, ");" OUT_ENDL);
			out.add("}" OUT_ENDL);
		}
	}
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL);
}

// Emits registration branch for `engine:world` import.
void emitGeneratedWorldImportRegistration(OutputStream& out, MetaData& data) {
	// engine:world import depends on entity handle type for return/argument types.
	out.add("if (equalStrings(name, \"world\")) {" OUT_ENDL);
	out.add("nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\");" OUT_ENDL);
	out.add("nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\");" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\") };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"createEntity\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\"), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_createEntity);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\") };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"destroyEntity\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_destroyEntity);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\") };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"hasEntity\"), TypeRef(TypeRef::BOOL), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_hasEntity);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), TypeRef(TypeRef::STRING) };" OUT_ENDL);
	out.add("TypeRef return_type = nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\");" OUT_ENDL);
	out.add("return_type.nullable = true;" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"findByName\"), return_type, Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_findByName);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		out.add("{" OUT_ENDL);
		out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\") };" OUT_ENDL);
		out.add("TypeRef return_type = nativeType(module, module.makeQualifiedName(\"", m.id, "\", \"", m.name, "\"), \"engine:", m.id, "/", m.name, "\");" OUT_ENDL);
		out.add("return_type.nullable = true;" OUT_ENDL);
		out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"", m.id, "\"), return_type, Span<const TypeRef>(params, lengthOf(params)), &");
		appendWorldModuleWrapperName(out, m);
		out.add(");" OUT_ENDL);
		out.add("}" OUT_ENDL);
	}
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL);
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

	out.add("// import engine:entity as entity" OUT_ENDL);
	out.add("fn destroy(e : Entity) : void;" OUT_ENDL);
	out.add("fn isValid(e : Entity) : bool;" OUT_ENDL);
	out.add("fn getPosition(e : Entity) : DVec3;" OUT_ENDL);
	out.add("fn getRotation(e : Entity) : Quat;" OUT_ENDL);
	out.add("fn getScale(e : Entity) : Vec3;" OUT_ENDL);
	out.add("fn setPosition(e : Entity, position : DVec3) : void;" OUT_ENDL);
	out.add("fn setScale(e : Entity, scale : Vec3) : void;" OUT_ENDL);
	out.add("fn setRotation(e : Entity, rotation : Quat) : void;" OUT_ENDL);
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			out.add("fn ", c.id, "(arg0 : Entity) : ?", c.name, ";" OUT_ENDL);
		}
	}
	out.add(OUT_ENDL);

	out.add("// import engine:world as world" OUT_ENDL);
	out.add("fn createEntity(w : World) : Entity;" OUT_ENDL);
	out.add("fn destroyEntity(w : World, e : Entity) : void;" OUT_ENDL);
	out.add("fn hasEntity(w : World, e : Entity) : bool;" OUT_ENDL);
	out.add("fn findByName(w : World, name : string) : ?Entity;" OUT_ENDL);
	for (Module& m : data.modules) {
		if (m.components.size == 0) continue;
		out.add("fn ", m.id, "(w : World) : ?", m.id, ".", m.name, ";" OUT_ENDL);
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
		out.add("// import engine:", m.id, " as ", m.id, OUT_ENDL);
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
			out.add("// import engine:", c.id, " as ", c.id, OUT_ENDL);
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
				out.add("fn ", a.id, "Count(component : ", c.name, ") : i32;" OUT_ENDL);
				out.add("fn ", a.id, "(component : ", c.name, ", idx : i32) : ?", c.name, a.name, "ArrayItem;" OUT_ENDL);
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

	out.add("static bool registerGeneratedEngineImport(Module& module, World* world, StringView path, StringView alias) {" OUT_ENDL);
	out.add("if (!startsWith(path, \"engine:\")) return false;" OUT_ENDL);
	out.add("StringView name = path.withoutLeft(7);" OUT_ENDL);
	out.add("if (registerGeneratedMetaEnum(module, name, alias)) return true;" OUT_ENDL);
	out.add("if (alias.empty()) return false;" OUT_ENDL);
	emitGeneratedEntityImportRegistration(out, data);
	emitGeneratedWorldImportRegistration(out, data);
	wrapper_idx = 0;
	emitGeneratedModuleImportRegistrations(out, data, &wrapper_idx);
	emitGeneratedComponentImportRegistrations(out, data, &wrapper_idx);

	out.add("return false;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("} // namespace Lumix::LumScript::generated" OUT_ENDL);
	formatCPP(out);
	writeFile("src/lumscript/lumscript_capi.gen.h", out);
	serializeLumScriptReference(data);
	g_meta_data = nullptr;
}

} // anonymous namespace

META_PLUGIN(serializeLumScriptMeta)
