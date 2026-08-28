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
// - Strings are passed as non-owning byte spans. The caller keeps the
//   underlying bytes alive for the duration of the call.
// - The ABI is intentionally plain C: no templates, references, or exceptions.

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef ASSERT
	#ifdef NDEBUG
		#define ASSERT(X)
	#elif defined LS_TESTS
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

typedef struct ls_string_view {
	const char* begin;
	i64 length;
} ls_string_view;

typedef struct ls_slice {
	const void* data;
	i64 length;
} ls_slice;

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
	LS_TYPE_UNTYPED_INT,
	LS_TYPE_UNTYPED_FLOAT,
	LS_TYPE_STRUCT,
	LS_TYPE_TAGGED_UNION,
	LS_TYPE_ENUM,
	LS_TYPE_FUNCTION,
	LS_TYPE_ARRAY,
	LS_TYPE_SLICE,
	LS_TYPE_NULL_VALUE,
	LS_TYPE_CPTR,
	LS_TYPE_NAMESPACE,
	LS_TYPE_NULLABLE
} ls_type_kind;

// Generic status used by C API operations that only report success or failure.
// `LS_RESULT_SUSPENDED` (returned by calls interrupted by the debugger) is
// deliberately non-zero so `if (!result)` keeps meaning "failed".
typedef enum ls_result {
	LS_RESULT_FAILURE = 0,
	LS_RESULT_OK = 1,
	LS_RESULT_SUSPENDED = 2
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

ls_string_view ls_arg_read_string(ls_call_frame* frame);

// Writes a string result into a native call frame. The bytes are copied into
// runtime-owned storage, so `value` only needs to remain valid for this call.
void ls_result_string(ls_runtime* runtime, ls_call_frame* frame, ls_string_view value);

#define LS_ARG(frame, type, name) type name; \
	do { \
		memcpy(&(name), (frame).args, sizeof(type)); \
		(frame).args += sizeof(type); \
	} while(0)

#define LS_STRING_ARG(frame, name) ls_string_view name = ls_arg_read_string(&(frame))

#define LS_RESULT(frame, value) do { \
	auto _ls_val = (value); \
	memcpy((frame).result, &_ls_val, sizeof(_ls_val)); \
	(frame).result += sizeof(_ls_val); \
} while(0)

#define LS_TYPED_RESULT(frame, type, value) do { \
	type _ls_val = (value); \
	memcpy((frame).result, &_ls_val, sizeof(_ls_val)); \
	(frame).result += sizeof(_ls_val); \
} while(0)

// Native function callback used by `ls_runtime_set_native_function_callback`.
// Slice arguments and results in `frame` use the `ls_slice` representation
// above and occupy sizeof(ls_slice) bytes. Use LS_ARG/LS_RESULT with ls_slice
// to read or write them; the element type and element size come from the
// declared script signature.
typedef void (*ls_native_fn)(ls_runtime* runtime, ls_call_frame frame);

typedef struct ls_arena {
	void* (*allocate)(void* user_data, size_t size, size_t align);
	void (*restore)(void* user_data, void* ptr);

	void* user_data;
} ls_arena;

// Host bridge shared by module creation, parsing, compilation, and runtime.
//
// - `arena` is used for every object created with this host
// - diagnostics hooks are used for error output
typedef struct ls_host {
	ls_arena arena;

	void* diagnostics_userdata;
	ls_print_fn print;
} ls_host;

// Opaque module/runtime handles.
//
// These are deliberately incomplete in the C ABI. Callers only pass pointers
// around; all ownership and implementation details remain inside LumScript.
typedef struct ls_module ls_module;
typedef struct ls_unit ls_unit;
typedef struct ls_bytecode ls_bytecode;
typedef struct ls_type ls_type;

// A typed attribute value. `value` points to the runtime-layout bytes of `type`
// and is owned by the module/bytecode that owns the inspected type.
typedef struct ls_attribute {
	const ls_type* type;
	const void* value;
} ls_attribute;

// Module lifetime.
//
// Create one module per script bundle or compilation unit. Destroy it when the
// compiled declarations and any runtime state are no longer needed.
ls_module* ls_module_create(ls_host* host);
void ls_module_destroy(ls_module* module);

// Native registration.
//
// Register custom native functions before typechecking or execution.
// Native types let scripts talk about engine objects by name, while native
// functions expose host behavior to scripts.
// Units and their native functions are available after a successful typecheck.
int ls_module_get_unit_count(ls_module* module);
ls_unit* ls_module_get_unit(ls_module* module, int index);
ls_string_view ls_unit_get_path(ls_unit* unit);
int ls_unit_get_native_function_count(ls_unit* unit);
ls_string_view ls_unit_get_native_function_name(ls_unit* unit, int index);

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

int ls_module_get_function_count(ls_module* module);
int ls_module_get_global_count(ls_module* module);

typedef struct ls_bytecode_compile_options {
	bool optimize;
} ls_bytecode_compile_options;

// Compile the checked module into bytecode through the IR pipeline.
ls_bytecode* ls_bytecode_compile(
	ls_module* module,
	ls_host* host,
	ls_bytecode_compile_options* options
);
void ls_bytecode_destroy(ls_bytecode* bytecode);

// Enumerate all types emitted into the bytecode. Returned type handles are
// stable until the bytecode is destroyed.
u32 ls_bytecode_type_count(const ls_bytecode* bytecode);
const ls_type* ls_bytecode_type(const ls_bytecode* bytecode, u32 index);

// Bytecode runtime lifetime.
//
// Bind a runtime to compiled bytecode to call script functions repeatedly.
// Pass a distinct host to use a separate runtime arena; null uses the bytecode host.
// Destroy it when execution is finished.
ls_runtime* ls_runtime_create(ls_bytecode* bytecode, ls_host* host);
void ls_runtime_destroy(ls_runtime* runtime);

ls_result ls_runtime_set_native_function_callback(
	ls_runtime* runtime,
	ls_unit* unit,
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
// writes 0) when there is no result. A slice value in these bytes is laid out
// as `ls_slice`: pointer first, then signed i64 element count. It is a
// non-owning view; the host must keep its backing storage alive. Non-extern
// struct layout is implementation-defined; extern struct fields use target C
// ABI layout. Hosts can read components at introspected offsets instead of
// relying on the positional `ls_to_*` helpers. The pointer is invalidated by
// the next push or
// call.
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
// Push every declared argument onto the runtime stack with the `ls_push_*`
// helpers first. The call fails if fewer argument bytes are available.
// After the call, the return value is left on top of the runtime stack.
ls_result ls_call(ls_runtime* runtime, ls_string_view function_name);

// Execute a bytecode function by index.
ls_result ls_call_index(ls_runtime* runtime, i32 function_index);

// Query the declared return type of the function named `function_name`.
// Callers can then read the value from the runtime stack using the `ls_to_*`
// helpers with index `-1`.
ls_type_kind ls_bytecode_runtime_result_kind(ls_runtime* runtime, ls_string_view function_name);

// Type introspection.
//
// `ls_type` describes the shape of a script value - its kind, byte size,
// and for compound types (struct, array, slice) the layout of their
// sub-values. Handles are owned by the bytecode and stable for its lifetime.
//
// Obtain a type handle from the debug API (ls_debug_local_type,
// ls_debug_global_type) or from module-level struct queries. The handle is
// valid while the owning bytecode (or bytecode-compiled module) lives.
//

// Returns the kind category of the type.
ls_type_kind ls_type_get_kind(const ls_type* type);

// Returns the name of the type (struct name, enum name, etc.).
// Returns an empty string_view for anonymous or unnamed types.
ls_string_view ls_type_get_name(const ls_type* type);

// Returns the byte size of values of this type. Matches the byte_size
// reported by ls_debug_local_value / ls_debug_global_value.
u32 ls_type_get_size(const ls_type* type);

// Returns the required byte alignment of values of this type.
u32 ls_type_get_alignment(const ls_type* type);

// Introspect a struct type (valid when kind == LS_TYPE_STRUCT).
// Fields are enumerated in declaration order. Non-extern struct layout is
// implementation-defined; extern structs use target C ABI layout.

// Number of fields in the struct.
u32 ls_type_struct_field_count(const ls_type* type);

// Name of the field at `field_index`.
ls_string_view ls_type_struct_field_name(const ls_type* type, u32 field_index);

// Type handle for the field at `field_index`. Recursively queryable for
// nested struct drill-down.
const ls_type* ls_type_struct_field_type(const ls_type* type, u32 field_index);

// Byte offset of the field from the start of the struct value. The host
// uses this to read the field: `(u8*)struct_value + offset`.
u32 ls_type_struct_field_offset(const ls_type* type, u32 field_index);

// Attributes.

// Number of attributes attached to a type.
u32 ls_type_attribute_count(const ls_type* type);

// Attribute value at `attribute_index`. The attribute's declaration type is
// returned in `.type`; its struct value is returned in `.value`. Use
// ls_type_get_size(result.type) for the value size. Returns { NULL, NULL } for
// an invalid index.
ls_attribute ls_type_attribute_value(const ls_type* type, u32 attribute_index);

// Number of attributes attached to the struct field at `field_index`.
// Returns 0 when `type` is not a struct or the field index is invalid.
u32 ls_type_struct_field_attribute_count(const ls_type* type, u32 field_index);

// Attribute value attached to a struct field. Returns { NULL, NULL } for an
// invalid index. Use ls_type_get_size(result.type) for the value size.
ls_attribute ls_type_struct_field_attribute_value(
	const ls_type* type,
	u32 field_index,
	u32 attribute_index
);

// Introspect a tagged union type (valid when kind == LS_TYPE_TAGGED_UNION).
// A tagged union value is stored as: [tag : i32] [payload : N bytes].
// The tag identifies which member is active (0, 1, 2, ...).
// All members share the same payload space (size = max member size).

// Number of member types in the union.
u32 ls_type_union_member_count(const ls_type* type);

// Type handle for the member at `member_index`.
const ls_type* ls_type_union_member_type(const ls_type* type, u32 member_index);

// Returns the active tag from a tagged union value. The tag is the first 4
// bytes of the value, interpreted as a signed i32.
i32 ls_type_union_tag(const ls_type* type, const void* value);

// Introspect an enum type (valid when kind == LS_TYPE_ENUM).

// Number of values (members) in the enum.
u32 ls_type_enum_value_count(const ls_type* type);

// Name of the enum value at `value_index`.
ls_string_view ls_type_enum_value_name(const ls_type* type, u32 value_index);

// Integer value of the enum value at `value_index`.
i32 ls_type_enum_value_value(const ls_type* type, u32 value_index);

// Introspect an array or slice type (valid when kind is LS_TYPE_ARRAY
// or LS_TYPE_SLICE).

// Element type of the array or slice.
const ls_type* ls_type_array_element_type(const ls_type* type);

// Compile-time element count. Returns the fixed length for LS_TYPE_ARRAY;
// returns 0 for LS_TYPE_SLICE (whose length is dynamic at runtime).
u32 ls_type_array_length(const ls_type* type);

// Returns whether the type is const-qualified.
bool ls_type_is_const(const ls_type* type);

// Introspect a nullable type (valid when kind == LS_TYPE_NULLABLE).
// A nullable value is stored as: [has_value : u8] [inner_value : N bytes].
// Read the first byte: 0 = null, 1 = value present.

// Inner (wrapped) type of the nullable.
const ls_type* ls_type_nullable_inner_type(const ls_type* type);

// Returns true when the nullable value is null (has_value byte is 0).
bool ls_type_nullable_is_null(const ls_type* type, const void* value);

// Returns a pointer past the has_value flag, i.e. to the inner value bytes.
// Only valid when ls_type_nullable_is_null returns false.
const void* ls_type_nullable_value_ptr(const ls_type* type, const void* value);

// Debugger.
//
// Suspension-based: when a debug-enabled runtime pauses, the interrupted
// `ls_call` unwinds and returns `LS_RESULT_SUSPENDED` with the script state
// kept intact. The host queries `ls_debug_pause_event`, inspects state, then
// continues with `ls_debug_resume`. While suspended, don't push arguments or
// start new calls on the runtime.
//
// Calls through a function value (indirect calls) are ordinary script-to-
// script calls at the bytecode level and suspend normally, same as direct
// calls. Host-provided native callbacks are plain C function calls with no
// suspension support at all: none of this project's native functions call
// back into script, so this hasn't needed guarding, but a native callback
// that did call `ls_call`/`ls_call_index` reentrantly would be calling into
// an interpreter loop nested on the live C stack, which cannot suspend out
// from under it.
//
// Inspection calls are only valid while suspended; pointers they return are
// invalidated by resume.

typedef enum ls_debug_pause_reason {
	LS_DEBUG_PAUSE_BREAKPOINT = 0,
	LS_DEBUG_PAUSE_STEP,
	LS_DEBUG_PAUSE_ERROR,
} ls_debug_pause_reason;

typedef enum ls_debug_action {
	LS_DEBUG_CONTINUE = 0,
	LS_DEBUG_STEP_INTO,
	LS_DEBUG_STEP_OVER,
	LS_DEBUG_STEP_OUT,
	// Abort script execution; the interrupted `ls_call` fails.
	LS_DEBUG_ABORT,
} ls_debug_action;

typedef struct ls_debug_location {
	ls_string_view source_name;
	u32 line;
	u32 column;
} ls_debug_location;

// `location` is the statement about to execute in the innermost frame.
typedef struct ls_debug_event {
	ls_debug_pause_reason reason;
	ls_debug_location location;
	// Failure description when `reason` is `LS_DEBUG_PAUSE_ERROR`.
	ls_string_view message;
} ls_debug_event;

int ls_debug_is_suspended(ls_runtime* runtime);

ls_result ls_debug_pause_event(ls_runtime* runtime, ls_debug_event* out_event);

// Re-enter the interpreter where it paused. Must be called on the script
// thread; fails when the runtime is not suspended. Returns like the original
// `ls_call`: `LS_RESULT_OK` with the result on the runtime stack,
// `LS_RESULT_SUSPENDED`, or `LS_RESULT_FAILURE`.
ls_result ls_debug_resume(ls_runtime* runtime, ls_debug_action action);

// Breakpoints. `line` is 1-based; the snapped statement line is written to
// `*resolved_line` (may be null). Fails when the source or line is unknown.
ls_result ls_debug_set_breakpoint(ls_bytecode* bytecode, ls_string_view source_name, u32 line, u32* resolved_line);
ls_result ls_debug_remove_breakpoint(ls_bytecode* bytecode, ls_string_view source_name, u32 line);
void ls_debug_remove_all_breakpoints(ls_bytecode* bytecode);

// Call stack inspection. Frame 0 is the innermost frame. Also valid
// immediately after a failed `ls_call`/`ls_call_index`, reporting the stack at
// the point of failure; the next call overwrites it.
u32 ls_debug_stack_depth(ls_runtime* runtime);
ls_string_view ls_debug_frame_function_name(ls_runtime* runtime, u32 frame_index);
ls_result ls_debug_frame_location(ls_runtime* runtime, u32 frame_index, ls_debug_location* out_location);

// Variable inspection. Locals enumerate the parameters and locals in scope at
// the frame's current statement. Values point at the raw bytes in live
// frame/global storage (runtime layout, see `ls_call_result`); writing through
// them mutates the running script.
u32 ls_debug_frame_local_count(ls_runtime* runtime, u32 frame_index);
ls_string_view ls_debug_local_name(ls_runtime* runtime, u32 frame_index, u32 local_index);
ls_type_kind ls_debug_local_kind(ls_runtime* runtime, u32 frame_index, u32 local_index);
void* ls_debug_local_value(ls_runtime* runtime, u32 frame_index, u32 local_index, u32* size);
const ls_type* ls_debug_local_type(ls_runtime* runtime, u32 frame_index, u32 local_index);

u32 ls_debug_global_count(ls_runtime* runtime);
ls_string_view ls_debug_global_name(ls_runtime* runtime, u32 global_index);
ls_type_kind ls_debug_global_kind(ls_runtime* runtime, u32 global_index);
void* ls_debug_global_value(ls_runtime* runtime, u32 global_index, u32* size);
const ls_type* ls_debug_global_type(ls_runtime* runtime, u32 global_index);

#ifdef __cplusplus
}
#endif
