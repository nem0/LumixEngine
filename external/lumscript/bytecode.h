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
// - native callbacks still see arguments/results through the public runtime
//   stack helpers
//
// Addressing contract:
// - `LOAD_AT` and `STORE_AT` copy an explicit byte width
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
// - `SLICE_LOAD` reads `base, length, index` registers
// - `SLICE_STORE` reads `base, length, index, value` registers
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

	LS_OP_LOAD_AT,
	LS_OP_STORE_AT,
	LS_OP_REF_AT,
	LS_OP_BOUNDS_CHECK,
	LS_OP_SLICE,
	LS_OP_SLICE_LOAD,
	LS_OP_SLICE_STORE,
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

	LS_OP_JUMP,
	LS_OP_JUMP_IF_FALSE,
	LS_OP_JUMP_IF_TRUE,

	LS_OP_CALL_DIRECT,
	LS_OP_CALL_INDIRECT,
	LS_OP_CAST,
	LS_OP_RETURN,
} ls_op;

typedef enum ls_function_kind {
	LS_FUNCTION_SCRIPT = 0,
	LS_FUNCTION_NATIVE,
} ls_function_kind;

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
} ls_function_bc;

typedef struct ls_bytecode {
	const ls_host* host;
	ls_arena* arena;

	ls_function_bc* functions;
	u32 function_count;
	u32 function_capacity;

	u32 global_size;
	bool has_global_init;

	ls_string_view* strings;
	u32 string_count;
	u32 string_capacity;
} ls_bytecode;

typedef struct ls_runtime {
	const ls_host* host;
	const ls_bytecode* bytecode;
	ls_arena* arena;

	// Stack top: byte offset one past the topmost live value.
	u32 stack_top;
	// Base byte offset for the current call frame.
	u32 frame_base;

	u8* stack;
	u32 stack_capacity;

	// Index of the function whose return value sits on top of the stack, or -1.
	// `ls_call_result` uses it to locate the raw result bytes. Host pushes
	// invalidate it.
	i32 result_function;

	// Indexed by module/native-function index.
	ls_native_fn* native_callbacks;
	u32 native_callback_count;
} ls_runtime;

#ifdef __cplusplus
}
#endif
