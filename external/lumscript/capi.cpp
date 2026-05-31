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
		if (index < (i32)unit.native_functions.size()) return unit.native_functions[(size_t)index].canonical_name;
		index -= (i32)unit.native_functions.size();
	}
	return {};
}

ls_type ls_module_get_native_function_return_type(ls_module* module, int index) {
	if (!module || index < 0) return ls_type_make(LS_TYPE_INVALID);
	for (const Unit* unit_ptr : module->units) {
		const Unit& unit = *unit_ptr;
		if (index < (i32)unit.native_functions.size()) return toC(unit.native_functions[(size_t)index].return_type);
		index -= (i32)unit.native_functions.size();
	}
	return ls_type_make(LS_TYPE_INVALID);
}

int ls_module_get_native_function_param_count(ls_module* module, int index) {
	if (!module || index < 0) return 0;
	for (const Unit* unit_ptr : module->units) {
		const Unit& unit = *unit_ptr;
		if (index < (i32)unit.native_functions.size()) return (int)unit.native_functions[(size_t)index].params.size();
		index -= (i32)unit.native_functions.size();
	}
	return 0;
}

ls_type ls_module_get_native_function_param_type(ls_module* module, int index, int param_index) {
	if (!module || index < 0) return ls_type_make(LS_TYPE_INVALID);
	for (const Unit* unit_ptr : module->units) {
		const Unit& unit = *unit_ptr;
		if (index >= (i32)unit.native_functions.size()) {
			index -= (i32)unit.native_functions.size();
			continue;
		}
		const NativeFunctionDecl& fn = unit.native_functions[(size_t)index];
		if (param_index < 0 || param_index >= (int)fn.params.size()) return ls_type_make(LS_TYPE_INVALID);
		return toC(fn.params[(size_t)param_index].type);
	}
	return ls_type_make(LS_TYPE_INVALID);
}

int ls_module_add_native_function(
	ls_module* module,
	ls_string_view name,
	ls_type return_type,
	const ls_type* param_types,
	size_t param_count
) {
	if (!module) return -1;
	if (!param_types && param_count > 0) return -1;
	// TODO this is shit
	Unit& unit = module->addUnit();
	NativeFunctionDecl& fn = unit.native_functions.emplace_back();
	fn.canonical_name = module->copyName(name);
	fn.return_type = toTypeRef(return_type);
	ls_string_view symbol_name = name;
	ls_string_view owner;
	ls_string_view member;
	if (splitMemberName(name, &owner, &member)) symbol_name = member;
	unit.symbols.push_back(Symbol{Symbol::EXTERN_FN, {unit.source_name, symbol_name}});

	for (size_t i = 0; i < param_count; ++i) {
		Param& p = fn.params.emplace_back();
		p.type = toTypeRef(param_types[i]);
	}
	i32 count = 0;
	for (const Unit* unit_ptr : module->units) count += (i32)unit_ptr->native_functions.size();
	return count - 1;
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


