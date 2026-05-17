#pragma once

#include "lumscript/lumscript_ast.h"
#include "lumscript/lumscript_checker.h"
#include "lumscript/lumscript_diagnostics.h"
#include "lumscript/lumscript_parser.h"
#include "lumscript/lumscript_runtime.h"
#include "core/log.h"

namespace Lumix::LumScript {

inline bool builtinLogError(Span<const Value> args, Value*, void*) {
	logError(args[0].string);
	return true;
}

inline void addBuiltinStruct(Module& module, StringView name, Span<const char*> fields) {
	for (StructDecl& s : module.structs) if (equalStrings(s.name, name)) return;
	StructDecl& s = module.structs.emplace(module.allocator);
	s.name = name;
	for (const char* field_name : fields) {
		FieldDecl& field = s.fields.emplace();
		field.name = field_name;
		field.type = TypeRef(TypeRef::F32);
	}
}

inline void registerBuiltinTypes(Module& module) {
	const char* vec3_fields[] = {"x", "y", "z"};
	addBuiltinStruct(module, "Vec3", Span<const char*>(vec3_fields));
	const char* quat_fields[] = {"x", "y", "z", "w"};
	addBuiltinStruct(module, "Quat", Span<const char*>(quat_fields));
}

inline void registerBuiltinFunctions(Module& module) {
	TypeRef params[] = {TypeRef(TypeRef::STRING)};
	addNativeFunction(module, "logError", TypeRef(TypeRef::VOID), Span<const TypeRef>(params), &builtinLogError);
}

inline bool resolveImports(Module& module, Diagnostics& diagnostics, ImportResolver import_resolver, void* import_resolver_userdata) {
	for (i32 i = 0; i < module.imports.size() && !diagnostics.has_error; ++i) {
		ImportDecl& import = module.imports[i];
		if (import.processed) continue;
		import.processed = true;
		if (!import_resolver) {
			diagnostics.errorAt(import.token, "Can not import '", import.path, "'");
			return false;
		}
		StringView source;
		if (!import_resolver(module, import.path, import.alias, &source, import_resolver_userdata)) {
			diagnostics.errorAt(import.token, "Can not import '", import.path, "'");
			return false;
		}
		if (!source.empty() && !parse(module, source, diagnostics, import.alias)) return false;
	}
	return !diagnostics.has_error;
}

inline bool compile(Module& module, StringView source, Diagnostics& diagnostics, ImportResolver import_resolver = nullptr, void* import_resolver_userdata = nullptr) {
	registerBuiltinTypes(module);
	return parse(module, source, diagnostics) && resolveImports(module, diagnostics, import_resolver, import_resolver_userdata) && typecheck(module, diagnostics);
}

inline bool compileWithBuiltins(Module& module, StringView source, Diagnostics& diagnostics, ImportResolver import_resolver = nullptr, void* import_resolver_userdata = nullptr) {
	registerBuiltinTypes(module);
	if (!parse(module, source, diagnostics)) return false;
	if (!resolveImports(module, diagnostics, import_resolver, import_resolver_userdata)) return false;
	registerBuiltinFunctions(module);
	return typecheck(module, diagnostics);
}

} // namespace Lumix::LumScript
