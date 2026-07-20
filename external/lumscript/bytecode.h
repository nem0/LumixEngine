#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "capi.h"

// Bytecode is intentionally simple: a byte-register VM with linear instruction
// streams and a flat function table. Constants are embedded directly in the
// stream.
//
// Encoding:
// - every instruction starts with one opcode byte (`ls_op`)
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
//   stack. Hand-written bytecode must uphold the same invariant.
// - RETURN deposits the result at the callee frame base, which is already
//   the caller's result location for direct calls; indirect calls relocate
//   the result down into the consumed function-value slot
// - RETURN_BASE is a zero-operand form for results already at the frame base
// - native callbacks still see arguments/results through the public runtime
//   stack helpers
//
// Addressing contract:
// - `LOAD_INDEXED` and `STORE_INDEXED` copy an explicit byte width
// - they read explicit base reference, index, and value/result registers
// - they compute `base + index * scale + offset`, then load/store bytes
// - `scale` and `offset` are encoded as immediates
// - this is intended to cover nested field access and array indexing through
//   one generic form
// - `BOUNDS_CHECK` validates an explicit index register against a static length
//
// Slice contract:
// - a slice occupies `base, length` in stack memory
// - `base` is an absolute runtime memory address, so slices can alias caller locals
//   while passed to and returned from nested calls
// - `SLICE` reads `base, length, begin, end` registers and writes a subslice
// - `SLICE_*_LOCAL` reads/writes slice, index, and value/result frame registers
// - `SLICE_*_AT_LOCAL` additionally carries element and field byte ranges
// - all slice bounds are checked by the runtime
//
// Opcode layout summary:
// - constants: LOAD_CONST_N (`dst`, inline payload), LOAD_CONST_STRING
//   (`dst`, string index)
// - frame copies: COPY (`dst`, `src`, `byte size`)
// - global access: GLOBAL_LOAD/GLOBAL_STORE carry destination/source register,
//   global byte offset, and byte size
// - refs: LOCAL_REF/GLOBAL_REF (`dst`, byte offset)
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
// `ABORT` is a hard stop: the VM terminates execution immediately.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ls_op {
	LS_OP_LOAD_CONST_1 = 1,
	LS_OP_LOAD_CONST_2,
	LS_OP_LOAD_CONST_4,
	LS_OP_LOAD_CONST_8,
	LS_OP_LOAD_CONST_STRING,
	LS_OP_COPY,
	LS_OP_GLOBAL_LOAD,
	LS_OP_GLOBAL_STORE,
	LS_OP_LOCAL_REF,
	LS_OP_GLOBAL_REF,

	LS_OP_LOAD_INDEXED,
	LS_OP_STORE_INDEXED,
	LS_OP_LOAD_INDEXED_LOCAL_I32,
	LS_OP_STORE_INDEXED_LOCAL_I32,
	LS_OP_COPY_AT_LOCAL_I32,
	LS_OP_REF_INDEXED,
	LS_OP_BOUNDS_CHECK,
	LS_OP_SLICE,
	LS_OP_SLICE_LOAD_LOCAL,
	LS_OP_SLICE_STORE_LOCAL,
	LS_OP_SLICE_LOAD_LOCAL_I32,
	LS_OP_SLICE_STORE_LOCAL_I32,
	LS_OP_SLICE_LOAD_AT_LOCAL,
	LS_OP_SLICE_STORE_AT_LOCAL,
	LS_OP_SLICE_LOAD_AT_LOCAL_I32,
	LS_OP_SLICE_STORE_AT_LOCAL_I32,
	LS_OP_SLICE_REF,
	LS_OP_SLICE_LENGTH,

	LS_OP_ADD_I8,
	LS_OP_ADD_U8,
	LS_OP_ADD_I16,
	LS_OP_ADD_U16,
	LS_OP_ADD_I32,
	LS_OP_ADD_U32,
	LS_OP_ADD_I64,
	LS_OP_ADD_U64,
	LS_OP_ADD_F32,
	LS_OP_ADD_F64,

	LS_OP_SUB_I8,
	LS_OP_SUB_U8,
	LS_OP_SUB_I16,
	LS_OP_SUB_U16,
	LS_OP_SUB_I32,
	LS_OP_SUB_U32,
	LS_OP_SUB_I64,
	LS_OP_SUB_U64,
	LS_OP_SUB_F32,
	LS_OP_SUB_F64,

	LS_OP_MUL_I8,
	LS_OP_MUL_U8,
	LS_OP_MUL_I16,
	LS_OP_MUL_U16,
	LS_OP_MUL_I32,
	LS_OP_MUL_U32,
	LS_OP_MUL_I64,
	LS_OP_MUL_U64,
	LS_OP_MUL_F32,
	LS_OP_MUL_F64,

	LS_OP_DIV_I8,
	LS_OP_DIV_U8,
	LS_OP_DIV_I16,
	LS_OP_DIV_U16,
	LS_OP_DIV_I32,
	LS_OP_DIV_U32,
	LS_OP_DIV_I64,
	LS_OP_DIV_U64,
	LS_OP_DIV_F32,
	LS_OP_DIV_F64,

	LS_OP_MOD_I8,
	LS_OP_MOD_U8,
	LS_OP_MOD_I16,
	LS_OP_MOD_U16,
	LS_OP_MOD_I32,
	LS_OP_MOD_U32,
	LS_OP_MOD_I64,
	LS_OP_MOD_U64,
	LS_OP_INC_I32,
	LS_OP_INC_I64,
	LS_OP_DEC_I32,
	LS_OP_DEC_I64,

	LS_OP_NEG_I8,
	LS_OP_NEG_U8,
	LS_OP_NEG_I16,
	LS_OP_NEG_U16,
	LS_OP_NEG_I32,
	LS_OP_NEG_U32,
	LS_OP_NEG_I64,
	LS_OP_NEG_U64,
	LS_OP_NEG_F32,
	LS_OP_NEG_F64,

	LS_OP_NOT,

	LS_OP_EQ,
	LS_OP_NE,
	LS_OP_LT,
	LS_OP_LE,
	LS_OP_GT,
	LS_OP_GE,
	#define LS_COMPARE_JUMP_OPS(TYPE) \
		LS_OP_JE_##TYPE, \
		LS_OP_JGE_##TYPE, \
		LS_OP_JGT_##TYPE, \
		LS_OP_JLT_##TYPE, \
		LS_OP_JLE_##TYPE,
	LS_COMPARE_JUMP_OPS(I8)
	LS_COMPARE_JUMP_OPS(U8)
	LS_COMPARE_JUMP_OPS(I16)
	LS_COMPARE_JUMP_OPS(U16)
	LS_COMPARE_JUMP_OPS(I32)
	LS_COMPARE_JUMP_OPS(U32)
	LS_COMPARE_JUMP_OPS(I64)
	LS_COMPARE_JUMP_OPS(U64)
	LS_COMPARE_JUMP_OPS(F32)
	LS_COMPARE_JUMP_OPS(F64)
	#undef LS_COMPARE_JUMP_OPS

	LS_OP_JE_STRING,

	LS_OP_JUMP,
	LS_OP_JZ_U8,
	LS_OP_JNZ_U8,
	LS_OP_JZ_I32,
	LS_OP_JZ_I64,
	LS_OP_JNZ_I32,
	LS_OP_JNZ_I64,
	LS_OP_JGZ_I32,
	LS_OP_JGZ_I64,
	LS_OP_JGEZ_I32,
	LS_OP_JGEZ_I64,
	LS_OP_JLTZ_I32,
	LS_OP_JLTZ_I64,
	LS_OP_JLEZ_I32,
	LS_OP_JLEZ_I64,

	LS_OP_CALL_DIRECT,
	LS_OP_CALL_INDIRECT,
	LS_OP_CAST,
	LS_OP_STRING_TO_CSTR,
	LS_OP_CSTR_TO_STRING,
	LS_OP_RETURN,
	LS_OP_RETURN_BASE,

	// Debugger breakpoint trap. Never emitted by the compiler; written in place
	// of a statement's first opcode byte by `ls_debug_set_breakpoint`, which
	// saves the original byte so the patch can be reversed.
	LS_OP_BREAK,
} ls_op;

typedef enum ls_function_kind {
	LS_FUNCTION_SCRIPT = 0,
	LS_FUNCTION_NATIVE,
} ls_function_kind;

typedef struct ls_bytecode_source_map_entry {
	// Byte offset of the first instruction associated with this source location.
	u32 code_offset;
	// Non-owning source identifier copied from the AST token.
	ls_string_view source_name;
	u32 line;
	u32 column;
} ls_bytecode_source_map_entry;

// Index into ls_bytecode::type_info, or this sentinel when no type metadata
// is available (e.g. compiler temporaries).
#define LS_TYPE_INDEX_NONE 0xffffffffu

// Describes one field of a struct type. Used at debug time to enumerate
// struct members and compute their byte offsets.
typedef struct ls_type_field_info {
	ls_string_view name;
	u32 type_index;   // index into the owning bytecode's type_info[]
	u32 offset;       // byte offset from the start of the struct value
} ls_type_field_info;

// The `const ls_type*` handle returned by the C API (capi.h) points directly
// into the bytecode's type_info[] array.  The struct is fully defined here
// (not opaque) so that debugger.c and runtime.c can access the members.
// Host code only ever sees `const ls_type*` through the public API.
typedef struct ls_type {
	const struct ls_bytecode* bytecode;  // owning bytecode (for field/type lookups)
	ls_type_kind kind;
	u32 byte_size;               // same as typeByteSize for this type
	u32 field_count;             // 0 when kind != LS_TYPE_STRUCT
	u32 first_field_index;       // index into bytecode->type_fields[]; unused when field_count == 0
	u32 member_count;            // 0 when kind != LS_TYPE_TAGGED_UNION
	u32 first_member_index;      // index into bytecode->type_member_indices[]; unused when member_count == 0
	u32 element_type_index;      // LS_TYPE_INDEX_NONE when kind is not ARRAY, SLICE, or NULLABLE
	u32 array_length;            // LS_TYPE_INDEX_NONE when not ARRAY or SLICE; 0 for SLICE (dynamic length)
} ls_type;

// Debug-only description of one parameter or local's storage, used by
// `ls_debug_frame_local_*`. Frame offsets are never reused within a
// function (see FunctionCompiler::next_local_offset in bytecode_compiler.cpp),
// so a local's live range is approximated as [scope_begin_offset, end of
// function) rather than tracking true block-scope exit: it may be reported
// as "in scope" past the end of the if/while/for block that actually
// declared it, but the frame byte offset always holds a value of the right
// type and size for that slot, so this is imprecise, never unsafe.
typedef struct ls_bytecode_local_debug_entry {
	ls_string_view name;
	// Byte offset into the callee's frame (same space as instruction register
	// operands, i.e. relative to `ls_runtime::frame`).
	u32 offset;
	u32 byte_size;
	ls_type_kind kind;
	// Index into ls_bytecode::type_info[], or LS_TYPE_INDEX_NONE.
	// Lets the debugger resolve full type metadata (struct fields, array
	// element types, etc.) from the flat debug kind.
	u32 type_index;
	// Bytecode offset (in the owning function's `code`) of the first
	// instruction at which this local is live. Parameters are live from 0.
	u32 scope_begin_offset;
} ls_bytecode_local_debug_entry;

typedef struct ls_function_bc {
	ls_string_view name;
	ls_function_kind kind;
	bool is_builtin_native;

	// Parameter/result/frame sizes are measured in raw bytes.
	u32 param_size;
	u32 return_size;
	u32 frame_size;

	ls_type_kind return_kind;
	u8* code;
	u32 code_size;

	// Sorted by code_offset. Entries describe the source location for the
	// instruction at code_offset until the next entry.
	ls_bytecode_source_map_entry* source_map;
	u32 source_map_count;

	// Named parameters and locals, declaration order. Compiler temporaries
	// (addLocal calls with no slot out-param) have no entry here.
	ls_bytecode_local_debug_entry* locals;
	u32 local_count;
} ls_function_bc;

// Debug-only description of one global's storage, used by `ls_debug_global_*`.
typedef struct ls_bytecode_global_debug_entry {
	ls_string_view name;
	// Byte offset into the runtime's global memory region (`ls_runtime`'s
	// stack, bytes [0, ls_bytecode::global_size)).
	u32 offset;
	u32 byte_size;
	ls_type_kind kind;
	u32 type_index;  // LS_TYPE_INDEX_NONE when no type metadata
} ls_bytecode_global_debug_entry;

// One active breakpoint patch. `code` was `original_byte` before being
// overwritten with `LS_OP_BREAK`.
typedef struct ls_bytecode_breakpoint {
	u8* code;
	u8 original_byte;
} ls_bytecode_breakpoint;

typedef struct ls_bytecode {
	ls_host* host;
	ls_arena* arena;

	ls_function_bc* functions;
	u32 function_count;
	u32 function_capacity;

	u32 global_size;
	bool has_global_init;

	ls_string_view* strings;
	u32 string_count;
	u32 string_capacity;

	// One entry per named global in declaration order. Compiler temporaries
	// and the synthetic global-initializer function are not globals and have
	// no entry here.
	ls_bytecode_global_debug_entry* global_debug;
	u32 global_debug_count;

	// Type metadata for debugger introspection. Each entry describes one
	// unique type (primitives, structs, arrays, slices). Entries are stable
	// by pointer for the bytecode's lifetime - the `const ls_type*` returned
	// by public API functions points directly into these arrays.
	// `type_fields` is a flat array of struct field descriptors, indexed by
	// `ls_type::first_field_index` + field offset.
	ls_type* type_info;
	u32 type_info_count;
	u32 type_info_capacity;
	ls_type_field_info* type_fields;
	u32 type_field_count;
	u32 type_field_capacity;

	// Flat array of type indices for tagged union members, indexed by
	// `ls_type::first_member_index` + member offset.
	u32* type_member_indices;
	u32 type_member_count;
	u32 type_member_capacity;

	// Active breakpoint patches, set by `ls_debug_set_breakpoint`. Directly
	// malloc'd/realloc'd and freed in `ls_bytecode_destroy`, same as this
	// struct itself and `ls_runtime::stack`/`native_callbacks`: the arena's
	// `restore` only rewinds to an earlier watermark (LIFO), which doesn't fit
	// entries added/removed in arbitrary order across a debug session, so
	// object-owned mutable collections like this one bypass the arena.
	// Shared by every `ls_runtime` bound to this bytecode, since the patch
	// lives in `ls_function_bc::code` itself.
	ls_bytecode_breakpoint* breakpoints;
	u32 breakpoint_count;
	u32 breakpoint_capacity;
} ls_bytecode;

#define LS_MAX_CALL_DEPTH 4096u

typedef struct runtime_call_frame {
	const ls_function_bc* function;
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

typedef struct ls_runtime {
	ls_host* host;
	const ls_bytecode* bytecode;
	ls_arena* arena;

	// One past the topmost live stack value.
	u8* stack_top;
	// Cached pointer to the current call frame.
	u8* frame;

	u8* stack;
	u8* stack_end;

	// Byte count of the last call result at the top of the stack. Host pushes
	// invalidate it; zero represents no result or a void result.
	u32 result_size;

	runtime_call_frame call_stack[LS_MAX_CALL_DEPTH];
	u32 call_depth;

	// Indexed by bytecode function index.
	ls_native_fn* native_callbacks;
	u32 native_callback_count;

	// Snapshot of the call stack at the point of the most recent
	// `ls_call`/`ls_call_index` failure, innermost frame first. Overwritten by
	// the next call; `fail_frame_count` is 0 when the last call succeeded.
	runtime_call_frame fail_frames[LS_MAX_CALL_DEPTH + 1u];
	u32 fail_frame_count;

	// Debugger suspension. See `ls_debug_resume` in capi.h.
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
	runtime_restore_point call_starts[LS_MAX_CALL_DEPTH];
	u32 call_start_depth;
	// Reported by `ls_debug_pause_event` while suspended.
	ls_debug_event pause_event;

	// Stepping, armed by `ls_debug_resume(LS_DEBUG_STEP_*)`. `step_action` is
	// `LS_DEBUG_CONTINUE` (0, the calloc default) when no step is in
	// progress. While armed, the interpreter suspends with
	// `LS_DEBUG_PAUSE_STEP` at the first source line or call depth reached that
	// differs from the step start and satisfies the action's call-depth rule,
	// measured against `step_start_call_depth` (`runtime->call_depth` at the
	// moment the step began): STEP_INTO has no depth rule (any depth stops
	// it), STEP_OVER requires `call_depth <= step_start_call_depth` (calls
	// made from the starting statement run to completion without stopping
	// inside them), STEP_OUT requires `call_depth < step_start_call_depth`
	// (stop only after returning to a shallower frame).
	ls_debug_action step_action;
	u32 step_start_line;
	u32 step_start_call_depth;
	// Temporary LS_OP_BREAK patches installed for one step action. They are
	// runtime-owned so user breakpoints in ls_bytecode remain independent.
	runtime_step_trap* step_traps;
	u32 step_trap_count;
	u32 step_trap_capacity;
} ls_runtime;

ls_result ls_runtime_set_native_function_callback_by_bytecode_index(
	ls_runtime* runtime,
	int bytecode_index,
	ls_native_fn callback
);

// Re-enters the interpreter at `runtime->suspended_frame`. Internal entry
// point used by `ls_debug_resume` (debugger.c); not part of the public C ABI.
// Fails immediately when the runtime is not suspended. Returns like
// `ls_call`: OK with the result on the stack, SUSPENDED, or FAILURE.
ls_result ls_runtime_resume_suspended(ls_runtime* runtime);

#ifdef __cplusplus
}
#endif
