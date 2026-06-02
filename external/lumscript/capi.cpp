#include "capi.h"

#include <new>

#include "ast.h"

static ls_type toC(const TypeRef& type) {
	ls_type result = {};
	result.kind = type.kind;
	result.name = type.unresolved_name;
	result.struct_index = type.struct_index;
	result.element_kind = type.element_kind;
	result.element_name = type.element_name;
	result.array_size = type.array_size;
	result.nullable = type.nullable ? 1 : 0;
	return result;
}

static TypeRef toTypeRef(ls_type value) {
	TypeRef type;
	type.kind = value.kind;
	type.unresolved_name = value.name;
	type.struct_index = value.struct_index;
	type.element_kind = value.element_kind;
	type.element_name = value.element_name;
	type.array_size = value.array_size;
	type.nullable = value.nullable != 0;
	return type;
}

ls_module* ls_module_create(const ls_host* host) {
	return new (std::nothrow) ls_module(host);
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
	for (const Unit* unit_ptr : module->units) count += (i32)unit_ptr->native_functions.size();
	return count;
}

ls_string_view ls_module_get_native_function_name(ls_module* module, int index) {
	if (!module || index < 0) return {};
	for (const Unit* unit_ptr : module->units) {
		const Unit& unit = *unit_ptr;
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
	for (const Unit* unit_ptr : module->units) count += (i32)unit_ptr->structs.size();
	return count;
}

int ls_module_get_function_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit* unit_ptr : module->units) count += (i32)unit_ptr->functions.size();
	return count;
}

int ls_module_get_global_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit* unit_ptr : module->units) count += (i32)unit_ptr->globals.size();
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


