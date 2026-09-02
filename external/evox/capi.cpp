#include "capi.h"
#include "compiler.h"
#include "bytecode.h"
#include "ir.h"

#include <stdlib.h>

ex_module* ex_module_create(ex_host* host) {
	if (!host || !host->arena.allocate) return nullptr;
	return new ex_module(host);
}

void ex_module_destroy(ex_module* module) {
	delete module;
}

int ex_module_get_unit_count(ex_module* module) {
	return module ? module->units.size() : 0;
}

ex_unit* ex_module_get_unit(ex_module* module, int index) {
	if (!module || index < 0 || index >= module->units.size()) return nullptr;
	return (ex_unit*)&module->units[index];
}

ex_string_view ex_unit_get_path(ex_unit* unit) {
	return unit ? ((Unit*)unit)->path : ex_string_view{};
}

int ex_unit_get_native_function_count(ex_unit* unit) {
	return unit ? ((Unit*)unit)->native_symbols.size() : 0;
}

ex_string_view ex_unit_get_native_function_name(ex_unit* unit, int index) {
	Unit* impl = (Unit*)unit;
	if (!impl || index < 0 || index >= impl->native_symbols.size()) return {};
	return impl->native_symbols[index]->name;
}

ex_result ex_runtime_set_native_function_callback(ex_runtime* runtime, ex_unit* unit, int function_index, ex_native_fn callback) {
	Unit* impl = (Unit*)unit;
	if (!impl || function_index < 0 || function_index >= impl->native_symbols.size()) return EX_RESULT_FAILURE;
	FunctionExpression* fn = static_cast<FunctionExpression*>(impl->native_symbols[function_index]->expression);
	return ex_runtime_set_native_function_callback_by_bytecode_index(runtime, (int)fn->bytecode_index, callback);
}

int ex_module_get_function_count(ex_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (sym.expression && sym.expression->kind == Expression::FUNCTION) ++count;
		}
	}
	return count;
}

int ex_module_get_global_count(ex_module* module) {
	if (!module) return 0;
	i32 count = 0;
	for (const Unit& unit : module->units) {
		for (const Symbol& sym : unit.symbols) {
			if (sym.expression) ++count;
		}
	}
	return count;
}

void ex_bytecode_destroy(ex_bytecode* bytecode) {
	// TODO
}

u32 ex_bytecode_type_count(const ex_bytecode* bytecode) {
	return bytecode ? bytecode->type_info_count : 0;
}

const ex_type* ex_bytecode_type(const ex_bytecode* bytecode, u32 index) {
	if (!bytecode || index >= bytecode->type_info_count) return nullptr;
	return &bytecode->type_info[index];
}

u32 ex_type_attribute_count(const ex_type* type) {
	return type ? type->attribute_count : 0u;
}

ex_attribute ex_type_attribute_value(const ex_type* type, u32 attribute_index) {
	if (!type || !type->bytecode || attribute_index >= type->attribute_count) return {nullptr, nullptr};

	const ex_type_attribute_info& info = type->bytecode->type_attributes[type->first_attribute_index + attribute_index];
	if (info.type_index >= type->bytecode->type_info_count) return {nullptr, nullptr};

	return {(const ex_type*)&type->bytecode->type_info[info.type_index], info.value};
}

u32 ex_type_struct_field_attribute_count(const ex_type* type, u32 field_index) {
	if (!type || type->kind != EX_TYPE_STRUCT || !type->bytecode || field_index >= type->field_count) return 0u;

	return type->bytecode->type_fields[type->first_field_index + field_index].attribute_count;
}

ex_attribute ex_type_struct_field_attribute_value(
	const ex_type* type,
	u32 field_index,
	u32 attribute_index
) {
	if (!type || type->kind != EX_TYPE_STRUCT || !type->bytecode || field_index >= type->field_count) return {nullptr, nullptr};

	const ex_type_field_info& field = type->bytecode->type_fields[type->first_field_index + field_index];
	if (attribute_index >= field.attribute_count) return {nullptr, nullptr};

	const ex_type_attribute_info& info = type->bytecode->type_attributes[field.first_attribute_index + attribute_index];
	if (info.type_index >= type->bytecode->type_info_count) return {nullptr, nullptr};

	return {(const ex_type*)&type->bytecode->type_info[info.type_index], info.value};
}
