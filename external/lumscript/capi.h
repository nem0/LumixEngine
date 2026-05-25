#pragma once

// C-friendly LumScript API.
//
// This header exposes a compact C ABI around the C++ LumScript
// implementation so C code can parse, typecheck, compile, and execute scripts
// without including any internal engine headers.
//
// Design notes:
// - `ls_module` owns parsed declarations, registered native functions, and the
//   string storage needed to keep copied names alive.
// - `ls_runtime` is a lightweight execution context bound to one module.
// - `ls_bytecode` and `ls_bytecode_runtime` are the planned bytecode pipeline
//   equivalents. They are declared here before their implementation exists so
//   embedders can target the future ABI shape.
// - `ls_value` is the generic runtime value container used for inputs and
//   outputs.
// - `ls_host` bundles allocator hooks and diagnostics callbacks into one
//   object. It is the main bridge between host code and LumScript.
// - Strings are passed as non-owning `[begin, end)` spans. The caller keeps the
//   underlying bytes alive for the duration of the call.
// - The ABI is intentionally plain C: no templates, references, or exceptions.

#include <assert.h>

#ifndef ASSERT
	#define ASSERT(x) assert(x)
#endif

typedef char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef unsigned int u32;
typedef long long i64;
typedef unsigned long long u64;
typedef u64 uintptr;

static_assert(sizeof(uintptr) == sizeof(void*));

#ifdef __cplusplus
extern "C" {
#endif

// Non-owning string span. `end` points one past the last byte.
typedef struct ls_string_view {
	const char* begin;
	const char* end;
} ls_string_view;

// Type categories mirrored from the internal LumScript type system.
typedef enum ls_type_kind {
	LS_TYPE_INVALID = 0,
	LS_TYPE_VOID,
	LS_TYPE_BOOL,
	LS_TYPE_I8,
	LS_TYPE_U8,
	LS_TYPE_I16,
	LS_TYPE_U16,
	LS_TYPE_I32,
	LS_TYPE_U32,
	LS_TYPE_I64,
	LS_TYPE_U64,
	LS_TYPE_F32,
	LS_TYPE_F64,
	LS_TYPE_STRING,
	LS_TYPE_UNTYPED_INT,
	LS_TYPE_UNTYPED_FLOAT,
	LS_TYPE_STRUCT,
	LS_TYPE_ENUM,
	LS_TYPE_NATIVE,
	LS_TYPE_FUNCTION,
	LS_TYPE_ARRAY,
	LS_TYPE_NULL_VALUE
} ls_type_kind;

// Type descriptor used by native functions and value constructors.
//
// For scalar kinds, only `kind` matters. For compound kinds:
// - `name` identifies structs, enums, or native types by visible name
// - `struct_index` refers to the resolved declaration inside the module
// - `element_*` and `array_size` describe array metadata
// - `nullable` marks values that may legally be null
typedef struct ls_type {
	ls_type_kind kind;
	ls_string_view name;
	i32 struct_index;
	ls_type_kind element_kind;
	ls_string_view element_name;
	i32 array_size;
	int nullable;
} ls_type;

// Enum member descriptor used by `ls_module_add_enum`.
typedef struct ls_enum_member {
	ls_string_view name;
	i32 value;
} ls_enum_member;

// Generic runtime value.
//
// The layout mirrors the internal runtime representation closely so values can
// move across the ABI with minimal translation. Only a subset of fields is
// meaningful for a given type:
// - integers use `i` / `u` / `i64` / `u64`
// - floats use `f` / `d`
// - strings use `string`
// - native objects use `ptr`
// - structured values may use `composite`
typedef struct ls_value {
	ls_type type;
	int b;
	i32 i;
	u32 u;
	i64 i64;
	u64 u64;
	float f;
	double d;
	ls_string_view string;
	float composite[4];
	void* ptr;
} ls_value;

// Native print callback used by `ls_host`.
typedef void (*ls_print_fn)(void* userdata, ls_string_view msg);

// Import resolver used by `ls_module_compile`.
//
// Return non-zero on success and write the imported source into `*source`.
typedef int (*ls_import_resolver_fn)(void* userdata, ls_string_view path, ls_string_view alias, ls_string_view* source);

// Native function callback used by `ls_module_add_native_function`.
//
// Return non-zero on success. `result` may be null if the caller does not care
// about a return value.
typedef int (*ls_native_fn)(const ls_value* args, size_t arg_count, ls_value* result, void* userdata);

// Host bridge shared by module creation, parsing, compilation, and runtime.
//
// - allocator hooks are used for module/runtime-owned memory
// - diagnostics hooks are used for error output
// - the two userdata pointers are kept separate so a host can route memory and
//   diagnostics through different objects
typedef struct ls_host {
	void* allocator_userdata;
	void* (*allocate)(void* userdata, size_t size, size_t align);
	void (*deallocate)(void* userdata, void* ptr);
	void* (*reallocate)(void* userdata, void* ptr, size_t new_size, size_t old_size, size_t align);
	void* diagnostics_userdata;
	ls_print_fn print;
} ls_host;

// Opaque module/runtime handles.
//
// These are deliberately incomplete in the C ABI. Callers only pass pointers
// around; all ownership and implementation details remain inside LumScript.
typedef struct ls_module ls_module;
typedef struct ls_runtime ls_runtime;
typedef struct ls_bytecode ls_bytecode;
typedef struct ls_bytecode_runtime ls_bytecode_runtime;

// Module lifetime.
//
// Create one module per script bundle or compilation unit. Destroy it when the
// compiled declarations and any runtime state are no longer needed.
ls_module* ls_module_create(const ls_host* host);
void ls_module_destroy(ls_module* module);
void* ls_to_cpp_module(ls_module* module); // returns c++ Module as void*

// Native registration.
//
// Register custom native types and functions before typechecking or execution.
// Native types let scripts talk about engine objects by name, while native
// functions expose host behavior to scripts.
int ls_module_add_native_type(ls_module* module, ls_string_view name, ls_string_view id);
int ls_module_add_enum(ls_module* module, ls_string_view name, const ls_enum_member* members, size_t member_count);
int ls_module_add_native_function(
	ls_module* module,
	ls_string_view name,
	ls_type return_type,
	const ls_type* param_types,
	size_t param_count,
	ls_native_fn callback,
	void* userdata
);

// Front-end pipeline helpers.
//
// `ls_module_parse` appends declarations into the module.
// `ls_module_typecheck` resolves and validates the current module contents.
// `ls_module_compile` performs parse + import resolution + typecheck in one
// call.
int ls_module_parse(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_host* host
);

int ls_module_typecheck(
	ls_module* module,
	ls_host* host
);

int ls_module_compile(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_host* host,
	ls_import_resolver_fn import_resolver,
	void* import_resolver_userdata
);

int ls_module_get_struct_count(ls_module* module);
int ls_module_get_function_count(ls_module* module);
int ls_module_get_global_count(ls_module* module);

// Runtime lifetime.
//
// Bind a runtime to a prepared module to call script functions repeatedly.
// Destroy it when execution is finished.
ls_runtime* ls_runtime_create(ls_module* module);
void ls_runtime_destroy(ls_runtime* runtime);

// Execute a function by name.
//
// Arguments are passed as a flat array of `ls_value`. The result pointer may be
// null if the caller only cares about success/failure.
int ls_runtime_call(
	ls_runtime* runtime,
	ls_string_view function_name,
	const ls_value* args,
	size_t arg_count,
	ls_value* result,
	ls_host* host
);

// Compile the prepared module into bytecode.
//
// Returns a bytecode handle on success, or null on failure. Destroy the
// returned bytecode after all runtimes using it have been destroyed.
ls_bytecode* ls_bytecode_compile(
	ls_module* module,
	ls_host* host
);
void ls_bytecode_destroy(ls_bytecode* bytecode);

// Bytecode runtime lifetime.
//
// Bind a runtime to compiled bytecode to call script functions repeatedly.
// Destroy it when execution is finished.
ls_bytecode_runtime* ls_bytecode_runtime_create(ls_bytecode* bytecode);
void ls_bytecode_runtime_destroy(ls_bytecode_runtime* runtime);

// Push values onto the bytecode runtime stack.
//
// These are used to pass arguments before calling a function.
void ls_bytecode_runtime_push_bool(ls_bytecode_runtime* runtime, int value);
void ls_bytecode_runtime_push_i32(ls_bytecode_runtime* runtime, i32 value);
void ls_bytecode_runtime_push_u32(ls_bytecode_runtime* runtime, u32 value);
void ls_bytecode_runtime_push_i64(ls_bytecode_runtime* runtime, i64 value);
void ls_bytecode_runtime_push_u64(ls_bytecode_runtime* runtime, u64 value);
void ls_bytecode_runtime_push_f32(ls_bytecode_runtime* runtime, float value);
void ls_bytecode_runtime_push_f64(ls_bytecode_runtime* runtime, double value);
void ls_bytecode_runtime_push_string(ls_bytecode_runtime* runtime, ls_string_view value);
void ls_bytecode_runtime_push_null(ls_bytecode_runtime* runtime);

i32 ls_to_i32(ls_bytecode_runtime* runtime, i32 index);

// Execute a bytecode function by name.
//
// The runtime stack is used for arguments. The result pointer may be null if
// the caller does not care about a return value.
int ls_bytecode_runtime_call(
	ls_bytecode_runtime* runtime,
	ls_string_view function_name,
	ls_host* host
);

// Helper constructors for type descriptors.
//
// These are convenience functions for native code that needs to describe
// values, parameters, or array metadata in the same shape LumScript expects
// internally.
ls_string_view ls_make_qualified_name(ls_module* module, ls_string_view prefix, ls_string_view name);
ls_type ls_type_make(ls_type_kind kind);
ls_type ls_type_make_struct(ls_string_view name, i32 struct_index, int nullable);
ls_type ls_type_make_enum(ls_string_view name, i32 struct_index, int nullable);
ls_type ls_type_make_native(ls_string_view name, i32 struct_index, int nullable);
ls_type ls_type_make_array(
	ls_type_kind element_kind,
	ls_string_view element_name,
	i32 struct_index,
	i32 array_size,
	int nullable
);

// Helper constructors for runtime values.
//
// The helpers fill in the usual numeric aliases so native code can populate a
// single canonical `ls_value` without manually duplicating every field.
ls_value ls_value_make_void(void);
ls_value ls_value_make_bool(int value);
ls_value ls_value_make_i32(i32 value);
ls_value ls_value_make_u32(u32 value);
ls_value ls_value_make_i64(i64 value);
ls_value ls_value_make_u64(u64 value);
ls_value ls_value_make_f32(float value);
ls_value ls_value_make_f64(double value);
ls_value ls_value_make_string(ls_string_view value);
ls_value ls_value_make_null(void);

#ifdef __cplusplus
}
#endif
