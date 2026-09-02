#pragma once

// C-friendly Evox API.
//
// This header exposes a compact C ABI around the C++ Evox
// implementation so C code can parse, typecheck, compile, and execute scripts
// without including any internal engine headers.
//
// Design notes:
// - `ex_module` owns parsed declarations, registered native functions, and the
//   string storage needed to keep copied names alive.
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

typedef struct ex_string_view {
	const char* begin;
	i64 length;
} ex_string_view;

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
	EX_TYPE_NULLABLE
} ex_type_kind;

// Generic status used by C API operations that only report success or failure.
// `EX_RESULT_SUSPENDED` (returned by calls interrupted by the debugger) is
// deliberately non-zero so `if (!result)` keeps meaning "failed".
typedef enum ex_result {
	EX_RESULT_FAILURE = 0,
	EX_RESULT_OK = 1,
	EX_RESULT_SUSPENDED = 2
} ex_result;

// Native print callback used by `ex_host`.
typedef void (*ex_print_fn)(void* userdata, ex_string_view msg);

// Import resolver used by `ex_module_compile`.
//
// Return non-zero on success and write the imported source into `*source`.
typedef int (*ex_import_resolver_fn)(void* userdata, ex_string_view path, ex_string_view alias, ex_string_view* source);

typedef struct ex_runtime ex_runtime;

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

// Native function callback used by `ex_runtime_set_native_function_callback`.
// Slice arguments and results in `frame` use the `ex_slice` representation
// above and occupy sizeof(ex_slice) bytes. Use EX_ARG/EX_RESULT with ex_slice
// to read or write them; the element type and element size come from the
// declared script signature.
typedef void (*ex_native_fn)(ex_runtime* runtime, ex_call_frame frame);

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
int ex_module_get_unit_count(ex_module* module);
ex_unit* ex_module_get_unit(ex_module* module, int index);
ex_string_view ex_unit_get_path(ex_unit* unit);
int ex_unit_get_native_function_count(ex_unit* unit);
ex_string_view ex_unit_get_native_function_name(ex_unit* unit, int index);

// Front-end pipeline helpers.
//
// `ex_module_parse` appends declarations into the module.
// `ex_module_typecheck` resolves and validates the current module contents.
// `ex_module_compile` performs parse + import resolution + typecheck in one
// call.
ex_result ex_module_parse(ex_module* module, ex_string_view source, ex_string_view source_name);

ex_result ex_module_typecheck(ex_module* module);

ex_result ex_module_compile(
	ex_module* module,
	ex_string_view source,
	ex_string_view source_name,
	ex_import_resolver_fn import_resolver,
	void* import_resolver_userdata
);

int ex_module_get_function_count(ex_module* module);
int ex_module_get_global_count(ex_module* module);

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

// Bytecode runtime lifetime.
//
// Bind a runtime to compiled bytecode to call script functions repeatedly.
// Pass a distinct host to use a separate runtime arena; null uses the bytecode host.
// Destroy it when execution is finished.
ex_runtime* ex_runtime_create(ex_bytecode* bytecode, ex_host* host);
void ex_runtime_destroy(ex_runtime* runtime);

ex_result ex_runtime_set_native_function_callback(
	ex_runtime* runtime,
	ex_unit* unit,
	int function_index,
	ex_native_fn callback
);

void ex_push_bool(ex_runtime* runtime, int value);
void ex_push_i32(ex_runtime* runtime, i32 value);
void ex_push_u32(ex_runtime* runtime, u32 value);
void ex_push_i64(ex_runtime* runtime, i64 value);
void ex_push_u64(ex_runtime* runtime, u64 value);
void ex_push_f32(ex_runtime* runtime, float value);
void ex_push_f64(ex_runtime* runtime, double value);
void ex_push_string(ex_runtime* runtime, ex_string_view value);
void ex_push_null(ex_runtime* runtime);
void ex_push_ptr(ex_runtime* runtime, void* value);

// Raw access to the most recent call result.
//
// Returns a pointer to the raw bytes of the value returned by the last
// executed function and writes their count to `*size`, or returns null (and
// writes 0) when there is no result. A slice value in these bytes is laid out
// as `ex_slice`: pointer first, then signed i64 element count. It is a
// non-owning view; the host must keep its backing storage alive. Non-extern
// struct layout is implementation-defined; extern struct fields use target C
// ABI layout. Hosts can read components at introspected offsets instead of
// relying on the positional `ex_to_*` helpers. The pointer is invalidated by
// the next push or
// call.
const void* ex_call_result(ex_runtime* runtime, u32* size);

i32 ex_to_bool(ex_runtime* runtime, i32 index);
i8  ex_to_i8 (ex_runtime* runtime, i32 index);
u8  ex_to_u8 (ex_runtime* runtime, i32 index);
i16 ex_to_i16(ex_runtime* runtime, i32 index);
u16 ex_to_u16(ex_runtime* runtime, i32 index);
i32 ex_to_i32(ex_runtime* runtime, i32 index);
u32 ex_to_u32(ex_runtime* runtime, i32 index);
i64 ex_to_i64(ex_runtime* runtime, i32 index);
u64 ex_to_u64(ex_runtime* runtime, i32 index);
float ex_to_f32(ex_runtime* runtime, i32 index);
double ex_to_f64(ex_runtime* runtime, i32 index);
ex_string_view ex_to_string(ex_runtime* runtime, i32 index);
void* ex_to_ptr(ex_runtime* runtime, i32 index);

// Execute a bytecode function by name.
//
// Push every declared argument onto the runtime stack with the `ex_push_*`
// helpers first. The call fails if fewer argument bytes are available.
// After the call, the return value is left on top of the runtime stack.
ex_result ex_call(ex_runtime* runtime, ex_string_view function_name);

// Execute a bytecode function by index.
ex_result ex_call_index(ex_runtime* runtime, i32 function_index);

// Query the declared return type of the function named `function_name`.
// Callers can then read the value from the runtime stack using the `ex_to_*`
// helpers with index `-1`.
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

// Returns the name of the type (struct name, enum name, etc.).
// Returns an empty string_view for anonymous or unnamed types.
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

// Debugger.
//
// Suspension-based: when a debug-enabled runtime pauses, the interrupted
// `ex_call` unwinds and returns `EX_RESULT_SUSPENDED` with the script state
// kept intact. The host queries `ex_debug_pause_event`, inspects state, then
// continues with `ex_debug_resume`. While suspended, don't push arguments or
// start new calls on the runtime.
//
// Calls through a function value (indirect calls) are ordinary script-to-
// script calls at the bytecode level and suspend normally, same as direct
// calls. Host-provided native callbacks are plain C function calls with no
// suspension support at all: none of this project's native functions call
// back into script, so this hasn't needed guarding, but a native callback
// that did call `ex_call`/`ex_call_index` reentrantly would be calling into
// an interpreter loop nested on the live C stack, which cannot suspend out
// from under it.
//
// Inspection calls are only valid while suspended; pointers they return are
// invalidated by resume.

typedef enum ex_debug_pause_reason {
	EX_DEBUG_PAUSE_BREAKPOINT = 0,
	EX_DEBUG_PAUSE_STEP,
	EX_DEBUG_PAUSE_ERROR,
} ex_debug_pause_reason;

typedef enum ex_debug_action {
	EX_DEBUG_CONTINUE = 0,
	EX_DEBUG_STEP_INTO,
	EX_DEBUG_STEP_OVER,
	EX_DEBUG_STEP_OUT,
	// Abort script execution; the interrupted `ex_call` fails.
	EX_DEBUG_ABORT,
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

int ex_debug_is_suspended(ex_runtime* runtime);

ex_result ex_debug_pause_event(ex_runtime* runtime, ex_debug_event* out_event);

// Re-enter the interpreter where it paused. Must be called on the script
// thread; fails when the runtime is not suspended. Returns like the original
// `ex_call`: `EX_RESULT_OK` with the result on the runtime stack,
// `EX_RESULT_SUSPENDED`, or `EX_RESULT_FAILURE`.
ex_result ex_debug_resume(ex_runtime* runtime, ex_debug_action action);

// Breakpoints. `line` is 1-based; the snapped statement line is written to
// `*resolved_line` (may be null). Fails when the source or line is unknown.
ex_result ex_debug_set_breakpoint(ex_bytecode* bytecode, ex_string_view source_name, u32 line, u32* resolved_line);
ex_result ex_debug_remove_breakpoint(ex_bytecode* bytecode, ex_string_view source_name, u32 line);
void ex_debug_remove_all_breakpoints(ex_bytecode* bytecode);

// Call stack inspection. Frame 0 is the innermost frame. Also valid
// immediately after a failed `ex_call`/`ex_call_index`, reporting the stack at
// the point of failure; the next call overwrites it.
u32 ex_debug_stack_depth(ex_runtime* runtime);
ex_string_view ex_debug_frame_function_name(ex_runtime* runtime, u32 frame_index);
ex_result ex_debug_frame_location(ex_runtime* runtime, u32 frame_index, ex_debug_location* out_location);

// Variable inspection. Locals enumerate the parameters and locals in scope at
// the frame's current statement. Values point at the raw bytes in live
// frame/global storage (runtime layout, see `ex_call_result`); writing through
// them mutates the running script.
u32 ex_debug_frame_local_count(ex_runtime* runtime, u32 frame_index);
ex_string_view ex_debug_local_name(ex_runtime* runtime, u32 frame_index, u32 local_index);
ex_type_kind ex_debug_local_kind(ex_runtime* runtime, u32 frame_index, u32 local_index);
void* ex_debug_local_value(ex_runtime* runtime, u32 frame_index, u32 local_index, u32* size);
const ex_type* ex_debug_local_type(ex_runtime* runtime, u32 frame_index, u32 local_index);

u32 ex_debug_global_count(ex_runtime* runtime);
ex_string_view ex_debug_global_name(ex_runtime* runtime, u32 global_index);
ex_type_kind ex_debug_global_kind(ex_runtime* runtime, u32 global_index);
void* ex_debug_global_value(ex_runtime* runtime, u32 global_index, u32* size);
const ex_type* ex_debug_global_type(ex_runtime* runtime, u32 global_index);

#ifdef __cplusplus
}
#endif
