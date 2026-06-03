#include "capi.h"
#include "compiler.h"

ls_module* ls_module_create(const ls_host* host) {
	if (!host || !host->create_arena || !host->destroy_arena) return nullptr;
	return new ls_module(host);
}

void ls_module_destroy(ls_module* module) {
	delete module;
}

int ls_module_get_native_function_index(ls_module* module, ls_string_view name) {
	if (!module) return -1;
	return module->findNativeFunction(name);
}

int ls_module_get_native_function_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) count += (i32)unit.native_functions.size();
	return count;
}

ls_string_view ls_module_get_native_function_name(ls_module* module, int index) {
	if (!module || index < 0) return {};
	for (const Unit& unit : module->units) {
		if (index < (i32)unit.native_functions.size()) {
			CanonicalName cn = unit.native_functions[(size_t)index].canonical_name;
			return module->makeQualifiedName(cn.path, cn.name);
		}
		index -= (i32)unit.native_functions.size();
	}
	return {};
}

int ls_module_get_struct_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) count += (i32)unit.structs.size();
	return count;
}

int ls_module_get_function_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) count += (i32)unit.functions.size();
	return count;
}

int ls_module_get_global_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) count += (i32)unit.globals.size();
	return count;
}

ls_string_view ls_make_qualified_name(ls_module* module, ls_string_view prefix, ls_string_view name) {
	if (!module) return {};
	return module->makeQualifiedName(prefix, name);
}

ls_type ls_type_make(ls_type_kind kind) {
	ls_type type = {};
	type.kind = kind;
	return type;
}


