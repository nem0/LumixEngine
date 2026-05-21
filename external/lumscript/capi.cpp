#include "capi.h"

#include <new>

#include "compiler.h"
#include "runtime.h"

static ls_string_view toC(ls_string_view value) {
	return value;
}

static ls_string_view fromC(ls_string_view value) {
	return value;
}

static TypeRef fromC(ls_type value) {
	TypeRef type;
	type.kind = (TypeRef::Kind)value.kind;
	type.name = fromC(value.name);
	type.struct_index = value.struct_index;
	type.element_kind = (TypeRef::Kind)value.element_kind;
	type.element_name = fromC(value.element_name);
	type.array_size = value.array_size;
	type.nullable = value.nullable != 0;
	return type;
}

static ls_type toC(TypeRef value) {
	ls_type type = {};
	type.kind = (ls_type_kind)value.kind;
	type.name = toC(value.name);
	type.struct_index = value.struct_index;
	type.element_kind = (ls_type_kind)value.element_kind;
	type.element_name = toC(value.element_name);
	type.array_size = value.array_size;
	type.nullable = value.nullable ? 1 : 0;
	return type;
}

static Value fromC(ls_value value) {
	Value result;
	result.type = fromC(value.type);
	result.b = value.b != 0;
	result.i = value.i;
	result.u = value.u;
	result.i64 = value.i64;
	result.u64 = value.u64;
	result.f = value.f;
	result.d = value.d;
	result.string = fromC(value.string);
	for (i32 i = 0; i < 4; ++i) result.composite[i] = value.composite[i];
	result.ptr = value.ptr;
	return result;
}

static ls_value toC(const Value& value) {
	ls_value result = {};
	result.type = toC(value.type);
	result.b = value.b ? 1 : 0;
	result.i = value.i;
	result.u = value.u;
	result.i64 = value.i64;
	result.u64 = value.u64;
	result.f = value.f;
	result.d = value.d;
	result.string = toC(value.string);
	for (i32 i = 0; i < 4; ++i) result.composite[i] = value.composite[i];
	result.ptr = value.ptr;
	return result;
}

struct NativeFunctionContext {
	ls_native_fn callback = nullptr;
	void* userdata = nullptr;
	Module* module = nullptr;
};

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
	return TypeRef(TypeRef::NATIVE, module.native_types[idx].id, idx);
}

static bool nativeCallback(std::span<const Value> args, Value* result, void* userdata) {
	NativeFunctionContext* ctx = (NativeFunctionContext*)userdata;
	if (!ctx || !ctx->callback) return false;

	std::vector<ls_value> c_args;
	for (const Value& arg : args) c_args.push_back(toC(arg));

	ls_value c_result = {};
	const int ok = ctx->callback(c_args.data(), c_args.size(), result ? &c_result : nullptr, ctx->userdata);
	if (ok && result) *result = fromC(c_result);
	return ok != 0;
}

struct ModuleHandle {
	explicit ModuleHandle(const ls_host* host_)
		: host(host_ ? *host_ : ls_host{})
		, module(&host)
	{}

	ls_host host;
	Module module;
};

struct RuntimeHandle {
	explicit RuntimeHandle(ModuleHandle* module)
		: module(module)
		, runtime(module->module)
	{}

	ModuleHandle* module = nullptr;
	Runtime runtime;
};

ls_module* ls_module_create(const ls_host* host) {
	return reinterpret_cast<ls_module*>(new (std::nothrow) ModuleHandle(host));
}

void* ls_to_cpp_module(ls_module* module) {
	return &reinterpret_cast<ModuleHandle*>(module)->module;
}

void ls_module_destroy(ls_module* module) {
	delete reinterpret_cast<ModuleHandle*>(module);
}

int ls_module_add_native_type(ls_module* module, ls_string_view name, ls_string_view id) {
	if (!module) return -1;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	for (i32 i = 0; i < handle->module.native_types.size(); ++i) {
		if (equalStrings(handle->module.native_types[i].name, fromC(name))) return i;
	}
	NativeTypeDecl& type = handle->module.native_types.emplace_back();
	type.name = handle->module.copyName(fromC(name));
	type.id = handle->module.copyName(fromC(id));
	return (int)handle->module.native_types.size() - 1;
}

int ls_module_add_enum(ls_module* module, ls_string_view name, const ls_enum_member* members, size_t member_count) {
	if (!module) return -1;
	if (!members && member_count > 0) return -1;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	for (i32 i = 0; i < handle->module.enums.size(); ++i) {
		if (equalStrings(handle->module.enums[i].name, fromC(name))) return i;
	}
	EnumDecl& e = handle->module.enums.emplace_back();
	e.name = handle->module.copyName(fromC(name));
	for (size_t i = 0; i < member_count; ++i) {
		EnumMember& member = e.members.emplace_back();
		member.name = handle->module.copyName(fromC(members[i].name));
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

	void* ctx_mem = allocateMemory(handle->module.host, sizeof(NativeFunctionContext), alignof(NativeFunctionContext));
	if (!ctx_mem) return -1;

	NativeFunctionContext* ctx = new (ctx_mem) NativeFunctionContext();
	ctx->callback = callback;
	ctx->userdata = userdata;
	ctx->module = &handle->module;
	handle->module.allocated_native_data.push_back(ctx);

	NativeFunctionDecl& fn = handle->module.native_functions.emplace_back();
	fn.name = handle->module.copyName(fromC(name));
	fn.return_type = fromC(return_type);
	fn.callback = &nativeCallback;
	fn.userdata = ctx;

	for (size_t i = 0; i < param_count; ++i) {
		Param& p = fn.params.emplace_back();
		p.type = fromC(param_types[i]);
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
	return parse(handle->module, fromC(source), {}, fromC(source_name)) ? 1 : 0;
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
	if (!ctx->resolver(ctx->userdata, toC(path), toC(alias), &c_source)) return false;
	*source = fromC(c_source);
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

	return compile(handle->module, fromC(source), import_resolver ? importResolverAdapter : nullptr, import_resolver ? &resolver : nullptr, fromC(source_name)) ? 1 : 0;
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

ls_runtime* ls_runtime_create(ls_module* module) {
	if (!module) return nullptr;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	return reinterpret_cast<ls_runtime*>(new (std::nothrow) RuntimeHandle(handle));
}

void ls_runtime_destroy(ls_runtime* runtime) {
	delete reinterpret_cast<RuntimeHandle*>(runtime);
}

int ls_runtime_call(
	ls_runtime* runtime,
	ls_string_view function_name,
	const ls_value* args,
	size_t arg_count,
	ls_value* result,
	ls_host* host
) {
	if (!runtime) return 0;
	if (!args && arg_count > 0) return 0;
	RuntimeHandle* handle = reinterpret_cast<RuntimeHandle*>(runtime);
	std::vector<Value> c_args;
	for (size_t i = 0; i < arg_count; ++i) c_args.push_back(fromC(args[i]));

	Value c_result;
	const bool ok = handle->runtime.call(fromC(function_name), std::span<const Value>(c_args.begin(), c_args.size()), result ? &c_result : nullptr);
	if (ok && result) *result = toC(c_result);
	return ok ? 1 : 0;
}

ls_string_view ls_make_qualified_name(ls_module* module, ls_string_view prefix, ls_string_view name) {
	if (!module) return {};
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	return toC(handle->module.makeQualifiedName(fromC(prefix), fromC(name)));
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

ls_value ls_value_make_void(void) {
	ls_value value = {};
	value.type = ls_type_make(LS_TYPE_VOID);
	return value;
}

ls_value ls_value_make_bool(int value) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_BOOL);
	result.b = value != 0;
	return result;
}

ls_value ls_value_make_i32(int32_t value) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_I32);
	result.i = value;
	result.u = (uint32_t)value;
	result.i64 = (int64_t)value;
	result.u64 = (uint64_t)value;
	result.f = (float)value;
	result.d = (double)value;
	return result;
}

ls_value ls_value_make_u32(uint32_t value) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_U32);
	result.u = value;
	result.i = (int32_t)value;
	result.i64 = (int64_t)value;
	result.u64 = (uint64_t)value;
	result.f = (float)value;
	result.d = (double)value;
	return result;
}

ls_value ls_value_make_i64(int64_t value) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_I64);
	result.i64 = value;
	result.u64 = (uint64_t)value;
	result.i = (int32_t)value;
	result.u = (uint32_t)value;
	result.f = (float)value;
	result.d = (double)value;
	return result;
}

ls_value ls_value_make_u64(uint64_t value) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_U64);
	result.u64 = value;
	result.i64 = (int64_t)value;
	result.i = (int32_t)value;
	result.u = (uint32_t)value;
	result.f = (float)value;
	result.d = (double)value;
	return result;
}

ls_value ls_value_make_f32(float value) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_F32);
	result.f = value;
	result.d = (double)value;
	result.i = (int32_t)value;
	result.u = (uint32_t)value;
	result.i64 = (int64_t)value;
	result.u64 = (uint64_t)value;
	return result;
}

ls_value ls_value_make_f64(double value) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_F64);
	result.d = value;
	result.f = (float)value;
	result.i = (int32_t)value;
	result.u = (uint32_t)value;
	result.i64 = (int64_t)value;
	result.u64 = (uint64_t)value;
	return result;
}

ls_value ls_value_make_string(ls_string_view value) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_STRING);
	result.string = value;
	return result;
}

ls_value ls_value_make_null(void) {
	ls_value result = {};
	result.type = ls_type_make(LS_TYPE_NULL_VALUE);
	return result;
}
