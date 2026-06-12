#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "capi.h"

// Bytecode is intentionally simple: a stack VM with linear instruction streams
// and a flat function table. Constants are embedded directly in the stream.
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
// Call stack contract:
// - ordinary calls evaluate arguments onto the stack in source order
// - `CALL_DIRECT` uses a function-table index and consumes the arguments on the
//   stack
// - `CALL_INDIRECT` consumes a callee function value that sits below its
//   arguments on the stack
// - both call forms leave the return value(s) on the stack in place of the
//   call frame's argument window
// - the callee determines argument/result slot widths via its function metadata
//
// Addressing contract:
// - `LOAD_AT` and `STORE_AT` operate on one slot only
// - they consume a base reference and an index from the stack
// - stack order is `... base index`; `LOAD_AT` leaves `... value`
// - stack order for stores is `... base index value`; `STORE_AT` leaves `...`
// - they compute `base + index * scale + offset`, then load/store one slot
// - `scale` and `offset` are encoded as immediates
// - this is intended to cover nested field access and array indexing through
//   one generic form
// - aggregate values spanning multiple slots are handled by repeated slot access
//
// Slice contract:
// - a slice occupies two stack slots in `base, length` order
// - `base` is an absolute runtime stack slot, so slices can alias caller locals
//   while passed to and returned from nested calls
// - `SLICE` consumes `base, length, begin, end` and produces a subslice
// - `SLICE_LOAD` consumes `base, length, index`
// - `SLICE_STORE` consumes `base, length, index, value`
// - all slice bounds are checked by the runtime
//
// Opcode layout summary:
// - no operand: NOP, NOT, AND, OR, EQ, NE, LT, LE, GT, GE, CALL_INDIRECT,
//   RETURN, POP, ABORT
// - u32 operand: LOAD_LOCAL, STORE_LOCAL, LOAD_GLOBAL, STORE_GLOBAL,
//   LOCAL_REF, GLOBAL_REF, CALL_DIRECT
// - two u8 type operands: CAST (`from`, `to`)
// - embedded constant payload: LOAD_CONST_1, LOAD_CONST_2, LOAD_CONST_4,
//   LOAD_CONST_8
// - string constant payload: LOAD_CONST_STRING
// - scaled address operand: LOAD_AT, STORE_AT, REF_AT (`u32 scale`, `i32 offset`)
// - slice operand: SLICE, SLICE_LOAD, SLICE_STORE (`u32 element slot count`)
// - i32 operand: JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE
//
// `ABORT` is a hard stop: the VM terminates execution immediately.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ls_op {
	LS_OP_NOP = 0,

	LS_OP_LOAD_CONST_1,
	LS_OP_LOAD_CONST_2,
	LS_OP_LOAD_CONST_4,
	LS_OP_LOAD_CONST_8,
	LS_OP_LOAD_CONST_STRING,
	LS_OP_LOAD_LOCAL,
	LS_OP_STORE_LOCAL,
	LS_OP_LOAD_GLOBAL,
	LS_OP_STORE_GLOBAL,
	LS_OP_LOCAL_REF,
	LS_OP_GLOBAL_REF,

	LS_OP_LOAD_AT,
	LS_OP_STORE_AT,
	LS_OP_REF_AT,
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
	LS_OP_AND,
	LS_OP_OR,

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
	LS_OP_POP,
	LS_OP_ABORT,
} ls_op;

typedef union ls_value {
	i8 i8val;
	u8 u8val;
	i16 i16val;
	u16 u16val;
	i32 i32val;
	u32 u32val;
	i64 i64val;
	u64 u64val;
	float f32val;
	double f64val;
	void* cptr;
} ls_value;

typedef enum ls_function_kind {
	LS_FUNCTION_SCRIPT = 0,
	LS_FUNCTION_NATIVE,
} ls_function_kind;

typedef struct ls_function_bc {
	ls_string_view name;
	ls_function_kind kind;
	bool is_builtin_native;

	// Function values are addressed by table index.
	u32 index;

	// Parameter/result counts are in value slots, not source-level arity.
	u32 param_count;
	u32 param_slot_count;
	u32 return_slot_count;
	u32 local_slot_count;
	u32 max_stack;

	ls_type_kind return_kind;
	u8* code;
	u32 code_size;
	u32 code_capacity;
} ls_function_bc;

typedef struct ls_bytecode {
	const ls_host* host;
	ls_arena* arena;

	ls_function_bc* functions;
	u32 function_count;
	u32 function_capacity;

	u32 global_slot_count;
	ls_function_bc global_init;
	bool has_global_init;

	ls_string_view* strings;
	u32 string_count;
	u32 string_capacity;
} ls_bytecode;

typedef struct ls_call_frame {
	// Function being returned to.
	u32 function_index;
	// Saved byte offset to resume at after return.
	u32 return_offset;
	// Saved frame base for the caller.
	u32 saved_frame_base;
	// Stack base for the callee's argument/result window.
	u32 stack_base;
	// Number of result slots the caller expects.
	u32 result_slot_count;
} ls_call_frame;

typedef struct ls_runtime {
	const ls_host* host;
	const ls_bytecode* bytecode;
	ls_arena* arena;

	// Stack top: index one past the topmost live value.
	u32 stack_top;
	// Base index for the current call frame.
	u32 frame_base;
	// Byte offset into the current function body.
	u32 instruction_offset;

	ls_value* stack;
	u32 stack_capacity;

	ls_call_frame* frames;
	u32 frame_count;
	u32 frame_capacity;

	// Indexed by module/native-function index.
	ls_native_fn* native_callbacks;
	u32 native_callback_count;
	u32 native_callback_capacity;
} ls_runtime;

#ifdef __cplusplus
}
#endif
