#include "meta.h"

#define OUT_ENDL "\r\n"
#define L(...) out.add(__VA_ARGS__, OUT_ENDL)

namespace {

bool isSupportedLumScriptType(StringView type) {
	return equal(type, "void")
		|| equal(type, "bool")
		|| equal(type, "i32")
		|| equal(type, "int")
		|| equal(type, "u32")
		|| equal(type, "float")
		|| equal(type, "Vec3")
		|| equal(type, "Quat")
		|| equal(type, "EntityRef")
		|| equal(type, "EntityPtr");
}

bool isLumScriptStringArg(const Arg& arg) {
	return arg.is_const && arg.is_ptr && equal(arg.type, "char");
}

bool isLumScriptEntityType(StringView type) {
	return equal(type, "EntityRef") || equal(type, "EntityPtr");
}

bool isSupportedLumScriptArg(const Arg& arg) {
	return !arg.is_ref && ((!arg.is_ptr && isSupportedLumScriptType(arg.type)) || isLumScriptStringArg(arg));
}

StringView functionScriptName(Function& f) {
	return f.attributes.alias.size() > 0 ? f.attributes.alias : f.name;
}

void appendLumScriptType(OutputStream& out, StringView type) {
	if (equal(type, "void")) out.add("TypeRef(TypeRef::VOID)");
	else if (equal(type, "bool")) out.add("TypeRef(TypeRef::BOOL)");
	else if (equal(type, "i32") || equal(type, "int") || equal(type, "u32")) out.add("TypeRef(TypeRef::I32)");
	else if (equal(type, "float")) out.add("TypeRef(TypeRef::F32)");
	else if (equal(type, "Vec3")) out.add("TypeRef(TypeRef::STRUCT, \"Vec3\", -1)");
	else if (equal(type, "Quat")) out.add("TypeRef(TypeRef::STRUCT, \"Quat\", -1)");
	else if (equal(type, "EntityRef") || equal(type, "EntityPtr")) out.add("nativeType(module, module.makeQualifiedName(\"entity\", \"Entity\"), \"engine:entity/Entity\")");
	else out.add("TypeRef()");
}

void appendLumScriptArgType(OutputStream& out, const Arg& arg) {
	if (isLumScriptStringArg(arg)) out.add("TypeRef(TypeRef::STRING)");
	else appendLumScriptType(out, arg.type);
}

void appendComponentHandleType(OutputStream& out, Component& c, const char* visible_namespace_expr) {
	out.add("nativeType(module, module.makeQualifiedName(", visible_namespace_expr, ", \"", c.name, "\"), \"engine:", c.id, "/", c.name, "\")");
}

void appendComponentReturnType(OutputStream& out, Component& c, const char* visible_namespace_expr, bool nullable) {
	if (!nullable) {
		appendComponentHandleType(out, c, visible_namespace_expr);
		return;
	}
	out.add("TypeRef return_type = ");
	appendComponentHandleType(out, c, visible_namespace_expr);
	out.add(";" OUT_ENDL);
	out.add("return_type.nullable = true;" OUT_ENDL);
}

void appendArgValue(OutputStream& out, const Arg& arg, i32 idx) {
	if (equal(arg.type, "bool")) out.add("args[", idx, "].b");
	else if (equal(arg.type, "i32") || equal(arg.type, "int")) out.add("args[", idx, "].i");
	else if (equal(arg.type, "u32")) out.add("(u32)args[", idx, "].i");
	else if (equal(arg.type, "float")) out.add("args[", idx, "].f");
	else if (equal(arg.type, "Vec3")) out.add("toVec3(args[", idx, "])");
	else if (equal(arg.type, "Quat")) out.add("toQuat(args[", idx, "])");
	else if (equal(arg.type, "EntityRef")) out.add("EntityRef(args[", idx, "].i)");
	else if (equal(arg.type, "EntityPtr")) out.add("EntityPtr(args[", idx, "].i)");
	else if (isLumScriptStringArg(arg)) out.add("lumscript_string_arg_", idx);
}

void appendReturnValue(OutputStream& out, StringView type, const char* value) {
	if (equal(type, "void")) return;
	L("if (result) {");
	if (equal(type, "bool")) {
		L("result->type = TypeRef(TypeRef::BOOL);");
		L("result->b = ", value, ";");
	}
	else if (equal(type, "i32") || equal(type, "int") || equal(type, "u32")) {
		L("result->type = TypeRef(TypeRef::I32);");
		L("result->i = (i32)", value, ";");
		L("result->f = (float)result->i;");
	}
	else if (equal(type, "float")) {
		L("result->type = TypeRef(TypeRef::F32);");
		L("result->f = ", value, ";");
		L("result->i = (i32)result->f;");
	}
	else if (equal(type, "Vec3")) {
		L("*result = makeVec3Value(", value, ");");
	}
	else if (equal(type, "Quat")) {
		L("*result = makeQuatValue(", value, ");");
	}
	else if (equal(type, "EntityRef") || equal(type, "EntityPtr")) {
		L("result->type = TypeRef(TypeRef::NATIVE, \"engine:entity/Entity\", -1);");
		L("result->i = ", value, ".index;");
	}
	L("}");
}

bool isSupportedLumScriptFunction(Function& f) {
	if (!isSupportedLumScriptType(f.return_type)) return false;
	bool supported = true;
	forEachArg(f.args, [&](const Arg& arg, bool) {
		if (!isSupportedLumScriptArg(arg)) supported = false;
	});
	return supported;
}

void appendWrapperName(OutputStream& out, Component& c, Function& f, i32 idx) {
	out.add("lumscript_", c.id, "_", functionScriptName(f), "_", idx);
}

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

void serializeLumScriptMeta(MetaData& data) {
	OutputStream out;
	out.add("// Generated by meta.cpp" OUT_ENDL OUT_ENDL);
	out.add("#pragma once" OUT_ENDL OUT_ENDL);
	out.add("#include \"lumscript/lumscript_ast.h\"" OUT_ENDL);
	out.add("#include \"lumscript/lumscript_runtime.h\"" OUT_ENDL);
	out.add("#include \"core/stream.h\"" OUT_ENDL);
	out.add("#include \"engine/reflection.h\"" OUT_ENDL);
	out.add("#include \"engine/world.h\"" OUT_ENDL);
	for (Module& m : data.modules) {
		StringView include_path = makeStringView(m.filename);
		if (startsWith(include_path, "plugins/")) {
			out.add("#include \"../", include_path, "\"" OUT_ENDL);
		}
		else {
			if (startsWith(include_path, "src/")) include_path = withoutPrefix(include_path, 4);
			out.add("#include \"", include_path, "\"" OUT_ENDL);
		}
	}
	out.add(OUT_ENDL);
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
	out.add("static Quat toQuat(const Value& value) {" OUT_ENDL);
	out.add("return Quat(value.composite[0], value.composite[1], value.composite[2], value.composite[3]);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static Value makeVec3Value(const Vec3& value) {" OUT_ENDL);
	out.add("return Runtime::makeVec3(TypeRef(TypeRef::STRUCT, \"Vec3\", -1), value.x, value.y, value.z);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static Value makeVec3Value(const DVec3& value) {" OUT_ENDL);
	out.add("return Runtime::makeVec3(TypeRef(TypeRef::STRUCT, \"Vec3\", -1), (float)value.x, (float)value.y, (float)value.z);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("static Value makeQuatValue(const Quat& value) {" OUT_ENDL);
	out.add("return Runtime::makeQuat(TypeRef(TypeRef::STRUCT, \"Quat\", -1), value.x, value.y, value.z, value.w);" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
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
	out.add("world->setPosition(EntityRef(args[0].i), DVec3(toVec3(args[1])));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_entity_getPosition(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result || args[0].i < 0 || !world->hasEntity(EntityRef(args[0].i))) return false;" OUT_ENDL);
	out.add("*result = makeVec3Value(world->getPosition(EntityRef(args[0].i)));" OUT_ENDL);
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
	out.add("*result = makeVec3Value(world->getScale(EntityRef(args[0].i)));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);

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

	out.add("static bool lumscript_world_setPosition(Span<const Value> args, Value*, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || args[1].i < 0 || !world->hasEntity(EntityRef(args[1].i))) return false;" OUT_ENDL);
	out.add("world->setPosition(EntityRef(args[1].i), DVec3(toVec3(args[2])));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_world_getPosition(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result || args[1].i < 0 || !world->hasEntity(EntityRef(args[1].i))) return false;" OUT_ENDL);
	out.add("*result = makeVec3Value(world->getPosition(EntityRef(args[1].i)));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_world_setRotation(Span<const Value> args, Value*, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || args[1].i < 0 || !world->hasEntity(EntityRef(args[1].i))) return false;" OUT_ENDL);
	out.add("world->setRotation(EntityRef(args[1].i), toQuat(args[2]));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_world_getRotation(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result || args[1].i < 0 || !world->hasEntity(EntityRef(args[1].i))) return false;" OUT_ENDL);
	out.add("*result = makeQuatValue(world->getRotation(EntityRef(args[1].i)));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_world_setScale(Span<const Value> args, Value*, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || args[1].i < 0 || !world->hasEntity(EntityRef(args[1].i))) return false;" OUT_ENDL);
	out.add("world->setScale(EntityRef(args[1].i), toVec3(args[2]));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static bool lumscript_world_getScale(Span<const Value> args, Value* result, void*) {" OUT_ENDL);
	out.add("World* world = (World*)args[0].ptr;" OUT_ENDL);
	out.add("if (!world || !result || args[1].i < 0 || !world->hasEntity(EntityRef(args[1].i))) return false;" OUT_ENDL);
	out.add("*result = makeVec3Value(world->getScale(EntityRef(args[1].i)));" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);

	i32 wrapper_idx = 0;
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				serializeLumScriptWrapper(out, m, c, f, wrapper_idx);
				++wrapper_idx;
			}
		}
	}

	out.add("static bool hasEnum(Module& module, StringView name) {" OUT_ENDL);
	out.add("for (EnumDecl& e : module.enums) if (equalStrings(e.name, name)) return true;" OUT_ENDL);
	out.add("return false;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("static void registerGeneratedMetaEnums(Module& module, StringView alias) {" OUT_ENDL);
	for (Enum& e : data.enums) {
		if (e.values.size == 0) continue;
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
	}
	for (Module& m : data.modules) {
		for (Enum& e : m.enums) {
			if (e.values.size == 0) continue;
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
		}
	}
	out.add("}" OUT_ENDL OUT_ENDL);

	out.add("static bool registerGeneratedEngineImport(Module& module, World* world, StringView path, StringView alias) {" OUT_ENDL);
	out.add("if (!startsWith(path, \"engine:\") || alias.empty()) return false;" OUT_ENDL);
	out.add("StringView name = path.withoutLeft(7);" OUT_ENDL);
	out.add("registerGeneratedMetaEnums(module, alias);" OUT_ENDL);
	out.add("if (equalStrings(name, \"entity\")) {" OUT_ENDL);
	out.add("nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\");" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\") };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"destroy\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_destroy);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"isValid\"), TypeRef(TypeRef::BOOL), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_isValid);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getPosition\"), TypeRef(TypeRef::STRUCT, \"Vec3\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_getPosition);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getRotation\"), TypeRef(TypeRef::STRUCT, \"Quat\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_getRotation);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getScale\"), TypeRef(TypeRef::STRUCT, \"Vec3\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_entity_getScale);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"Entity\"), \"engine:entity/Entity\"), TypeRef(TypeRef::STRUCT, \"Vec3\", -1) };" OUT_ENDL);
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
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\"), TypeRef(TypeRef::STRUCT, \"Vec3\", -1) };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"setPosition\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_setPosition);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"setScale\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_setScale);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\") };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getPosition\"), TypeRef(TypeRef::STRUCT, \"Vec3\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_getPosition);" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getScale\"), TypeRef(TypeRef::STRUCT, \"Vec3\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_getScale);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\"), TypeRef(TypeRef::STRUCT, \"Quat\", -1) };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"setRotation\"), TypeRef(TypeRef::VOID), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_setRotation);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("{" OUT_ENDL);
	out.add("TypeRef params[] = { nativeType(module, makeEngineName(module, alias, \"World\"), \"engine:world/World\"), nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\") };" OUT_ENDL);
	out.add("addNativeFunction(module, module.makeQualifiedName(alias, \"getRotation\"), TypeRef(TypeRef::STRUCT, \"Quat\", -1), Span<const TypeRef>(params, lengthOf(params)), &lumscript_world_getRotation);" OUT_ENDL);
	out.add("}" OUT_ENDL);
	out.add("return true;" OUT_ENDL);
	out.add("}" OUT_ENDL);

	wrapper_idx = 0;
	for (Module& m : data.modules) {
		for (Component& c : m.components) {
			bool has_supported_function = false;
			for (Function& f : c.functions) {
				if (isSupportedLumScriptFunction(f)) {
					has_supported_function = true;
					break;
				}
			}
			if (!has_supported_function) {
				for (Function& f : c.functions) if (isSupportedLumScriptFunction(f)) ++wrapper_idx;
				continue;
			}
			L("if (equalStrings(name, \"", c.id, "\")) {");
			L("nativeType(module, makeEngineName(module, \"entity\", \"Entity\"), \"engine:entity/Entity\");");
			L("nativeType(module, makeEngineName(module, alias, \"", c.name, "\"), \"engine:", c.id, "/", c.name, "\");");
			L("bool registered = false;");
			for (Function& f : c.functions) {
				if (!isSupportedLumScriptFunction(f)) continue;
				serializeLumScriptFunctionRegistration(out, c, f, wrapper_idx);
				++wrapper_idx;
			}
			L("return registered;");
			L("}");
		}
	}

	out.add("return false;" OUT_ENDL);
	out.add("}" OUT_ENDL OUT_ENDL);
	out.add("} // namespace Lumix::LumScript::generated" OUT_ENDL);
	formatCPP(out);
	writeFile("src/lumscript/lumscript_capi.gen.h", out);
}

} // anonymous namespace

META_PLUGIN(serializeLumScriptMeta)
