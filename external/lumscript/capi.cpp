#include "capi.h"
#include "compiler.h"

static bool matchesFunctionName(ls_string_view requested, ls_string_view unit_path, ls_string_view function_name) {
	if (equalStrings(requested, function_name)) return true;
	const usize path_size = size(unit_path);
	const usize name_size = size(function_name);
	if (size(requested) != path_size + 1u + name_size) return false;
	if (compareMemory(data(requested), data(unit_path), path_size) != 0) return false;
	if (data(requested)[path_size] != '.') return false;
	return compareMemory(data(requested) + path_size + 1u, data(function_name), name_size) == 0;
}

ls_module* ls_module_create(const ls_host* host) {
	if (!host || !host->create_arena || !host->destroy_arena) return nullptr;
	return new ls_module(host);
}

void ls_module_destroy(ls_module* module) {
	delete module;
}

int ls_module_get_native_function_index(ls_module* module, ls_string_view name) {
	if (!module) return -1;
	i32 index = 0;
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			if (!fn->is_extern) continue;
			if (matchesFunctionName(name, unit.path, sym.name)) return index;
			++index;
		}
	}
	return -1;
}

int ls_module_get_native_function_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			if (fn->is_extern) ++count;
		}
	}
	return count;
}

ls_string_view ls_module_get_native_function_name(ls_module* module, int index) {
	if (!module || index < 0) return {};
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (!sym.expression || sym.expression->kind != Expression::FUNCTION) continue;
			FunctionExpression* fn = static_cast<FunctionExpression*>(sym.expression);
			if (!fn->is_extern) continue;
			if (index == 0) return sym.name;
			--index;
		}
	}
	return {};
}

int ls_module_get_struct_count(ls_module* module) {
	(void)module;
	return 0;
}

int ls_module_get_function_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (sym.expression && sym.expression->kind == Expression::FUNCTION) ++count;
		}
	}
	return count;
}

int ls_module_get_global_count(ls_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (sym.expression) ++count;
		}
	}
	return count;
}

ls_string_view ls_make_qualified_name(ls_module* module, ls_string_view prefix, ls_string_view name) {
	(void)module;
	(void)prefix;
	(void)name;
	return {};
}
