#include "capi.h"

#include <new>
#include <string.h>

#include "lumscript.h"
#include "lumscript/lumscript_engine_api.h"
#include "runtime.h"

namespace Lumix::LumScript {

static ls_string_view toC(StringView value) {
	return {value.begin, value.end};
}

static StringView fromC(ls_string_view value) {
	return {value.begin, value.end};
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

struct CAllocator final : IAllocator {
	explicit CAllocator(const ls_host* host)
		: m_host(host ? *host : ls_host{})
	{}

	void* allocate(size_t size, size_t align) override {
		if (m_host.allocate) return m_host.allocate(m_host.allocator_userdata, size, align);
		(void)align;
		return ::operator new(size, std::nothrow);
	}

	void deallocate(void* ptr) override {
		if (!ptr) return;
		if (m_host.deallocate) {
			m_host.deallocate(m_host.allocator_userdata, ptr);
			return;
		}
		::operator delete(ptr);
	}

	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override {
		if (m_host.reallocate) return m_host.reallocate(m_host.allocator_userdata, ptr, new_size, old_size, align);
		void* mem = allocate(new_size, align);
		if (ptr && mem) memcpy(mem, ptr, old_size < new_size ? old_size : new_size);
		if (ptr) {
			if (m_host.deallocate) m_host.deallocate(m_host.allocator_userdata, ptr);
			else ::operator delete(ptr);
		}
		return mem;
	}

	ls_host m_host;
};

struct CCallbacks final : Callbacks {
	explicit CCallbacks(ls_host* host)
		: m_host(host)
	{
		if (m_host) has_error = m_host->has_error != 0;
	}

	void print(StringView msg) override {
		has_error = true;
		if (!m_host) return;
		if (m_host->print) m_host->print(m_host->diagnostics_userdata, toC(msg));
	}

	ls_host* m_host = nullptr;
};

struct NativeFunctionContext {
	ls_native_fn callback = nullptr;
	void* userdata = nullptr;
	Module* module = nullptr;
};

static i32 addNativeType(Module& module, StringView name, StringView id) {
	for (i32 i = 0; i < module.native_types.size(); ++i) {
		if (equalStrings(module.native_types[i].name, name)) return i;
	}
	NativeTypeDecl& type = module.native_types.emplace();
	type.name = module.copyName(name);
	type.id = module.copyName(id);
	return module.native_types.size() - 1;
}

static TypeRef nativeType(Module& module, StringView visible_name, StringView id) {
	const i32 idx = addNativeType(module, visible_name, id);
	return TypeRef(TypeRef::NATIVE, module.native_types[idx].id, idx);
}

static bool nativeCallback(Span<const Value> args, Value* result, void* userdata) {
	NativeFunctionContext* ctx = (NativeFunctionContext*)userdata;
	if (!ctx || !ctx->callback) return false;

	Array<ls_value> c_args(ctx->module->allocator);
	for (const Value& arg : args) c_args.push(toC(arg));

	ls_value c_result = {};
	const int ok = ctx->callback(c_args.begin(), c_args.size(), result ? &c_result : nullptr, ctx->userdata);
	if (ok && result) *result = fromC(c_result);
	return ok != 0;
}

struct ModuleHandle {
	explicit ModuleHandle(const ls_host* host)
		: mem(host)
		, module(mem)
	{}

	CAllocator mem;
	Module module;
};

struct RuntimeHandle {
	explicit RuntimeHandle(ModuleHandle* module)
		: module(module)
		, runtime(module->module, module->module.allocator)
	{}

	ModuleHandle* module = nullptr;
	Runtime runtime;
};

} // namespace Lumix::LumScript

using namespace Lumix;
using namespace Lumix::LumScript;

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
	NativeTypeDecl& type = handle->module.native_types.emplace();
	type.name = handle->module.copyName(fromC(name));
	type.id = handle->module.copyName(fromC(id));
	return handle->module.native_types.size() - 1;
}

int ls_module_add_enum(ls_module* module, ls_string_view name, const ls_enum_member* members, size_t member_count) {
	if (!module) return -1;
	if (!members && member_count > 0) return -1;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	for (i32 i = 0; i < handle->module.enums.size(); ++i) {
		if (equalStrings(handle->module.enums[i].name, fromC(name))) return i;
	}
	EnumDecl& e = handle->module.enums.emplace(handle->module.allocator);
	e.name = handle->module.copyName(fromC(name));
	for (size_t i = 0; i < member_count; ++i) {
		EnumMember& member = e.members.emplace();
		member.name = handle->module.copyName(fromC(members[i].name));
		member.value = members[i].value;
	}
	return handle->module.enums.size() - 1;
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

	void* ctx_mem = handle->module.allocator.allocate(sizeof(NativeFunctionContext), alignof(NativeFunctionContext));
	if (!ctx_mem) return -1;

	NativeFunctionContext* ctx = new (ctx_mem) NativeFunctionContext();
	ctx->callback = callback;
	ctx->userdata = userdata;
	ctx->module = &handle->module;
	handle->module.allocated_native_data.push(ctx);

	NativeFunctionDecl& fn = handle->module.native_functions.emplace(handle->module.allocator);
	fn.name = handle->module.copyName(fromC(name));
	fn.return_type = fromC(return_type);
	fn.callback = &nativeCallback;
	fn.userdata = ctx;

	for (size_t i = 0; i < param_count; ++i) {
		Param& p = fn.params.emplace();
		p.type = fromC(param_types[i]);
	}
	return handle->module.native_functions.size() - 1;
}

int ls_module_parse(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_host* host
) {
	if (!module) return 0;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	CCallbacks cb(host);
	const int ok = parse(handle->module, fromC(source), cb, {}, fromC(source_name)) ? 1 : 0;
	if (host) host->has_error = cb.has_error ? 1 : 0;
	return ok;
}

int ls_module_typecheck(
	ls_module* module,
	ls_host* host
) {
	if (!module) return 0;
	ModuleHandle* handle = reinterpret_cast<ModuleHandle*>(module);
	CCallbacks cb(host);
	const int ok = typecheck(handle->module, cb) ? 1 : 0;
	if (host) host->has_error = cb.has_error ? 1 : 0;
	return ok;
}

struct ImportResolverContext {
	ls_import_resolver_fn resolver = nullptr;
	void* userdata = nullptr;
};

static bool importResolverAdapter(Module&, StringView path, StringView alias, StringView* source, void* userdata) {
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
	CCallbacks cb(host);

	ImportResolverContext resolver;
	resolver.resolver = import_resolver;
	resolver.userdata = import_resolver_userdata;

	const int ok = compile(handle->module, fromC(source), cb, import_resolver ? importResolverAdapter : nullptr, import_resolver ? &resolver : nullptr, fromC(source_name)) ? 1 : 0;
	if (host) host->has_error = cb.has_error ? 1 : 0;
	return ok;
}

int ls_module_get_struct_count(ls_module* module) {
	if (!module) return 0;
	return reinterpret_cast<ModuleHandle*>(module)->module.structs.size();
}

int ls_module_get_function_count(ls_module* module) {
	if (!module) return 0;
	return reinterpret_cast<ModuleHandle*>(module)->module.functions.size();
}

int ls_module_get_global_count(ls_module* module) {
	if (!module) return 0;
	return reinterpret_cast<ModuleHandle*>(module)->module.globals.size();
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
	CCallbacks cb(host);
	Array<Value> c_args(handle->module->module.allocator);
	for (size_t i = 0; i < arg_count; ++i) c_args.push(fromC(args[i]));

	Value c_result;
	const bool ok = handle->runtime.call(fromC(function_name), Span<const Value>(c_args.begin(), c_args.size()), result ? &c_result : nullptr, cb);
	if (ok && result) *result = toC(c_result);
	if (host) host->has_error = cb.has_error ? 1 : 0;
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
