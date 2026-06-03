#pragma once

#include <string.h>

#include "capi.h"

typedef struct ls_string {
	ls_host host;
	u32 ref_count;
	u32 length;
	char chars[1];
} ls_string;

enum class BytecodeOp : u8 {
	LOAD_CONST8,
	LOAD_CONST16,
	LOAD_CONST32,
	LOAD_CONST64,
	LOAD_STRING,
	LOAD_PARAM,
	LOAD_GLOBAL,
	LOAD_LOCAL,
	LOAD_INDIRECT,
	STORE_GLOBAL,
	STORE_LOCAL,
	STORE_INDIRECT,
	LOAD_FUNCTION,
	LOAD_NATIVE_FUNCTION,
	NOT_BOOL,
	JUMP,
	JUMP_IF_FALSE,
	POP,
	CALL,
	CALL_NATIVE,
	CALL_INDIRECT,
	ADD_I8,
	ADD_U8,
	ADD_I16,
	ADD_U16,
	ADD_I32,
	ADD_U32,
	ADD_I64,
	ADD_U64,
	ADD_F32,
	ADD_F64,
	ADD_STRING,
	SUB_I8,
	SUB_U8,
	SUB_I16,
	SUB_U16,
	SUB_I32,
	SUB_U32,
	SUB_I64,
	SUB_U64,
	SUB_F32,
	SUB_F64,
	MUL_I8,
	MUL_U8,
	MUL_I16,
	MUL_U16,
	MUL_I32,
	MUL_U32,
	MUL_I64,
	MUL_U64,
	MUL_F32,
	MUL_F64,
	DIV_I8,
	DIV_U8,
	DIV_I16,
	DIV_U16,
	DIV_I32,
	DIV_U32,
	DIV_I64,
	DIV_U64,
	DIV_F32,
	DIV_F64,
	MOD_I8,
	MOD_U8,
	MOD_I16,
	MOD_U16,
	MOD_I32,
	MOD_U32,
	MOD_I64,
	MOD_U64,
	CAST,
	CMP_EQ,
	CMP_NE,
	CMP_GT,
	CMP_GE,
	CMP_LT,
	CMP_LE,
	RETURN
};

#define LS_DECLARE_BYTECODE_ARRAY(name, type) \
typedef struct name { \
	type* data; \
	i32 size; \
	i32 capacity; \
	ls_arena* arena; \
} name; \
static inline bool name##_init(name* array, ls_arena* arena) { \
	if (!array || !arena || !arena->allocate) return false; \
	array->data = NULL; \
	array->size = 0; \
	array->capacity = 0; \
	array->arena = arena; \
	return true; \
} \
static inline void name##_clear(name* array) { \
	if (!array) return; \
	array->size = 0; \
} \
static inline bool name##_reserve(name* array, i32 capacity) { \
	if (!array || !array->arena || !array->arena->allocate) return false; \
	if (capacity <= array->capacity) return true; \
	i32 new_capacity = array->capacity > 0 ? array->capacity * 2 : 8; \
	if (new_capacity < capacity) new_capacity = capacity; \
	void* data = array->arena->allocate(array->arena->user_data, (size_t)new_capacity * sizeof(type), alignof(type)); \
	if (!data) return false; \
	if (array->data && array->size > 0) memcpy(data, array->data, (size_t)array->size * sizeof(type)); \
	array->data = (type*)data; \
	array->capacity = new_capacity; \
	return true; \
} \
static inline bool name##_resize(name* array, i32 size) { \
	if (!array) return false; \
	if (size > array->capacity && !name##_reserve(array, size)) return false; \
	array->size = size; \
	return true; \
} \
static inline bool name##_push_back(name* array, type value) { \
	if (!array) return false; \
	if (array->size >= array->capacity && !name##_reserve(array, array->size + 1)) return false; \
	array->data[array->size++] = value; \
	return true; \
}

LS_DECLARE_BYTECODE_ARRAY(ls_u8_array, u8)
LS_DECLARE_BYTECODE_ARRAY(ls_type_array, ls_type)

typedef struct BytecodeFunction {
	ls_string_view name;
	ls_type_array params;
	ls_type return_type;
	i32 code_offset;
	i32 code_size;
	// Number of stack slots reserved by the declared parameters. This can differ
	// from `params.size` when a single source parameter spans multiple flattened
	// VM slots.
	i32 param_slot_count;
	i32 local_count;
	i32 return_count;
} BytecodeFunction;

typedef struct BytecodeNativeFunction {
	ls_string_view canonical_name;
	ls_type_array params;
	ls_type return_type;
	i32 return_count;
} BytecodeNativeFunction;

LS_DECLARE_BYTECODE_ARRAY(ls_bytecode_function_array, BytecodeFunction)
LS_DECLARE_BYTECODE_ARRAY(ls_bytecode_native_function_array, BytecodeNativeFunction)
LS_DECLARE_BYTECODE_ARRAY(ls_string_ptr_array, ls_string*)

typedef struct ls_bytecode {
	ls_host host;
	ls_arena* arena;
	ls_bytecode_function_array functions;
	ls_bytecode_native_function_array native_functions;
	i32 global_count;
	ls_u8_array global_init_code;
	ls_u8_array code;
	ls_string_ptr_array string_literals;
} ls_bytecode;
