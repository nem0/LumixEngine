#include "capi.h"
#include "compiler.h"
#include "bytecode.h"
#include "ir.h"

#include <stdlib.h>

ls_module* ls_module_create(ls_host* host) {
	if (!host || !host->arena.allocate) return nullptr;
	return new ls_module(host);
}

void ls_module_destroy(ls_module* module) {
	delete module;
}

int ls_module_get_unit_count(ls_module* module) {
	return module ? module->units.size() : 0;
}

ls_unit* ls_module_get_unit(ls_module* module, int index) {
	if (!module || index < 0 || index >= module->units.size()) return nullptr;
	return (ls_unit*)&module->units[index];
}

ls_string_view ls_unit_get_path(ls_unit* unit) {
	return unit ? ((Unit*)unit)->path : ls_string_view{};
}

int ls_unit_get_native_function_count(ls_unit* unit) {
	return unit ? ((Unit*)unit)->native_symbols.size() : 0;
}

ls_string_view ls_unit_get_native_function_name(ls_unit* unit, int index) {
	Unit* impl = (Unit*)unit;
	if (!impl || index < 0 || index >= impl->native_symbols.size()) return {};
	return impl->native_symbols[index]->name;
}

ls_result ls_runtime_set_native_function_callback(ls_runtime* runtime, ls_unit* unit, int function_index, ls_native_fn callback) {
	Unit* impl = (Unit*)unit;
	if (!impl || function_index < 0 || function_index >= impl->native_symbols.size()) return LS_RESULT_FAILURE;
	FunctionExpression* fn = static_cast<FunctionExpression*>(impl->native_symbols[function_index]->expression);
	return ls_runtime_set_native_function_callback_by_bytecode_index(runtime, (int)fn->bytecode_index, callback);
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

void ls_bytecode_destroy(ls_bytecode* bytecode) {
	// TODO
}

u32 ls_bytecode_type_count(const ls_bytecode* bytecode) {
	return bytecode ? bytecode->type_info_count : 0;
}

const ls_type* ls_bytecode_type(const ls_bytecode* bytecode, u32 index) {
	if (!bytecode || index >= bytecode->type_info_count) return nullptr;
	return &bytecode->type_info[index];
}

u32 ls_type_attribute_count(const ls_type* type) {
	return type ? type->attribute_count : 0u;
}

ls_attribute ls_type_attribute_value(const ls_type* type, u32 attribute_index) {
	if (!type || !type->bytecode || attribute_index >= type->attribute_count) return {nullptr, nullptr};

	const ls_type_attribute_info& info = type->bytecode->type_attributes[type->first_attribute_index + attribute_index];
	if (info.type_index >= type->bytecode->type_info_count) return {nullptr, nullptr};

	return {(const ls_type*)&type->bytecode->type_info[info.type_index], info.value};
}

u32 ls_type_struct_field_attribute_count(const ls_type* type, u32 field_index) {
	if (!type || type->kind != LS_TYPE_STRUCT || !type->bytecode || field_index >= type->field_count) return 0u;

	return type->bytecode->type_fields[type->first_field_index + field_index].attribute_count;
}

ls_attribute ls_type_struct_field_attribute_value(
	const ls_type* type,
	u32 field_index,
	u32 attribute_index
) {
	if (!type || type->kind != LS_TYPE_STRUCT || !type->bytecode || field_index >= type->field_count) return {nullptr, nullptr};

	const ls_type_field_info& field = type->bytecode->type_fields[type->first_field_index + field_index];
	if (attribute_index >= field.attribute_count) return {nullptr, nullptr};

	const ls_type_attribute_info& info = type->bytecode->type_attributes[field.first_attribute_index + attribute_index];
	if (info.type_index >= type->bytecode->type_info_count) return {nullptr, nullptr};

	return {(const ls_type*)&type->bytecode->type_info[info.type_index], info.value};
}
