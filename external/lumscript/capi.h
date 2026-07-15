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
// - `ls_bytecode` and `ls_runtime` are the public execution pipeline.
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
typedef float f32;
typedef double f64;
typedef u64 uintptr;

static_assert(sizeof(uintptr) == sizeof(void*), "fix this");

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
	LS_TYPE_FUNCTION,
	LS_TYPE_ARRAY,
	LS_TYPE_SLICE,
	LS_TYPE_NULL_VALUE,
	LS_TYPE_CPTR,
	LS_TYPE_NAMESPACE
} ls_type_kind;

// Generic status used by C API operations that only report success or failure.
typedef enum ls_result {
	LS_RESULT_FAILURE = 0,
	LS_RESULT_OK = 1
} ls_result;

// Native print callback used by `ls_host`.
typedef void (*ls_print_fn)(void* userdata, ls_string_view msg);

// Import resolver used by `ls_module_compile`.
//
// Return non-zero on success and write the imported source into `*source`.
typedef int (*ls_import_resolver_fn)(void* userdata, ls_string_view path, ls_string_view alias, ls_string_view* source);

typedef struct ls_runtime ls_runtime;

typedef struct ls_call_frame {
	const u8* args;
	u8* result;
} ls_call_frame;

static inline const u8* ls_arg_read(ls_call_frame* frame, size_t size) {
	const u8* val = frame->args;
	frame->args += size;
	return val;
}

ls_string_view ls_arg_read_string(ls_call_frame* frame);

#define LS_ARG(frame, type, name) type name; \
	do { \
		const u8* _ls_ptr = ls_arg_read(&(frame), sizeof(type)); \
		memcpy(&(name), _ls_ptr, sizeof(type)); \
	} while(0)

#define LS_STRING_ARG(frame, name) ls_string_view name = ls_arg_read_string(&(frame))

#define LS_RESULT(frame, type, value) do { \
	type _ls_val = (value); \
	memcpy((frame).result, &_ls_val, sizeof(type)); \
} while(0)

// Native function callback used by `ls_runtime_set_native_function_callback`.
typedef void (*ls_native_fn)(ls_runtime* runtime, ls_call_frame frame);

typedef struct ls_arena {
	void* (*allocate)(void* user_data, size_t size, size_t align);
	void (*restore)(void* user_data, void* ptr);

	void* user_data;
} ls_arena;

// Host bridge shared by module creation, parsing, compilation, and runtime.
//
// - allocator hooks are used for module/runtime-owned memory
// - diagnostics hooks are used for error output
// - the two userdata pointers are kept separate so a host can route memory and
//   diagnostics through different objects
// - arena hooks are required; module/bytecode/runtime creation fails without
//   them
typedef struct ls_host {
	void* allocator_userdata;
	void* (*allocate)(void* userdata, size_t size, size_t align);
	void (*deallocate)(void* userdata, void* ptr);
	void* (*reallocate)(void* userdata, void* ptr, size_t new_size, size_t old_size, size_t align);
	
	ls_arena* (*create_arena)();
	void (*destroy_arena)(ls_arena* arena);

	void* diagnostics_userdata;
	ls_print_fn print;
} ls_host;

// Opaque module/runtime handles.
//
// These are deliberately incomplete in the C ABI. Callers only pass pointers
// around; all ownership and implementation details remain inside LumScript.
typedef struct ls_module ls_module;
typedef struct ls_bytecode ls_bytecode;

// Module lifetime.
//
// Create one module per script bundle or compilation unit. Destroy it when the
// compiled declarations and any runtime state are no longer needed.
ls_module* ls_module_create(const ls_host* host);
void ls_module_destroy(ls_module* module);

// Native registration.
//
// Register custom native functions before typechecking or execution.
// Native types let scripts talk about engine objects by name, while native
// functions expose host behavior to scripts.
int ls_module_get_native_function_index(ls_module* module, ls_string_view name);
int ls_module_get_native_function_count(ls_module* module);
ls_string_view ls_module_get_native_function_name(ls_module* module, int index);

// Front-end pipeline helpers.
//
// `ls_module_parse` appends declarations into the module.
// `ls_module_typecheck` resolves and validates the current module contents.
// `ls_module_compile` performs parse + import resolution + typecheck in one
// call.
ls_result ls_module_parse(ls_module* module, ls_string_view source, ls_string_view source_name);

ls_result ls_module_typecheck(ls_module* module);

ls_result ls_module_compile(
	ls_module* module,
	ls_string_view source,
	ls_string_view source_name,
	ls_import_resolver_fn import_resolver,
	void* import_resolver_userdata
);

int ls_module_get_struct_count(ls_module* module);
int ls_module_get_function_count(ls_module* module);
int ls_module_get_global_count(ls_module* module);

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
ls_runtime* ls_runtime_create(ls_bytecode* bytecode);
void ls_runtime_destroy(ls_runtime* runtime);
ls_result ls_runtime_set_native_function_callback(
	ls_runtime* runtime,
	int function_index,
	ls_native_fn callback
);

void ls_push_bool(ls_runtime* runtime, int value);
void ls_push_i32(ls_runtime* runtime, i32 value);
void ls_push_u32(ls_runtime* runtime, u32 value);
void ls_push_i64(ls_runtime* runtime, i64 value);
void ls_push_u64(ls_runtime* runtime, u64 value);
void ls_push_f32(ls_runtime* runtime, float value);
void ls_push_f64(ls_runtime* runtime, double value);
void ls_push_string(ls_runtime* runtime, ls_string_view value);
void ls_push_null(ls_runtime* runtime);
void ls_push_ptr(ls_runtime* runtime, void* value);

// Raw access to the most recent call result.
//
// Returns a pointer to the raw bytes of the value returned by the last
// executed function and writes their count to `*size`, or returns null (and
// writes 0) when there is no result. Struct fields are packed with no padding,
// in declaration order, so hosts can read components at explicit offsets
// instead of relying on the positional `ls_to_*` helpers. The pointer is
// invalidated by the next push or call.
const void* ls_call_result(ls_runtime* runtime, u32* size);

i32 ls_to_bool(ls_runtime* runtime, i32 index);
i8  ls_to_i8 (ls_runtime* runtime, i32 index);
u8  ls_to_u8 (ls_runtime* runtime, i32 index);
i16 ls_to_i16(ls_runtime* runtime, i32 index);
u16 ls_to_u16(ls_runtime* runtime, i32 index);
i32 ls_to_i32(ls_runtime* runtime, i32 index);
u32 ls_to_u32(ls_runtime* runtime, i32 index);
i64 ls_to_i64(ls_runtime* runtime, i32 index);
u64 ls_to_u64(ls_runtime* runtime, i32 index);
float ls_to_f32(ls_runtime* runtime, i32 index);
double ls_to_f64(ls_runtime* runtime, i32 index);
ls_string_view ls_to_string(ls_runtime* runtime, i32 index);
void* ls_to_ptr(ls_runtime* runtime, i32 index);

// Execute a bytecode function by name.
//
// Push arguments onto the runtime stack with the `ls_push_*` helpers first.
// After the call, the return value is left on top of the runtime stack.
ls_result ls_call(ls_runtime* runtime, ls_string_view function_name);

// Execute a bytecode function by index.
ls_result ls_call_index(ls_runtime* runtime, i32 function_index);

// Query the declared return type of the function named `function_name`.
// Callers can then read the value from the runtime stack using the `ls_to_*`
// helpers with index `-1`.
ls_type_kind ls_bytecode_runtime_result_kind(ls_runtime* runtime, ls_string_view function_name);

#ifdef __cplusplus
}
#endif
