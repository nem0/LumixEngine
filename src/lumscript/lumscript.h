#pragma once

#include "lumscript/ast.h"
#include "lumscript/checker.h"
#include "lumscript/diagnostics.h"
#include "lumscript/parser.h"
#include "lumscript/runtime.h"

namespace Lumix::LumScript {

inline void registerBuiltinTypes(Module& module) {
}

inline StringView normalizeImportPathForPolicy(StringView path) {
	if (!startsWith(path, "core:")) return path;
	StringView name = path.withoutLeft(5);
	if (!endsWith(name, ".lum")) return path;
	return StringView(path.begin, path.end - 4);
}

inline bool sameImportPathForPolicy(StringView lhs, StringView rhs) {
	return equalStrings(normalizeImportPathForPolicy(lhs), normalizeImportPathForPolicy(rhs));
}

inline bool resolveImports(Module& module, Diagnostics& diagnostics, ImportResolver import_resolver, void* import_resolver_userdata) {
	Array<u8> state(module.allocator);

	struct Context {
		Module& module;
		Diagnostics& diagnostics;
		ImportResolver import_resolver;
		void* import_resolver_userdata;
		Array<u8>& state;

		void ensureStateSize() {
			while (state.size() < module.imports.size()) state.push(0);
		}

		bool isDuplicateImport(i32 idx) {
			ImportDecl& import = module.imports[idx];
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = module.imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (sameImportPathForPolicy(import.path, previous.path)) return true;
			}
			return false;
		}

		bool hasAliasCollision(i32 idx) {
			ImportDecl& import = module.imports[idx];
			if (import.alias.empty()) return false;
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = module.imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (!sameImportPathForPolicy(import.path, previous.path)) return true;
			}
			return false;
		}

		bool resolveImport(i32 idx) {
			ensureStateSize();
			if (state[idx] == 2) return true;
			ImportDecl& import = module.imports[idx];
			if (state[idx] == 1) {
				diagnostics.errorAt(import.token, "Import cycle detected at '", import.path, "'");
				return false;
			}
			for (i32 i = 0; i < idx; ++i) {
				ImportDecl& previous = module.imports[i];
				if (!equalStrings(import.alias, previous.alias)) continue;
				if (!sameImportPathForPolicy(import.path, previous.path)) continue;
				if (state[i] == 1) {
					diagnostics.errorAt(import.token, "Import cycle detected at '", import.path, "'");
					return false;
				}
			}
			if (isDuplicateImport(idx)) {
				state[idx] = 2;
				import.processed = true;
				return true;
			}
			if (hasAliasCollision(idx)) {
				diagnostics.errorAt(import.token, "Import alias collision for '", import.alias, "'");
				return false;
			}

			if (!import_resolver) {
				diagnostics.errorAt(import.token, "Can not import '", import.path, "'");
				return false;
			}

			state[idx] = 1;
			const i32 old_import_count = module.imports.size();
			StringView source;
			if (!import_resolver(module, import.path, import.alias, &source, import_resolver_userdata)) {
				diagnostics.errorAt(import.token, "Can not import '", import.path, "'");
				return false;
			}
			if (!source.empty() && !parse(module, source, diagnostics, import.alias, import.path)) return false;

			ensureStateSize();
			for (i32 i = old_import_count; i < module.imports.size(); ++i) {
				if (!resolveImport(i)) return false;
			}

			state[idx] = 2;
			import.processed = true;
			return true;
		}
	};

	Context ctx {module, diagnostics, import_resolver, import_resolver_userdata, state};
	ctx.ensureStateSize();
	for (i32 i = 0; i < module.imports.size() && !diagnostics.has_error; ++i) {
		if (!ctx.resolveImport(i)) return false;
	}
	return !diagnostics.has_error;
}

inline bool compile(Module& module, StringView source, Diagnostics& diagnostics, ImportResolver import_resolver = nullptr, void* import_resolver_userdata = nullptr, StringView source_name = {}) {
	registerBuiltinTypes(module);
	return parse(module, source, diagnostics, {}, source_name) && resolveImports(module, diagnostics, import_resolver, import_resolver_userdata) && typecheck(module, diagnostics);
}

inline bool compileWithBuiltins(Module& module, StringView source, Diagnostics& diagnostics, ImportResolver import_resolver = nullptr, void* import_resolver_userdata = nullptr, StringView source_name = {}) {
	registerBuiltinTypes(module);
	if (!parse(module, source, diagnostics, {}, source_name)) return false;
	if (!resolveImports(module, diagnostics, import_resolver, import_resolver_userdata)) return false;
	return typecheck(module, diagnostics);
}

} // namespace Lumix::LumScript
