#include "capi.h"

#include <new>

#include "ast.h"

static TypeRef toTypeRef(ls_type value) {
	TypeRef type;
	type.kind = value.kind;
	type.name = value.name;
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

int ls_module_add_native_type(ls_module* module, ls_string_view name, ls_string_view id) {
	if (!module) return -1;
	for (i32 i = 0; i < module->native_types.size(); ++i) {
		if (equalStrings(module->native_types[i].name, name)) return i;
	}
	NativeTypeDecl& type = module->native_types.emplace_back();
	type.name = module->copyName(name);
	type.id = module->copyName(id);
	return (int)module->native_types.size() - 1;
}

int ls_module_add_enum(ls_module* module, ls_string_view name, const ls_enum_member* members, size_t member_count) {
	if (!module) return -1;
	if (!members && member_count > 0) return -1;
	for (i32 i = 0; i < module->enums.size(); ++i) {
		if (equalStrings(module->enums[i].name, name)) return i;
	}
	EnumDecl& e = module->enums.emplace_back();
	e.name = module->copyName(name);
	for (size_t i = 0; i < member_count; ++i) {
		EnumMember& member = e.members.emplace_back();
		member.name = module->copyName(members[i].name);
		member.value = members[i].value;
	}
	return (int)module->enums.size() - 1;
}

int ls_module_get_native_function_index(ls_module* module, ls_string_view name) {
	if (!module) return -1;
	return module->findNativeFunction(name);
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
	NativeFunctionDecl& fn = module->native_functions.emplace_back();
	fn.name = module->copyName(name);
	fn.canonical_name = fn.name;
	fn.return_type = toTypeRef(return_type);

	for (size_t i = 0; i < param_count; ++i) {
		Param& p = fn.params.emplace_back();
		p.type = toTypeRef(param_types[i]);
	}
	return (int)module->native_functions.size() - 1;
}

int ls_module_get_struct_count(ls_module* module) {
	if (!module) return 0;
	return (int)module->structs.size();
}

int ls_module_get_function_count(ls_module* module) {
	if (!module) return 0;
	return (int)module->functions.size();
}

int ls_module_get_global_count(ls_module* module) {
	if (!module) return 0;
	return (int)module->globals.size();
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

ls_type ls_type_make_struct(ls_string_view name, int32_t struct_index, int nullable) {
	ls_type res = ls_type_make(LS_TYPE_STRUCT);
	res.name = name;
	res.struct_index = struct_index;
	res.nullable = nullable;
	return res;
}

ls_type ls_type_make_enum(ls_string_view name, int32_t struct_index, int nullable) {
	ls_type res = ls_type_make(LS_TYPE_ENUM);
	res.name = name;
	res.struct_index = struct_index;
	res.nullable = nullable;
	return res;
}

ls_type ls_type_make_native(ls_string_view name, int32_t struct_index, int nullable) {
	ls_type res = ls_type_make(LS_TYPE_NATIVE);
	res.name = name;
	res.struct_index = struct_index;
	res.nullable = nullable;
	return res;
}

ls_type ls_type_make_array(
	ls_type_kind element_kind,
	ls_string_view element_name,
	int32_t struct_index,
	int32_t array_size,
	int nullable
) {
	ls_type res = ls_type_make(LS_TYPE_ARRAY);
	res.element_kind = element_kind;
	res.element_name = element_name;
	res.struct_index = struct_index;
	res.array_size = array_size;
	res.nullable = nullable;
	return res;
}

