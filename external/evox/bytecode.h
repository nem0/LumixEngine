#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "capi.h"

// Bytecode is intentionally simple: a byte-register VM with linear instruction
// streams and a flat function table. Constants are embedded directly in the
// stream.
//
// Encoding:
// - every instruction starts with one opcode byte (`ex_op`)
// - operands follow immediately in little-endian form
// - jumps store a signed 32-bit byte offset relative to the byte after the
//   operand
// - indices into tables are 32-bit unsigned values
// - `LOAD_CONST_N` embeds `N` raw bytes of inline payload after the opcode
// - the compiler chooses `N` from the literal width/type
// - runtime interpretation of the payload depends on the opcode and expected type
//
// Register contract:
// - register operands are byte offsets relative to the current frame base
// - parameter bytes occupy the first `param_size` frame bytes
// - local bytes follow parameters, and compiler temporaries follow locals
// - globals live at absolute byte offsets in the runtime memory buffer
// - aggregate values are copied as raw byte ranges
//
// Call contract:
// - the callee frame overlaps the caller frame: it starts at the caller's
//   argument register window, so the staged args are the callee's params in
//   place and no copy is made in either direction
// - consequently everything at or above the argument window must be dead in
//   the caller; the callee clobbers it with its locals and temps. Codegen
//   guarantees this by always staging args at the top of the live temp
//   stack.
// - RETURN deposits the result at the callee frame base, which is already
//   the caller's result location for direct calls; indirect calls relocate
//   the result down into the consumed function-value slot
// - RETURN_BASE is a zero-operand form for results already at the frame base
// - native callbacks still see arguments/results through the public runtime
//   stack helpers
//
// Addressing contract:
// - pointer arithmetic uses the ordinary arithmetic instructions
// - `BOUNDS_CHECK` validates a typed integer index register against a static length
//
// Slice contract:
// - a slice occupies `base, length` in stack memory
// - `base` is an absolute runtime memory address, so slices can alias caller locals
//   while passed to and returned from nested calls
// - `SLICE` reads `dst, source, begin, end` registers and writes a subslice
//   without modifying the source; element size follows the operands
// - `SLICE_REF` bounds-checks a typed integer index and writes the element
//   address to `dst` (the slice operand is read only); the type
//   kind byte of the index register follows the index register operand
// - `SLICE_EQ` reads two slice registers plus the element size and kind, and
//   writes a bool; `!=` is `SLICE_EQ` followed by `NOT`
// - all slice bounds are checked by the runtime
//
// Opcode layout summary:
// - constants: LOAD_CONST_N (`dst`, inline payload)
// - frame copies: COPY (`dst`, `src`, `byte size`)
// - pointers: FRAME_PTR and GLOBAL_PTR materialize pointers from immediate byte
//   offsets; LOAD_PTR and STORE_PTR handle indirect value access through a
//   pointer register plus an immediate byte offset
// - indexed access: LOAD_INDEXED_{8,16,32,64} and STORE_INDEXED_{8,16,32,64} combine bounds checking,
//   element scaling, address formation, and the memory access; their base
//   operand is a frame offset, not a pointer register. Their _IMM variants
//   take a constant index that the compiler has already validated against the
//   static array length, so they carry no index kind and no length and do no
//   runtime bounds checking
// - arithmetic/logical/comparison ops carry explicit destination and source
//   register operands; comparisons also carry a type byte
// - comparison branches carry lhs/rhs registers, a type byte, and a signed
//   relative jump offset; they do not materialize a boolean result
// - address/slice ops carry explicit register operands plus existing scale,
//   offset, and byte-size immediates
// - calls carry explicit destination, callee/argument registers, argument size,
//   and result size
// - jumps store signed 32-bit relative byte offsets; conditional jumps also
//   carry a condition register
//
// `PANIC` is a hard stop: the VM terminates execution immediately and reports
// its message slice.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ex_op {
	EX_OP_LOAD_CONST_1 = 1,
	EX_OP_LOAD_CONST_2,
	EX_OP_LOAD_CONST_4,
	EX_OP_LOAD_CONST_8,
	EX_OP_STRING_SLICE,
	EX_OP_COPY,
	EX_OP_FRAME_PTR,
	EX_OP_GLOBAL_PTR,
	EX_OP_LOAD_PTR,
	EX_OP_STORE_PTR,
	EX_OP_LOAD_INDEXED_8,
	EX_OP_LOAD_INDEXED_16,
	EX_OP_LOAD_INDEXED_32,
	EX_OP_LOAD_INDEXED_64,
	EX_OP_STORE_INDEXED_8,
	EX_OP_STORE_INDEXED_16,
	EX_OP_STORE_INDEXED_32,
	EX_OP_STORE_INDEXED_64,
	EX_OP_BOUNDS_CHECK,
	EX_OP_SLICE,
	EX_OP_SLICE_REF,
	EX_OP_SLICE_LOAD_8,
	EX_OP_SLICE_LOAD_16,
	EX_OP_SLICE_LOAD_32,
	EX_OP_SLICE_LOAD_64,
	EX_OP_SLICE_STORE_8,
	EX_OP_SLICE_STORE_16,
	EX_OP_SLICE_STORE_32,
	EX_OP_SLICE_STORE_64,
	EX_OP_SLICE_LENGTH,
	EX_OP_SLICE_EQ,

	EX_OP_ADD_8,
	EX_OP_ADD_16,
	EX_OP_ADD_32,
	EX_OP_ADD_64,
	EX_OP_ADD_F32,
	EX_OP_ADD_F64,

	EX_OP_SUB_8,
	EX_OP_SUB_16,
	EX_OP_SUB_32,
	EX_OP_SUB_64,
	EX_OP_SUB_F32,
	EX_OP_SUB_F64,

	EX_OP_MUL_8,
	EX_OP_MUL_16,
	EX_OP_MUL_32,
	EX_OP_MUL_64,
	EX_OP_MUL_F32,
	EX_OP_MUL_F64,
	// Fused multiply-add family: a single-rounding ternary float op per pair.
	// Operands are four u32 frame offsets: dst, lhs, rhs, addend.
	// - MADD:  dst =  lhs * rhs + addend
	// - MSUB:  dst =  lhs * rhs - addend
	// - NMADD: dst = -lhs * rhs + addend
	// - NMSUB: dst = -lhs * rhs - addend
	EX_OP_MADD_F32,
	EX_OP_MADD_F64,
	EX_OP_MSUB_F32,
	EX_OP_MSUB_F64,
	EX_OP_NMADD_F32,
	EX_OP_NMADD_F64,
	EX_OP_NMSUB_F32,
	EX_OP_NMSUB_F64,

	EX_OP_DIV_I8,
	EX_OP_DIV_U8,
	EX_OP_DIV_I16,
	EX_OP_DIV_U16,
	EX_OP_DIV_I32,
	EX_OP_DIV_U32,
	EX_OP_DIV_I64,
	EX_OP_DIV_U64,
	EX_OP_DIV_F32,
	EX_OP_DIV_F64,

	EX_OP_MOD_I8,
	EX_OP_MOD_U8,
	EX_OP_MOD_I16,
	EX_OP_MOD_U16,
	EX_OP_MOD_I32,
	EX_OP_MOD_U32,
	EX_OP_MOD_I64,
	EX_OP_MOD_U64,

	
	EX_OP_ADD_8_IMM,
	EX_OP_ADD_16_IMM,
	EX_OP_ADD_32_IMM,
	EX_OP_ADD_64_IMM,
	EX_OP_ADD_F32_IMM,
	EX_OP_ADD_F64_IMM,

	EX_OP_SUB_8_IMM,
	EX_OP_SUB_16_IMM,
	EX_OP_SUB_32_IMM,
	EX_OP_SUB_64_IMM,
	EX_OP_SUB_F32_IMM,
	EX_OP_SUB_F64_IMM,

	EX_OP_MUL_8_IMM,
	EX_OP_MUL_16_IMM,
	EX_OP_MUL_32_IMM,
	EX_OP_MUL_64_IMM,
	EX_OP_MUL_F32_IMM,
	EX_OP_MUL_F64_IMM,

	EX_OP_DIV_I8_IMM,
	EX_OP_DIV_U8_IMM,
	EX_OP_DIV_I16_IMM,
	EX_OP_DIV_U16_IMM,
	EX_OP_DIV_I32_IMM,
	EX_OP_DIV_U32_IMM,
	EX_OP_DIV_I64_IMM,
	EX_OP_DIV_U64_IMM,
	EX_OP_DIV_F32_IMM,
	EX_OP_DIV_F64_IMM,

	EX_OP_MOD_I8_IMM,
	EX_OP_MOD_U8_IMM,
	EX_OP_MOD_I16_IMM,
	EX_OP_MOD_U16_IMM,
	EX_OP_MOD_I32_IMM,
	EX_OP_MOD_U32_IMM,
	EX_OP_MOD_I64_IMM,
	EX_OP_MOD_U64_IMM,

	EX_OP_INC_I32,
	EX_OP_INC_I64,
	EX_OP_DEC_I32,
	EX_OP_DEC_I64,

	EX_OP_NEG_I8,
	EX_OP_NEG_U8,
	EX_OP_NEG_I16,
	EX_OP_NEG_U16,
	EX_OP_NEG_I32,
	EX_OP_NEG_U32,
	EX_OP_NEG_I64,
	EX_OP_NEG_U64,
	EX_OP_NEG_F32,
	EX_OP_NEG_F64,

	EX_OP_NOT,

	EX_OP_EQ,
	EX_OP_NE,
	EX_OP_LT,
	EX_OP_LE,
	EX_OP_GT,
	EX_OP_GE,
	#define EX_COMPARE_JUMP_OPS(TYPE) \
		EX_OP_JE_##TYPE, \
		EX_OP_JGE_##TYPE, \
		EX_OP_JGT_##TYPE, \
		EX_OP_JLT_##TYPE, \
		EX_OP_JLE_##TYPE, \
		EX_OP_JNE_##TYPE,
	EX_COMPARE_JUMP_OPS(I8)
	EX_COMPARE_JUMP_OPS(U8)
	EX_COMPARE_JUMP_OPS(I16)
	EX_COMPARE_JUMP_OPS(U16)
	EX_COMPARE_JUMP_OPS(I32)
	EX_COMPARE_JUMP_OPS(U32)
	EX_COMPARE_JUMP_OPS(I64)
	EX_COMPARE_JUMP_OPS(U64)
	EX_COMPARE_JUMP_OPS(F32)
	EX_COMPARE_JUMP_OPS(F64)
	#undef EX_COMPARE_JUMP_OPS

	EX_OP_JUMP,
	EX_OP_JZ_U8,
	EX_OP_JNZ_U8,
	EX_OP_JZ_I32,
	EX_OP_JZ_I64,
	EX_OP_JNZ_I32,
	EX_OP_JNZ_I64,
	EX_OP_JGZ_I32,
	EX_OP_JGZ_I64,
	EX_OP_JGEZ_I32,
	EX_OP_JGEZ_I64,
	EX_OP_JLTZ_I32,
	EX_OP_JLTZ_I64,
	EX_OP_JLEZ_I32,
	EX_OP_JLEZ_I64,

	EX_OP_CALL_DIRECT,
	EX_OP_CALL_NATIVE,
	EX_OP_CALL_INDIRECT,
	EX_OP_CAST,
	EX_OP_RETURN,
	EX_OP_RETURN_BASE,

	// Debugger breakpoint trap. Never emitted by the compiler; written in place
	// of a statement's first opcode byte by `ex_debug_set_breakpoint`, which
	// saves the original byte so the patch can be reversed.
	EX_OP_BREAK,
	// Terminates execution and reports the UTF-8 slice in its register operand.
	EX_OP_PANIC,
} ex_op;

typedef enum ex_function_kind {
	EX_FUNCTION_SCRIPT = 0,
	EX_FUNCTION_NATIVE,
} ex_function_kind;

// One source position in the bytecode's location table (the PDB "file table"
// model). source_name is copied into the bytecode arena, so the bytecode is
// self-contained after compilation.
typedef struct ex_bytecode_location {
	ex_string_view source_name;
	u32 line;
	u32 column;
} ex_bytecode_location;

typedef struct ex_bytecode_source_map_entry {
	// Byte offset of the first instruction associated with this source location.
	u32 code_offset;
	// Index into ex_bytecode::locations[] (0-based; never EX_INVALID_SOURCE_LOC
	// since recordSourceMap skips locations tied to no source position).
	u32 location_index;
} ex_bytecode_source_map_entry;

// Index into ex_bytecode::type_info, or this sentinel when no type metadata
// is available (e.g. compiler temporaries).
#define EX_TYPE_INDEX_NONE 0xffffffffu

// Describes one field of a struct type. Used at debug time to enumerate
// struct members and compute their byte offsets.
typedef struct ex_type_field_info {
	ex_string_view name;
	u32 type_index;   // index into the owning bytecode's type_info[]
	u32 offset;       // byte offset from the start of the struct value
	u32 first_attribute_index;
	u32 attribute_count;
} ex_type_field_info;

typedef struct ex_type_attribute_info {
	u32 type_index;
	const void* value;
} ex_type_attribute_info;

// Describes one value of an enum type. Used at debug time to enumerate
// enum value names and their integer values.
typedef struct ex_type_enum_value_info {
	ex_string_view name;
	i32 value;
} ex_type_enum_value_info;

// The `const ex_type*` handle returned by the C API (capi.h) points directly
// into the bytecode's type_info[] array.  The struct is fully defined here
// (not opaque) so that debugger.c and runtime.c can access the members.
// Host code only ever sees `const ex_type*` through the public API.
typedef struct ex_type {
	const struct ex_bytecode* bytecode;  // owning bytecode (for field/type lookups)
	ex_type_kind kind;
	u32 byte_size;               // same as typeByteSize for this type
	u32 alignment;               // required alignment of values of this type
	u32 field_count;             // 0 when kind != EX_TYPE_STRUCT
	u32 first_field_index;       // index into bytecode->type_fields[]; unused when field_count == 0
	u32 attribute_count;
	u32 first_attribute_index;
	u32 member_count;            // 0 when kind != EX_TYPE_TAGGED_UNION
	u32 first_member_index;      // index into bytecode->type_member_indices[]; unused when member_count == 0
	u32 value_count;             // 0 when kind != EX_TYPE_ENUM
	u32 first_value_index;       // index into bytecode->type_enum_values[]; unused when value_count == 0
	u32 element_type_index;      // EX_TYPE_INDEX_NONE when kind is not ARRAY, SLICE, NULLABLE or POINTER
	u32 array_length;            // EX_TYPE_INDEX_NONE when not ARRAY or SLICE; 0 for SLICE (dynamic length)
	bool is_const;                // true for const slices
	ex_string_view name;         // type name (struct name, enum name, etc.), empty if anonymous or unnamed
} ex_type;

// Debug-only description of one parameter or local's storage, used by
// `ex_debug_frame_local_*`. Frame offsets are never reused within a
// function's local allocation state,
// so a local's live range is approximated as [scope_begin_offset, end of
// function) rather than tracking true block-scope exit: it may be reported
// as "in scope" past the end of the if/while/for block that actually
// declared it, but the frame byte offset always holds a value of the right
// type and size for that slot, so this is imprecise, never unsafe.
typedef struct ex_bytecode_local_debug_entry {
	ex_string_view name;
	// Byte offset into the callee's frame (same space as instruction register
	// operands, i.e. relative to `ex_runtime::frame`).
	u32 offset;
	u32 byte_size;
	ex_type_kind kind;
	// Index into ex_bytecode::type_info[], or EX_TYPE_INDEX_NONE.
	// Lets the debugger resolve full type metadata (struct fields, array
	// element types, etc.) from the flat debug kind.
	u32 type_index;
	// Bytecode offset (in the owning function's `code`) of the first
	// instruction at which this local is live. Parameters are live from 0.
	u32 scope_begin_offset;
} ex_bytecode_local_debug_entry;

typedef struct ex_function_bc {
	ex_string_view name;
	ex_function_kind kind;
	bool is_builtin_native;

	// Parameter/result/frame sizes are measured in raw bytes.
	u32 param_size;
	u32 return_size;
	u32 frame_size;

	ex_type_kind return_kind;
	u8* code;
	u32 code_size;

	// Sorted by code_offset. Entries describe the source location for the
	// instruction at code_offset until the next entry.
	ex_bytecode_source_map_entry* source_map;
	u32 source_map_count;

	// Named parameters and locals, declaration order. Compiler temporaries
	// (addLocal calls with no slot out-param) have no entry here.
	ex_bytecode_local_debug_entry* locals;
	u32 local_count;
} ex_function_bc;

// Debug-only description of one global's storage, used by `ex_debug_global_*`.
typedef struct ex_bytecode_global_debug_entry {
	ex_string_view name;
	// Byte offset into the runtime's global memory region (`ex_runtime`'s
	// stack, bytes [0, ex_bytecode::global_size)).
	u32 offset;
	u32 byte_size;
	ex_type_kind kind;
	u32 type_index;  // EX_TYPE_INDEX_NONE when no type metadata
} ex_bytecode_global_debug_entry;

// One active breakpoint patch. `code` was `original_byte` before being
// overwritten with `EX_OP_BREAK`.
typedef struct ex_bytecode_breakpoint {
	u8* code;
	u8 original_byte;
} ex_bytecode_breakpoint;

typedef struct ex_bytecode {
	ex_host* host;
	ex_arena* arena;

	ex_function_bc* functions;
	u32 function_count;
	u32 function_capacity;

	u32 global_size;
	bool has_global_init;

	ex_string_view* strings;
	u32 string_count;

	// Deduplicated source locations referenced by the functions' source maps.
	// `ex_bytecode_source_map_entry::location_index` indexes here.
	ex_bytecode_location* locations;
	u32 location_count;

	// One entry per named global in declaration order. Compiler temporaries
	// and the synthetic global-initializer function are not globals and have
	// no entry here.
	ex_bytecode_global_debug_entry* global_debug;
	u32 global_debug_count;

	// Type metadata for debugger introspection. Each entry describes one
	// unique type (primitives, structs, arrays, slices). Entries are stable
	// by pointer for the bytecode's lifetime - the `const ex_type*` returned
	// by public API functions points directly into these arrays.
	// `type_fields` is a flat array of struct field descriptors, indexed by
	// `ex_type::first_field_index` + field offset.
	ex_type* type_info;
	u32 type_info_count;
	u32 type_info_capacity;
	ex_type_field_info* type_fields;
	u32 type_field_count;
	u32 type_field_capacity;
	ex_type_attribute_info* type_attributes;
	u32 type_attribute_count;
	u32 type_attribute_capacity;

	// Flat array of type indices for tagged union members, indexed by
	// `ex_type::first_member_index` + member offset.
	u32* type_member_indices;
	u32 type_member_count;
	u32 type_member_capacity;

	// Flat array of enum value descriptors, indexed by
	// `ex_type::first_value_index` + value index.
	ex_type_enum_value_info* type_enum_values;
	u32 type_enum_value_count;
	u32 type_enum_value_capacity;

	// Active breakpoint patches, set by `ex_debug_set_breakpoint`. Directly
	// malloc'd/realloc'd and freed in `ex_bytecode_destroy`, same as this
	// struct itself and `ex_runtime::stack`/`native_callbacks`: the arena's
	// `restore` only rewinds to an earlier watermark (LIFO), which doesn't fit
	// entries added/removed in arbitrary order across a debug session, so
	// object-owned mutable collections like this one bypass the arena.
	// Shared by every `ex_runtime` bound to this bytecode, since the patch
	// lives in `ex_function_bc::code` itself.
	ex_bytecode_breakpoint* breakpoints;
	u32 breakpoint_count;
	u32 breakpoint_capacity;
} ex_bytecode;

#define EX_MAX_CALL_DEPTH 4096u

typedef struct runtime_call_frame {
	const ex_function_bc* function;
	const u8* ip;
	u8* frame;
	u8* stack_top;
} runtime_call_frame;

typedef struct runtime_restore_point {
	u8* frame;
	u8* stack_top;
	u32 result_size;
	u32 call_depth;
} runtime_restore_point;

typedef struct runtime_step_trap {
	u8* code;
	u8 original_byte;
} runtime_step_trap;

typedef struct ex_runtime {
	ex_host* host;
	const ex_bytecode* bytecode;
	ex_arena* arena;

	// One past the topmost live stack value.
	u8* stack_top;
	// Cached pointer to the current call frame.
	u8* frame;

	u8* stack;
	u8* stack_end;

	// Byte count of the last call result at the top of the stack. Host pushes
	// invalidate it; zero represents no result or a void result.
	u32 result_size;

	runtime_call_frame call_stack[EX_MAX_CALL_DEPTH];
	u32 call_depth;

	// Indexed by bytecode function index.
	ex_native_fn* native_callbacks;
	u32 native_callback_count;

	// Snapshot of the call stack at the point of the most recent
	// `ex_call`/`ex_call_index` failure, innermost frame first. Overwritten by
	// the next call; `fail_frame_count` is 0 when the last call succeeded.
	runtime_call_frame fail_frames[EX_MAX_CALL_DEPTH + 1u];
	u32 fail_frame_count;

	// Debugger suspension. See `ex_debug_resume` in capi.h.
	//
	// True while a debug-enabled call is parked instead of having returned.
	// `call_stack[0..call_depth)` holds ancestor frames exactly as during
	// normal execution; `suspended_frame` holds the innermost (currently
	// "executing") frame's resume point, since that one only lived in the
	// interpreter loop's locals and would otherwise be lost when the loop's
	// C stack frame returns to the host.
	bool is_suspended;
	runtime_call_frame suspended_frame;
	// Runtime state at the beginning of each active host call. An entry stays
	// live while that call is suspended, so nested script -> native -> script
	// calls cannot overwrite their caller's restore point.
	runtime_restore_point call_starts[EX_MAX_CALL_DEPTH];
	u32 call_start_depth;
	// Reported by `ex_debug_pause_event` while suspended.
	ex_debug_event pause_event;

	// Stepping, armed by `ex_debug_resume(EX_DEBUG_STEP_*)`. `step_action` is
	// `EX_DEBUG_CONTINUE` (0, the calloc default) when no step is in
	// progress. While armed, the interpreter suspends with
	// `EX_DEBUG_PAUSE_STEP` at the first source line or call depth reached that
	// differs from the step start and satisfies the action's call-depth rule,
	// measured against `step_start_call_depth` (`runtime->call_depth` at the
	// moment the step began): STEP_INTO has no depth rule (any depth stops
	// it), STEP_OVER requires `call_depth <= step_start_call_depth` (calls
	// made from the starting statement run to completion without stopping
	// inside them), STEP_OUT requires `call_depth < step_start_call_depth`
	// (stop only after returning to a shallower frame).
	ex_debug_action step_action;
	u32 step_start_line;
	u32 step_start_call_depth;
	// Temporary EX_OP_BREAK patches installed for one step action. They are
	// runtime-owned so user breakpoints in ex_bytecode remain independent.
	runtime_step_trap* step_traps;
	u32 step_trap_count;
	u32 step_trap_capacity;
} ex_runtime;

ex_result ex_runtime_set_native_function_callback_by_bytecode_index(
	ex_runtime* runtime,
	int bytecode_index,
	ex_native_fn callback
);

// Re-enters the interpreter at `runtime->suspended_frame`. Internal entry
// point used by `ex_debug_resume` (debugger.c); not part of the public C ABI.
// Fails immediately when the runtime is not suspended. Returns like
// `ex_call`: OK with the result on the stack, SUSPENDED, or FAILURE.
ex_result ex_runtime_resume_suspended(ex_runtime* runtime);

#ifdef __cplusplus
}
#endif
