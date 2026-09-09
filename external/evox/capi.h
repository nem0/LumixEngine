#pragma once

// C-friendly Evox API.
//
// This header exposes a compact C ABI around the C++ Evox
// implementation so C code can parse, typecheck, compile, and execute scripts
// without including any internal engine headers.
//
// Design notes:
// - `ex_module` owns parsed declarations, registered native functions, and the
//   string storage needed to keep copied names alive. `ex_module_parse`
//   copies both the source text and the source path into the module arena,
//   so token values, import paths/aliases and symbol names stay valid until
//   the module is destroyed and callers may free their buffers after the
//   call returns.
// - `ex_bytecode` and `ex_runtime` are the public execution pipeline.
// - `ex_host` bundles allocator hooks and diagnostics callbacks into one
//   object. It is the main bridge between host code and Evox.
// - Strings are passed as non-owning byte spans. The caller keeps the
//   underlying bytes alive for the duration of the call.
// - The ABI is intentionally plain C: no templates, references, or exceptions.

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef ASSERT
	#ifdef NDEBUG
		#define ASSERT(X)
	#elif defined EX_TESTS
		#include <stdio.h>
		#include <stdlib.h>
		#define ASSERT(x) do { \
			if (!(x)) { \
				fprintf(stderr, "TEST ASSERT FAILED at %s:%d: %s\n", __FILE__, __LINE__, #x); \
				fflush(stderr); \
				exit(-1); \
			} \
		} while (false)
	#else
		#define ASSERT(x) assert(x)
	#endif
#endif

typedef signed char i8;
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

typedef struct ex_string_view {
	const char* begin;
	i64 length;
} ex_string_view;

// Location of a declaration returned by ex_module_definition_at(). Lines and
// columns are zero-based. Returned source_name storage is owned by the module
// and remains valid until the module is destroyed.
typedef struct ex_definition_location {
	ex_string_view source_name;
	u32 line;
	u32 column;
	u32 length;
} ex_definition_location;

typedef struct ex_slice {
	u8* data;
	i64 length;
} ex_slice;

// Type categories mirrored from the internal Evox type system.
typedef enum ex_type_kind {
	EX_TYPE_INVALID = 0,
	EX_TYPE_VOID,
	EX_TYPE_BOOL,
	EX_TYPE_I8,
	EX_TYPE_U8,
	EX_TYPE_I16,
	EX_TYPE_U16,
	EX_TYPE_I32,
	EX_TYPE_U32,
	EX_TYPE_I64,
	EX_TYPE_U64,
	EX_TYPE_F32,
	EX_TYPE_F64,
	EX_TYPE_UNTYPED_INT,
	EX_TYPE_UNTYPED_FLOAT,
	EX_TYPE_STRUCT,
	EX_TYPE_TAGGED_UNION,
	EX_TYPE_ENUM,
	EX_TYPE_FUNCTION,
	EX_TYPE_ARRAY,
	EX_TYPE_SLICE,
	EX_TYPE_NULL_VALUE,
	EX_TYPE_CPTR,
	EX_TYPE_NAMESPACE,
	EX_TYPE_NULLABLE,
	EX_TYPE_ANY
} ex_type_kind;

// Result of a C API operation. Failure results are non-zero so callers must
// compare against the specific result they expect (except EX_RESULT_FAILURE,
// which is retained for unexpected/internal failures).
typedef enum ex_result {
	EX_RESULT_FAILURE,
	EX_RESULT_OK,
	EX_RESULT_INVALID_ARGUMENT
} ex_result;

// Results specific to starting or resuming script execution. Keep these
// separate from ex_result so execution outcomes cannot be confused with
// results of module, debugger, or runtime-management operations.
typedef enum ex_call_result {
	EX_CALL_RESULT_OK,
	EX_CALL_RESULT_SUSPENDED,
	EX_CALL_RESULT_FUNCTION_NOT_FOUND,
	EX_CALL_RESULT_INVALID_ARGUMENT,
	EX_CALL_RESULT_INVALID_STATE,
	EX_CALL_RESULT_ALREADY_EXECUTING,
	EX_CALL_RESULT_NOT_SUSPENDED,
	EX_CALL_RESULT_NOT_RESUMABLE,
	EX_CALL_RESULT_OUT_OF_MEMORY,
	EX_CALL_RESULT_RUNTIME_ERROR,
	EX_CALL_RESULT_DIVISION_BY_ZERO,
	EX_CALL_RESULT_MODULO_BY_ZERO,
	EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS,
	EX_CALL_RESULT_INVALID_FUNCTION_CALL,
	EX_CALL_RESULT_PANIC,
	EX_CALL_RESULT_STACK_OVERFLOW,
	EX_CALL_RESULT_CALL_DEPTH
} ex_call_result;

// Native print callback used by `ex_host`.
typedef void (*ex_print_fn)(void* userdata, ex_string_view msg);
typedef void (*ex_diagnostic_fn)(void* userdata, ex_string_view source_name, u32 line, u32 column, u32 length);

// Import resolver used by `ex_module_compile`.
//
// Return non-zero on success and write the imported source into `*source`.
typedef int (*ex_import_resolver_fn)(void* userdata, ex_string_view path, ex_string_view alias, ex_string_view* source);

typedef struct ex_runtime ex_runtime;
typedef struct ex_task ex_task;

typedef enum ex_task_state {
	EX_TASK_READY = 0,
	EX_TASK_SUSPENDED,
	EX_TASK_FAILED,
} ex_task_state;

typedef struct ex_call_frame {
	const u8* args;
	u8* result;
} ex_call_frame;

ex_string_view ex_arg_read_string(ex_call_frame* frame);

// Writes a string result into a native call frame. The bytes are copied into
// runtime-owned storage, so `value` only needs to remain valid for this call.
void ex_result_string(ex_runtime* runtime, ex_call_frame* frame, ex_string_view value);

#define EX_ARG(frame, type, name) type name; \
	do { \
		memcpy(&(name), (frame).args, sizeof(type)); \
		(frame).args += sizeof(type); \
	} while(0)

#define EX_STRING_ARG(frame, name) ex_string_view name = ex_arg_read_string(&(frame))

#define EX_RESULT(frame, value) do { \
	auto _ls_val = (value); \
	memcpy((frame).result, &_ls_val, sizeof(_ls_val)); \
	(frame).result += sizeof(_ls_val); \
} while(0)

#define EX_TYPED_RESULT(frame, type, value) do { \
	type _ls_val = (value); \
	memcpy((frame).result, &_ls_val, sizeof(_ls_val)); \
	(frame).result += sizeof(_ls_val); \
} while(0)

// Native function callback used by the lazy native resolver.
// Slice arguments and results in `frame` use the `ex_slice` representation
// above and occupy sizeof(ex_slice) bytes. Use EX_ARG/EX_RESULT with ex_slice
// to read or write them; the element type and element size come from the
// declared script signature.
typedef void (*ex_native_fn)(ex_runtime* runtime, ex_call_frame frame);

// Resolves an extern function the first time it is called. The returned
// callback is cached in the runtime; returning NULL leaves the function
// unresolved and causes that call to fail.
typedef struct ex_native_function_desc {
	ex_string_view unit_path;
	ex_string_view name;
	int bytecode_index;
	u32 param_size;
	u32 return_size;
} ex_native_function_desc;

typedef ex_native_fn (*ex_native_resolver_fn)(
	ex_runtime* runtime,
	ex_native_function_desc function,
	void* userdata
);

typedef struct ex_arena {
	void* (*allocate)(void* user_data, size_t size, size_t align);
	void (*restore)(void* user_data, void* ptr);

	void* user_data;
} ex_arena;

// Host bridge shared by module creation, parsing, compilation, and runtime.
//
// - `arena` is used for every object created with this host
// - diagnostics hooks are used for error output
typedef struct ex_host {
	ex_arena arena;

	void* diagnostics_userdata;
	ex_print_fn print;
	ex_diagnostic_fn diagnostic;
} ex_host;

// Opaque module/runtime handles.
//
// These are deliberately incomplete in the C ABI. Callers only pass pointers
// around; all ownership and implementation details remain inside Evox.
typedef struct ex_module ex_module;
typedef struct ex_unit ex_unit;
typedef struct ex_bytecode ex_bytecode;
typedef struct ex_type ex_type;

// A typed attribute value. `value` points to the runtime-layout bytes of `type`
// and is owned by the module/bytecode that owns the inspected type.
typedef struct ex_attribute {
	const ex_type* type;
	const void* value;
} ex_attribute;

// Module lifetime.
//
// Create one module per script bundle or compilation unit. Destroy it when the
// compiled declarations and any runtime state are no longer needed.
ex_module* ex_module_create(ex_host* host);
void ex_module_destroy(ex_module* module);

// Native registration.
//
// Register custom native functions before typechecking or execution.
// Native types let scripts talk about engine objects by name, while native
// functions expose host behavior to scripts.
// Units and their native functions are available after a successful typecheck.
int ex_unit_get_native_function_count(ex_unit* unit);
ex_string_view ex_unit_get_native_function_name(ex_unit* unit, int index);

// unit enumeration
int ex_module_get_unit_count(ex_module* module);
ex_unit* ex_module_get_unit(ex_module* module, int index);
ex_string_view ex_unit_get_path(ex_unit* unit);

// import enumeration
int ex_unit_get_import_count(ex_unit* unit);
ex_string_view ex_unit_get_import_path(ex_unit* unit, int index);

// symbol/global enumeration
typedef enum ex_symbol_kind {
	EX_SYM_KIND_INVALID,
	
	EX_SYM_KIND_VARIABLE,
	EX_SYM_KIND_CONST,
	EX_SYM_KIND_COMPTIME,
	EX_SYM_KIND_IMPORT,
} ex_symbol_kind;

typedef struct ex_symbol_desc {
	ex_symbol_kind kind;
	ex_string_view name;
	u32 line;
	u32 column;
} ex_symbol_desc;

int ex_unit_get_symbols_count(ex_unit* unit);
ex_symbol_desc ex_unit_get_symbol(ex_unit* unit, int index);

// Front-end pipeline helpers.
//
// `ex_module_parse` appends declarations into the module.
// `ex_module_typecheck` resolves and validates the current module contents.
// `ex_module_compile` performs parse + import resolution + typecheck in one
// call.
ex_result ex_module_parse(ex_module* module, ex_string_view source, ex_string_view source_name);

ex_result ex_module_typecheck(ex_module* module);

// Finds the semantic declaration referred to by the token at `line`, `column`.
// The module must already have been parsed and typechecked; all positions
// are zero-based. Looks up the checked AST via source locations, so no source
// text needs to be passed back in. Returns EX_RESULT_OK when a declaration is
// found, and EX_RESULT_FAILURE otherwise.
ex_result ex_module_definition_at(
	ex_module* module,
	ex_string_view source_name,
	u32 line,
	u32 column,
	ex_definition_location* out_location
);

ex_result ex_module_compile(
	ex_module* module,
	ex_string_view source,
	ex_string_view source_name,
	ex_import_resolver_fn import_resolver,
	void* import_resolver_userdata
);

int ex_module_get_function_count(ex_module* module);

typedef struct ex_bytecode_compile_options {
	bool optimize;
} ex_bytecode_compile_options;

// Compile the checked module into bytecode through the IR pipeline.
ex_bytecode* ex_bytecode_compile(
	ex_module* module,
	ex_host* host,
	ex_bytecode_compile_options* options
);
void ex_bytecode_destroy(ex_bytecode* bytecode);

// Enumerate all types emitted into the bytecode. Returned type handles are
// stable until the bytecode is destroyed.
u32 ex_bytecode_type_count(const ex_bytecode* bytecode);
const ex_type* ex_bytecode_type(const ex_bytecode* bytecode, u32 index);

// Bytecode VM lifetime.
//
// An ex_runtime owns VM-wide state such as bytecode, globals, and native
// bindings. It is not an execution context: script execution happens only
// through ex_task. The runtime must outlive every task created from it;
// destroy tasks before destroying their runtime.
ex_runtime* ex_runtime_create(ex_bytecode* bytecode, ex_host* host);
void ex_runtime_destroy(ex_runtime* runtime);

// Task lifetime and execution.
//
// Creating a task allocates an independent execution context but does not
// select or invoke a function. Call it separately with ex_call(). A task may
// execute multiple sequential calls; each new call starts after the previous
// call has finished. Each task owns its stack, call frames, locals, and
// suspension state; tasks share the runtime's bytecode, globals, and native
// bindings.
ex_task* ex_task_create(ex_runtime* runtime);
void ex_task_destroy(ex_task* task);
ex_task_state ex_task_get_state(const ex_task* task);

// Begin executing a script function on a newly-created task. Arguments are
// copied from `args` according to the function's declared ABI. `args_size`
// must match the function's declared parameter byte size; `args` may be null
// when args_size is zero. This starts a fresh invocation and is valid on a
// newly-created task or after the previous invocation finished. It is not
// valid while the task is running or suspended. Returns FUNCTION_NOT_FOUND,
// INVALID_ARGUMENT, INVALID_STATE, or RUNTIME_ERROR when the invocation
// cannot complete.
ex_call_result ex_call(
	ex_task* task,
	ex_string_view function_name,
	const void* args,
	u32 args_size
);

// Resume a SUSPENDED task. Returns EX_CALL_RESULT_SUSPENDED when yield is reached,
// EX_RESULT_OK when the task completes, or FAILURE when it fails.
ex_call_result ex_task_resume(ex_task* task);

// TODO: Accept the resolver during ex_runtime_create and remove this setter.
// Installs a runtime-local lazy resolver for extern functions. A returned
// callback is cached; unresolved functions are retried on later calls.
ex_result ex_runtime_set_native_resolver(
	ex_runtime* runtime,
	ex_native_resolver_fn resolver,
	void* userdata
);

// Result access for the most recently completed task execution. Result memory
// belongs to the task and remains valid until the next ex_call, task resume,
// or task destruction.
const void* ex_task_result(ex_task* task, u32* size);

// Typed accessors for the current task result. Index -1 refers to the result.
i32 ex_task_to_bool(ex_task* task, i32 index);
i8  ex_task_to_i8 (ex_task* task, i32 index);
u8  ex_task_to_u8 (ex_task* task, i32 index);
i16 ex_task_to_i16(ex_task* task, i32 index);
u16 ex_task_to_u16(ex_task* task, i32 index);
i32 ex_task_to_i32(ex_task* task, i32 index);
u32 ex_task_to_u32(ex_task* task, i32 index);
i64 ex_task_to_i64(ex_task* task, i32 index);
u64 ex_task_to_u64(ex_task* task, i32 index);
float ex_task_to_f32(ex_task* task, i32 index);
double ex_task_to_f64(ex_task* task, i32 index);
ex_string_view ex_task_to_string(ex_task* task, i32 index);
void* ex_task_to_ptr(ex_task* task, i32 index);

// Query the declared return type of the function named `function_name`.
// Callers can then read the result through ex_task_result() or the
// ex_task_to_* helpers.
ex_type_kind ex_bytecode_runtime_result_kind(ex_runtime* runtime, ex_string_view function_name);

// Type introspection.
//
// `ex_type` describes the shape of a script value - its kind, byte size,
// and for compound types (struct, array, slice) the layout of their
// sub-values. Handles are owned by the bytecode and stable for its lifetime.
//
// Obtain a type handle from the debug API (ex_debug_local_type,
// ex_debug_global_type) or from module-level struct queries. The handle is
// valid while the owning bytecode (or bytecode-compiled module) lives.
//

// Returns the kind category of the type.
ex_type_kind ex_type_get_kind(const ex_type* type);

// Returns the fully qualified name of the type (unit path + '.' + declaration
// name, e.g. "core:entity.Entity"). Returns an empty string_view for
// anonymous or unnamed types.
ex_string_view ex_type_get_name(const ex_type* type);

// Returns the byte size of values of this type. Matches the byte_size
// reported by ex_debug_local_value / ex_debug_global_value.
u32 ex_type_get_size(const ex_type* type);

// Returns the required byte alignment of values of this type.
u32 ex_type_get_alignment(const ex_type* type);

// Introspect a struct type (valid when kind == EX_TYPE_STRUCT).
// Fields are enumerated in declaration order. Non-extern struct layout is
// implementation-defined; extern structs use target C ABI layout.

// Number of fields in the struct.
u32 ex_type_struct_field_count(const ex_type* type);

// Name of the field at `field_index`.
ex_string_view ex_type_struct_field_name(const ex_type* type, u32 field_index);

// Type handle for the field at `field_index`. Recursively queryable for
// nested struct drill-down.
const ex_type* ex_type_struct_field_type(const ex_type* type, u32 field_index);

// Byte offset of the field from the start of the struct value. The host
// uses this to read the field: `(u8*)struct_value + offset`.
u32 ex_type_struct_field_offset(const ex_type* type, u32 field_index);

// Attributes.

// Number of attributes attached to a type.
u32 ex_type_attribute_count(const ex_type* type);

// Attribute value at `attribute_index`. The attribute's declaration type is
// returned in `.type`; its struct value is returned in `.value`. Use
// ex_type_get_size(result.type) for the value size. Returns { NULL, NULL } for
// an invalid index.
ex_attribute ex_type_attribute_value(const ex_type* type, u32 attribute_index);

// Number of attributes attached to the struct field at `field_index`.
// Returns 0 when `type` is not a struct or the field index is invalid.
u32 ex_type_struct_field_attribute_count(const ex_type* type, u32 field_index);

// Attribute value attached to a struct field. Returns { NULL, NULL } for an
// invalid index. Use ex_type_get_size(result.type) for the value size.
ex_attribute ex_type_struct_field_attribute_value(
	const ex_type* type,
	u32 field_index,
	u32 attribute_index
);

// Introspect a tagged union type (valid when kind == EX_TYPE_TAGGED_UNION).
// A tagged union value is stored as: [tag : i32] [payload : N bytes].
// The tag identifies which member is active (0, 1, 2, ...).
// All members share the same payload space (size = max member size).

// Number of member types in the union.
u32 ex_type_union_member_count(const ex_type* type);

// Type handle for the member at `member_index`.
const ex_type* ex_type_union_member_type(const ex_type* type, u32 member_index);

// Returns the active tag from a tagged union value. The tag is the first 4
// bytes of the value, interpreted as a signed i32.
i32 ex_type_union_tag(const ex_type* type, const void* value);

// Introspect an enum type (valid when kind == EX_TYPE_ENUM).

// Number of values (members) in the enum.
u32 ex_type_enum_value_count(const ex_type* type);

// Name of the enum value at `value_index`.
ex_string_view ex_type_enum_value_name(const ex_type* type, u32 value_index);

// Integer value of the enum value at `value_index`.
i32 ex_type_enum_value_value(const ex_type* type, u32 value_index);

// Introspect an array or slice type (valid when kind is EX_TYPE_ARRAY
// or EX_TYPE_SLICE).

// Element type of the array or slice.
const ex_type* ex_type_array_element_type(const ex_type* type);
// Returns the pointee type for a language pointer, or NULL for opaque cptrs.
const ex_type* ex_type_pointer_inner_type(const ex_type* type);

// Compile-time element count. Returns the fixed length for EX_TYPE_ARRAY;
// returns 0 for EX_TYPE_SLICE (whose length is dynamic at runtime).
u32 ex_type_array_length(const ex_type* type);

// Returns whether the type is const-qualified.
bool ex_type_is_const(const ex_type* type);

// Introspect a nullable type (valid when kind == EX_TYPE_NULLABLE).
// A nullable value is stored as: [has_value : u8] [inner_value : N bytes].
// Read the first byte: 0 = null, 1 = value present.

// Inner (wrapped) type of the nullable.
const ex_type* ex_type_nullable_inner_type(const ex_type* type);

// Returns true when the nullable value is null (has_value byte is 0).
bool ex_type_nullable_is_null(const ex_type* type, const void* value);

// Returns a pointer past the has_value flag, i.e. to the inner value bytes.
// Only valid when ex_type_nullable_is_null returns false.
const void* ex_type_nullable_value_ptr(const ex_type* type, const void* value);

const ex_type* ex_type_from_any(const ex_runtime* runtime, const void* value);

//////////////////////////
// Debugger.
//
// Suspension-based: when a debug-enabled task pauses, the task execution
// returns `EX_CALL_RESULT_SUSPENDED` with the script state kept intact. The host queries `ex_debug_pause_event`, inspects task state, then
// continues with `ex_debug_resume`. While suspended, don't start new execution
// on the task.
//
// Calls through a function value (indirect calls) are ordinary script-to-
// script calls at the bytecode level and suspend normally, same as direct
// calls. Host-provided native callbacks are plain C function calls with no
// suspension support at all: none of this project's native functions call
// back into script, so this hasn't needed guarding, but a native callback
// that did start another task reentrantly would be calling into an interpreter
// loop nested on the live C stack, which cannot suspend out
// from under it.
//
// Inspection calls are only valid while suspended; pointers they return are
// invalidated by resume.

typedef enum ex_debug_pause_reason {
	EX_DEBUG_PAUSE_BREAKPOINT = 0,
	EX_DEBUG_PAUSE_STEP,
	EX_DEBUG_PAUSE_ERROR,
	EX_DEBUG_PAUSE_YIELD,
} ex_debug_pause_reason;

typedef enum ex_debug_action {
	EX_DEBUG_CONTINUE = 0,
	EX_DEBUG_STEP_INTO,
	EX_DEBUG_STEP_OVER,
	EX_DEBUG_STEP_OUT,
} ex_debug_action;

typedef struct ex_debug_location {
	ex_string_view source_name;
	u32 line;
	u32 column;
} ex_debug_location;

// `location` is the statement about to execute in the innermost frame.
typedef struct ex_debug_event {
	ex_debug_pause_reason reason;
	ex_debug_location location;
	// Failure description when `reason` is `EX_DEBUG_PAUSE_ERROR`.
	ex_string_view message;
} ex_debug_event;

int ex_debug_is_suspended(ex_task* task);

ex_result ex_debug_pause_event(ex_task* task, ex_debug_event* out_event);

// Re-enter the task where it paused. Must be called on the script thread;
// fails when the task is not suspended.
ex_call_result ex_debug_resume(ex_task* task, ex_debug_action action);

// Breakpoints. `line` is 1-based; the snapped statement line is written to
// `*resolved_line` (may be null). Fails when the source or line is unknown.
ex_result ex_debug_set_breakpoint(ex_bytecode* bytecode, ex_string_view source_name, u32 line, u32* resolved_line);
ex_result ex_debug_remove_breakpoint(ex_bytecode* bytecode, ex_string_view source_name, u32 line);
void ex_debug_remove_all_breakpoints(ex_bytecode* bytecode);

// Call stack inspection. Frame 0 is the innermost frame. Also valid
// immediately after a failed task execution, reporting the stack at the
// point of failure; the next task execution overwrites it.
u32 ex_debug_stack_depth(ex_task* task);
ex_string_view ex_debug_frame_function_name(ex_task* task, u32 frame_index);
ex_result ex_debug_frame_location(ex_task* task, u32 frame_index, ex_debug_location* out_location);

// Variable inspection. Locals enumerate the parameters and locals in scope at
// the frame's current statement. Values point at the raw bytes in live task
// frame storage; writing through them mutates the running script.
u32 ex_debug_frame_local_count(ex_task* task, u32 frame_index);
ex_string_view ex_debug_local_name(ex_task* task, u32 frame_index, u32 local_index);
void* ex_debug_local_value(ex_task* task, u32 frame_index, u32 local_index, u32* size);
const ex_type* ex_debug_local_type(ex_task* task, u32 frame_index, u32 local_index);

// Bytecode-owned unit metadata. Indices are valid only for this runtime's
// bytecode lifetime. These queries do not require suspension or a live module.
#define EX_DEBUG_UNIT_NONE ((u32)-1)
u32 ex_debug_unit_count(const ex_runtime* runtime);
u32 ex_debug_find_unit(const ex_runtime* runtime, ex_string_view source_name);
ex_string_view ex_debug_unit_source_name(const ex_runtime* runtime, u32 unit_index);
u32 ex_debug_unit_import_count(const ex_runtime* runtime, u32 unit_index);
// Returns the imported unit index, or EX_DEBUG_UNIT_NONE for invalid indices.
u32 ex_debug_unit_import(const ex_runtime* runtime, u32 unit_index, u32 import_index);
// Returns EX_DEBUG_UNIT_NONE for an invalid global index.
u32 ex_debug_global_unit(const ex_runtime* runtime, u32 global_index);

u32 ex_debug_global_count(ex_runtime* runtime);
ex_string_view ex_debug_global_name(ex_runtime* runtime, u32 global_index);
void* ex_debug_global_value(ex_runtime* runtime, u32 global_index, u32* size);
const ex_type* ex_debug_global_type(ex_runtime* runtime, u32 global_index);

#ifdef __cplusplus
}
#endif
