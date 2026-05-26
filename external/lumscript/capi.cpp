#include "capi.h"

#include <cstring>
#include <new>

#include "ast.h"
#include "bytecode.h"
#include "compiler.h"

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

static i32 addNativeType(Module& module, ls_string_view name, ls_string_view id) {
	for (i32 i = 0; i < module.native_types.size(); ++i) {
		if (equalStrings(module.native_types[i].name, name)) return i;
	}
	NativeTypeDecl& type = module.native_types.emplace_back();
	type.name = module.copyName(name);
	type.id = module.copyName(id);
	return (int)module.native_types.size() - 1;
}

static TypeRef nativeType(Module& module, ls_string_view visible_name, ls_string_view id) {
	const i32 idx = addNativeType(module, visible_name, id);
	return TypeRef(LS_TYPE_NATIVE, module.native_types[idx].id, idx);
}

struct ModuleHandle {
	explicit ModuleHandle(const ls_host* host_)
		: host(host_ ? *host_ : ls_host{})
		, module(&host)
	{}

	ls_host host;
	Module module;
};

static i32 bytecodeFindFunction(const ls_bytecode* bytecode, ls_string_view name);

ls_module* ls_module_create(const ls_host* host) {
	return reinterpret_cast<ls_module*>(new (std::nothrow) ModuleHandle(host));
}

void ls_module_destroy(ls_module* module) {
	delete reinterpret_cast<ModuleHandle*>(module);
}

int ls_module_add_native_type(ls_module* module, ls_string_view name, ls_string_view id) {
	if (!module) return -1;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	for (i32 i = 0; i < handle->module.native_types.size(); ++i) {
		if (equalStrings(handle->module.native_types[i].name, name)) return i;
	}
	NativeTypeDecl& type = handle->module.native_types.emplace_back();
	type.name = handle->module.copyName(name);
	type.id = handle->module.copyName(id);
	return (int)handle->module.native_types.size() - 1;
}

int ls_module_add_enum(ls_module* module, ls_string_view name, const ls_enum_member* members, size_t member_count) {
	if (!module) return -1;
	if (!members && member_count > 0) return -1;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	for (i32 i = 0; i < handle->module.enums.size(); ++i) {
		if (equalStrings(handle->module.enums[i].name, name)) return i;
	}
	EnumDecl& e = handle->module.enums.emplace_back();
	e.name = handle->module.copyName(name);
	for (size_t i = 0; i < member_count; ++i) {
		EnumMember& member = e.members.emplace_back();
		member.name = handle->module.copyName(members[i].name);
		member.value = members[i].value;
	}
	return (int)handle->module.enums.size() - 1;
}

int ls_module_add_native_function(
	ls_module* module,
	ls_string_view name,
	ls_type return_type,
	const ls_type* param_types,
	size_t param_count,
	ls_native_fn callback,
	void* userdata
) {
	if (!module) return -1;
	if (!param_types && param_count > 0) return -1;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);

	NativeFunctionDecl& fn = handle->module.native_functions.emplace_back();
	fn.name = handle->module.copyName(name);
	fn.return_type = toTypeRef(return_type);
	fn.callback = reinterpret_cast<NativeCallback>(callback);
	fn.userdata = userdata;

	for (size_t i = 0; i < param_count; ++i) {
		Param& p = fn.params.emplace_back();
		p.type = toTypeRef(param_types[i]);
	}
	return (int)handle->module.native_functions.size() - 1;
}

int ls_module_parse(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_host* host
) {
	if (!module) return 0;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	return parse(handle->module, source, {}, source_name) ? 1 : 0;
}

int ls_module_typecheck(
	ls_module* module,
	ls_host* host
) {
	if (!module) return 0;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	return typecheck(handle->module) ? 1 : 0;
}

struct ImportResolverContext {
	ls_import_resolver_fn resolver = nullptr;
	void* userdata = nullptr;
};

static bool importResolverAdapter(Module&, ls_string_view path, ls_string_view alias, ls_string_view* source, void* userdata) {
	ImportResolverContext* ctx = (ImportResolverContext*)userdata;
	if (!ctx || !ctx->resolver) return false;
	ls_string_view c_source = {};
	if (!ctx->resolver(ctx->userdata, path, alias, &c_source)) return false;
	*source = c_source;
	return true;
}

int ls_module_compile(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_host* host,
	ls_import_resolver_fn import_resolver,
	void* import_resolver_userdata
) {
	if (!module) return 0;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);

	ImportResolverContext resolver;
	resolver.resolver = import_resolver;
	resolver.userdata = import_resolver_userdata;

	return compile(handle->module, source, import_resolver ? importResolverAdapter : nullptr, import_resolver ? &resolver : nullptr, source_name) ? 1 : 0;
}

int ls_module_get_struct_count(ls_module* module) {
	if (!module) return 0;
	return (int)reinterpret_cast<ModuleHandle*>(module)->module.structs.size();
}

int ls_module_get_function_count(ls_module* module) {
	if (!module) return 0;
	return (int)reinterpret_cast<ModuleHandle*>(module)->module.functions.size();
}

int ls_module_get_global_count(ls_module* module) {
	if (!module) return 0;
	return (int)reinterpret_cast<ModuleHandle*>(module)->module.globals.size();
}

ls_bytecode* ls_bytecode_compile(
	ls_module* module,
	ls_host* host
) {
	if (!module) return nullptr;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	return compileBytecode(handle->module, host);
}

void ls_bytecode_destroy(ls_bytecode* bytecode) {
	destroyBytecode(bytecode);
}

ls_runtime* ls_runtime_create(ls_bytecode* bytecode) {
	return createBytecodeRuntime(bytecode);
}

void ls_runtime_destroy(ls_runtime* runtime) {
	destroyBytecodeRuntime(runtime);
}

static i32 runtimeStackIndex(ls_runtime* runtime, i32 index) {
	return index < 0 ? i32(runtime->stack.size()) + index : index;
}

i32 ls_to_i32(ls_runtime* runtime, i32 index) {
	if (!runtime) return 0;
	return i32(runtime->stack[runtimeStackIndex(runtime, index)]);
}

i32 ls_to_bool(ls_runtime* runtime, i32 index) {
	return ls_to_i32(runtime, index) != 0;
}

u32 ls_to_u32(ls_runtime* runtime, i32 index) {
	if (!runtime) return 0;
	return u32(runtime->stack[runtimeStackIndex(runtime, index)]);
}

i64 ls_to_i64(ls_runtime* runtime, i32 index) {
	if (!runtime) return 0;
	return (i64)runtime->stack[runtimeStackIndex(runtime, index)];
}

u64 ls_to_u64(ls_runtime* runtime, i32 index) {
	if (!runtime) return 0;
	return runtime->stack[runtimeStackIndex(runtime, index)];
}

float ls_to_f32(ls_runtime* runtime, i32 index) {
	if (!runtime) return 0.0f;
	float value = 0.0f;
	memcpy(&value, &runtime->stack[runtimeStackIndex(runtime, index)], sizeof(value));
	return value;
}

double ls_to_f64(ls_runtime* runtime, i32 index) {
	if (!runtime) return 0.0;
	double value = 0.0;
	memcpy(&value, &runtime->stack[runtimeStackIndex(runtime, index)], sizeof(value));
	return value;
}

ls_string_view ls_to_string(ls_runtime* runtime, i32 index) {
	ls_string_view result = {};
	if (!runtime) return result;
	const char* begin = (const char*)(uintptr)runtime->stack[runtimeStackIndex(runtime, index)];
	if (!begin) return result;
	result.begin = begin;
	result.end = begin + strlen(begin);
	return result;
}

void ls_push_bool(ls_runtime* runtime, int value) {
	runtime->stack.push_back(value ? 1u : 0u);
}

void ls_push_i32(ls_runtime* runtime, i32 value) {
	runtime->stack.push_back((u64)value);
}

void ls_push_u32(ls_runtime* runtime, u32 value) {
	runtime->stack.push_back((u64)value);
}

void ls_push_i64(ls_runtime* runtime, i64 value) {
	runtime->stack.push_back((u64)value);
}

void ls_push_u64(ls_runtime* runtime, u64 value) {
	runtime->stack.push_back(value);
}

void ls_push_f32(ls_runtime* runtime, float value) {
	u64 raw = 0;
	memcpy(&raw, &value, sizeof(value));
	runtime->stack.push_back(raw);
}

void ls_push_f64(ls_runtime* runtime, double value) {
	u64 raw = 0;
	memcpy(&raw, &value, sizeof(value));
	runtime->stack.push_back(raw);
}

void ls_push_string(ls_runtime* runtime, ls_string_view value) {
	(void)value;
	runtime->stack.push_back(0);
}

void ls_push_null(ls_runtime* runtime) {
	runtime->stack.push_back(0);
}

static i32 bytecodeFindFunction(const ls_bytecode* bytecode, ls_string_view name) {
	if (!bytecode) return -1;
	for (i32 i = 0; i < bytecode->functions.size(); ++i) {
		if (equalStrings(bytecode->functions[i].name, name)) return i;
	}
	return -1;
}

int ls_bytecode_runtime_call_index(
	ls_runtime* runtime,
	i32 function_index,
	size_t arg_count,
	size_t result_count
) {
	if (!runtime || !runtime->bytecode) return 0;
	if (function_index < 0 || function_index >= (i32)runtime->bytecode->functions.size()) return 0;
	BytecodeFunction& fn = runtime->bytecode->functions[function_index];
	if (fn.param_count != (i32)fn.params.size()) return 0;
	if (arg_count > fn.params.size()) return 0;
	const size_t global_count = (size_t)runtime->bytecode->global_count;
	if (runtime->stack.size() < global_count + arg_count) return 0;
	const size_t arg_base = runtime->stack.size() - arg_count;
	std::vector<u64> args;
	args.reserve(arg_count);
	for (size_t i = 0; i < arg_count; ++i) args.push_back(runtime->stack[arg_base + i]);
	runtime->stack.resize(global_count);
	for (size_t i = 0; i < arg_count; ++i) runtime->stack.push_back(args[i]);
	for (size_t i = arg_count; i < fn.params.size(); ++i) runtime->stack.push_back(0);
	if (!callBytecodeRuntime(runtime, function_index)) return 0;
	if (result_count > 0 && fn.return_count == 0) {
		runtime->stack.push_back(0);
	}
	return 1;
}

int ls_bytecode_runtime_call(
	ls_runtime* runtime,
	ls_string_view function_name,
	size_t arg_count,
	size_t result_count
) {
	const i32 function_index = runtime && runtime->bytecode ? bytecodeFindFunction(runtime->bytecode, function_name) : -1;
	if (function_index < 0) return 0;
	return ls_bytecode_runtime_call_index(runtime, function_index, arg_count, result_count);
}

ls_type_kind ls_bytecode_runtime_result_kind(ls_runtime* runtime, ls_string_view function_name) {
	if (!runtime || !runtime->bytecode) return LS_TYPE_VOID;
	const i32 function_index = bytecodeFindFunction(runtime->bytecode, function_name);
	if (function_index < 0) return LS_TYPE_VOID;
	return runtime->bytecode->functions[(size_t)function_index].return_type.kind;
}

ls_string_view ls_make_qualified_name(ls_module* module, ls_string_view prefix, ls_string_view name) {
	if (!module) return {};
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	return handle->module.makeQualifiedName(prefix, name);
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

