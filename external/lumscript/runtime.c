#include "bytecode.h"

 // TODO lot of silent failures here, should be handled better

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct ls_string_box {
	ls_string_view value;
} ls_string_box;

ls_string_view ls_arg_read_string(ls_call_frame* frame) {
	ls_string_box* box = NULL;
	memcpy(&box, frame->args, sizeof(box));
	frame->args += sizeof(box);
	return box ? box->value : (ls_string_view){NULL, NULL};
}

static int string_equals(ls_string_view a, ls_string_view b) {
	const ptrdiff_t a_size = (ptrdiff_t)(a.end - a.begin);
	const ptrdiff_t b_size = (ptrdiff_t)(b.end - b.begin);
	if (a_size != b_size) return 0;
	for (ptrdiff_t i = 0; i < a_size; ++i) {
		if (a.begin[i] != b.begin[i]) return 0;
	}
	return 1;
}

#define LS_STACK_CAPACITY_BYTES (65536u * 8u)
static u32 runtime_type_size(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_VOID: return 0u;
		case LS_TYPE_BOOL:
		case LS_TYPE_I8:
		case LS_TYPE_U8:
			return 1u;
		case LS_TYPE_I16:
		case LS_TYPE_U16:
			return 2u;
		case LS_TYPE_I32:
		case LS_TYPE_U32:
		case LS_TYPE_F32:
		case LS_TYPE_ENUM:
		case LS_TYPE_FUNCTION:
			return 4u;
		case LS_TYPE_I64:
		case LS_TYPE_U64:
		case LS_TYPE_F64:
		case LS_TYPE_STRING:
		case LS_TYPE_CPTR:
			return 8u;
		case LS_TYPE_SLICE:
			return 16u;
		default:
			return 8u;
	}
}

static const ls_function_bc* runtime_find_function(const ls_bytecode* bytecode, i32 function_index) {
	if (function_index < 0) return NULL;
	if ((u32)function_index >= bytecode->function_count) return NULL;
	return &bytecode->functions[(u32)function_index];
}

static const ls_function_bc* runtime_find_function_by_name(const ls_bytecode* bytecode, ls_string_view name, i32* out_index) {
	if (!bytecode) return NULL;
	for (u32 i = 0; i < bytecode->function_count; ++i) {
		if (string_equals(bytecode->functions[i].name, name)) {
			if (out_index) *out_index = (i32)i;
			return &bytecode->functions[i];
		}
	}
	return NULL;
}

static ls_string_box* runtime_make_string_box(ls_runtime* runtime, ls_string_view value) {
	ls_string_box* box = NULL;
	if (runtime && runtime->arena && runtime->arena->allocate) {
		box = (ls_string_box*)runtime->arena->allocate(runtime->arena->user_data, sizeof(ls_string_box), sizeof(void*));
	}
	else {
		box = (ls_string_box*)malloc(sizeof(ls_string_box));
	}
	if (!box) return NULL;
	box->value = value;
	return box;
}

static ls_string_box* runtime_copy_string_box(ls_runtime* runtime, ls_string_view value) {
	static const char empty[] = "";
	const ptrdiff_t size = value.begin && value.end && value.end >= value.begin ? value.end - value.begin : 0;
	char* copy = NULL;
	if (runtime && runtime->arena && runtime->arena->allocate) {
		copy = (char*)runtime->arena->allocate(runtime->arena->user_data, (size_t)size + 1u, 1u);
	}
	else {
		copy = (char*)malloc((size_t)size + 1u);
	}
	if (!copy) return NULL;
	if (size > 0) memcpy(copy, value.begin, (size_t)size);
	copy[size] = '\0';
	return runtime_make_string_box(runtime, (ls_string_view){size > 0 ? copy : empty, (size > 0 ? copy : empty) + size});
}

void ls_result_string(ls_runtime* runtime, ls_call_frame* frame, ls_string_view value) {
	void* box = runtime_copy_string_box(runtime, value);
	if (!box) return;
	memcpy(frame->result, &box, sizeof(box));
	frame->result += sizeof(box);
}

static void runtime_push_bytes(ls_runtime* runtime, const void* value, u32 size) {
	if (runtime->stack_top + size > runtime->stack_capacity) return;
	memcpy(runtime->stack + runtime->stack_top, value, size);
	runtime->stack_top += size;
	runtime->result_function = -1;
}

static int runtime_string_equals_cstr(ls_string_view value, const char* cstr) {
	const char* a = value.begin;
	const char* b = cstr;
	while (a < value.end && *b) {
		if (*a++ != *b++) return 0;
	}
	return a == value.end && *b == '\0';
}

static void runtime_native_sin_f32(ls_runtime* runtime, ls_call_frame frame) { (void)runtime; LS_ARG(frame, f32, value); LS_TYPED_RESULT(frame, f32, sinf(value)); }
static void runtime_native_cos_f32(ls_runtime* runtime, ls_call_frame frame) { (void)runtime; LS_ARG(frame, f32, value); LS_TYPED_RESULT(frame, f32, cosf(value)); }
static void runtime_native_sqrt_f32(ls_runtime* runtime, ls_call_frame frame) { (void)runtime; LS_ARG(frame, f32, value); LS_TYPED_RESULT(frame, f32, sqrtf(value)); }
static void runtime_native_sin_f64(ls_runtime* runtime, ls_call_frame frame) { (void)runtime; LS_ARG(frame, f64, value); LS_TYPED_RESULT(frame, f64, sin(value)); }
static void runtime_native_cos_f64(ls_runtime* runtime, ls_call_frame frame) { (void)runtime; LS_ARG(frame, f64, value); LS_TYPED_RESULT(frame, f64, cos(value)); }
static void runtime_native_sqrt_f64(ls_runtime* runtime, ls_call_frame frame) { (void)runtime; LS_ARG(frame, f64, value); LS_TYPED_RESULT(frame, f64, sqrt(value)); }

/* std:mem alloc(size, align) : byte[]. Allocates `size` bytes of heap memory. */
static void runtime_native_alloc(ls_runtime* runtime, ls_call_frame frame) {
	(void)runtime;
	LS_ARG(frame, i64, size);
	LS_ARG(frame, i64, align);
	(void)align;
	void* ptr = size > 0 ? malloc((size_t)size) : NULL;
	i64 actual = ptr ? size : 0;
	memcpy(frame.result, &ptr, sizeof(ptr));
	memcpy(frame.result + sizeof(ptr), &actual, sizeof(actual));
}

/* std:mem free(memory : byte[]) : void. */
static void runtime_native_free(ls_runtime* runtime, ls_call_frame frame) {
	(void)runtime;
	LS_ARG(frame, void*, ptr);
	LS_ARG(frame, i64, size);
	(void)size;
	free(ptr);
}

static void runtime_bind_builtin_callbacks(ls_runtime* runtime) {
	// TODO shouldn't we check import path?
	for (u32 i = 0; i < runtime->bytecode->function_count; ++i) {
		const ls_function_bc* fn = &runtime->bytecode->functions[i];
		if (fn->kind != LS_FUNCTION_NATIVE || !fn->is_builtin_native) continue;
		if (runtime_string_equals_cstr(fn->name, "sin")) runtime->native_callbacks[i] = &runtime_native_sin_f32;
		else if (runtime_string_equals_cstr(fn->name, "cos")) runtime->native_callbacks[i] = &runtime_native_cos_f32;
		else if (runtime_string_equals_cstr(fn->name, "sqrt")) runtime->native_callbacks[i] = &runtime_native_sqrt_f32;
		else if (runtime_string_equals_cstr(fn->name, "sin_f64")) runtime->native_callbacks[i] = &runtime_native_sin_f64;
		else if (runtime_string_equals_cstr(fn->name, "cos_f64")) runtime->native_callbacks[i] = &runtime_native_cos_f64;
		else if (runtime_string_equals_cstr(fn->name, "sqrt_f64")) runtime->native_callbacks[i] = &runtime_native_sqrt_f64;
		else if (runtime_string_equals_cstr(fn->name, "alloc")) runtime->native_callbacks[i] = &runtime_native_alloc;
		else if (runtime_string_equals_cstr(fn->name, "free")) runtime->native_callbacks[i] = &runtime_native_free;
	}
}

static u64 runtime_read_u64(const u8* code, u32* pc) {
	u64 value = 0;
	memcpy(&value, code + *pc, sizeof(value));
	*pc += (u32)sizeof(value);
	return value;
}

static u32 runtime_read_u32(const u8* code, u32* pc) {
	u32 value = 0;
	memcpy(&value, code + *pc, sizeof(value));
	*pc += (u32)sizeof(value);
	return value;
}

static i32 runtime_read_i32(const u8* code, u32* pc) {
	i32 value = 0;
	memcpy(&value, code + *pc, sizeof(value));
	*pc += (u32)sizeof(value);
	return value;
}

static double runtime_numeric_to_double(const u8* value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL: return (value && value[0] != 0u) ? 1.0 : 0.0;
		case LS_TYPE_I8:   { i8 v = 0; memcpy(&v, value, 1); return (double)v; }
		case LS_TYPE_U8:   { u8 v = 0; memcpy(&v, value, 1); return (double)v; }
		case LS_TYPE_I16:  { i16 v = 0; memcpy(&v, value, 2); return (double)v; }
		case LS_TYPE_U16:  { u16 v = 0; memcpy(&v, value, 2); return (double)v; }
		case LS_TYPE_I32:
		case LS_TYPE_ENUM: { i32 v = 0; memcpy(&v, value, 4); return (double)v; }
		case LS_TYPE_U32:  { u32 v = 0; memcpy(&v, value, 4); return (double)v; }
		case LS_TYPE_I64:  { i64 v = 0; memcpy(&v, value, 8); return (double)v; }
		case LS_TYPE_U64:  { u64 v = 0; memcpy(&v, value, 8); return (double)v; }
		case LS_TYPE_F32:  { f32 v = 0; memcpy(&v, value, 4); return (double)v; }
		case LS_TYPE_F64:  { f64 v = 0; memcpy(&v, value, 8); return v; }
		default:           return 0.0;
	}
}

static i64 runtime_numeric_to_i64(const u8* value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL: return (value && value[0] != 0u) ? 1 : 0;
		case LS_TYPE_I8:   { i8 v = 0; memcpy(&v, value, 1); return (i64)v; }
		case LS_TYPE_U8:   { u8 v = 0; memcpy(&v, value, 1); return (i64)v; }
		case LS_TYPE_I16:  { i16 v = 0; memcpy(&v, value, 2); return (i64)v; }
		case LS_TYPE_U16:  { u16 v = 0; memcpy(&v, value, 2); return (i64)v; }
		case LS_TYPE_I32:
		case LS_TYPE_ENUM: { i32 v = 0; memcpy(&v, value, 4); return (i64)v; }
		case LS_TYPE_U32:  { u32 v = 0; memcpy(&v, value, 4); return (i64)v; }
		case LS_TYPE_I64:  { i64 v = 0; memcpy(&v, value, 8); return v; }
		case LS_TYPE_U64:  { u64 v = 0; memcpy(&v, value, 8); return (i64)v; }
		case LS_TYPE_F32:  { f32 v = 0; memcpy(&v, value, 4); return (i64)v; }
		case LS_TYPE_F64:  { f64 v = 0; memcpy(&v, value, 8); return (i64)v; }
		default:           return 0;
	}
}

static u64 runtime_numeric_to_u64(const u8* value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL: return (value && value[0] != 0u) ? 1u : 0u;
		case LS_TYPE_I8:   { i8 v = 0; memcpy(&v, value, 1); return (u64)v; }
		case LS_TYPE_U8:   { u8 v = 0; memcpy(&v, value, 1); return (u64)v; }
		case LS_TYPE_I16:  { i16 v = 0; memcpy(&v, value, 2); return (u64)v; }
		case LS_TYPE_U16:  { u16 v = 0; memcpy(&v, value, 2); return (u64)v; }
		case LS_TYPE_I32:
		case LS_TYPE_ENUM: { i32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case LS_TYPE_U32:  { u32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case LS_TYPE_I64:  { i64 v = 0; memcpy(&v, value, 8); return (u64)v; }
		case LS_TYPE_U64:  { u64 v = 0; memcpy(&v, value, 8); return v; }
		case LS_TYPE_F32:  { f32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case LS_TYPE_F64:  { f64 v = 0; memcpy(&v, value, 8); return (u64)v; }
		default:           return 0u;
	}
}

static u8* runtime_frame_ptr(ls_runtime* runtime, u32 offset) {
	return runtime->stack + runtime->frame_base + offset;
}

#define LS_REG_BINOP(TYPE, EXPR) \
	do { \
		const u32 dst__ = runtime_read_u32(fn->code, &pc); \
		const u32 lhs_offset__ = runtime_read_u32(fn->code, &pc); \
		const u32 rhs_offset__ = runtime_read_u32(fn->code, &pc); \
		TYPE a = 0; TYPE b = 0; \
		u8* lhs__ = runtime_frame_ptr(runtime, lhs_offset__); \
		u8* rhs__ = runtime_frame_ptr(runtime, rhs_offset__); \
		u8* out__ = runtime_frame_ptr(runtime, dst__); \
		memcpy(&a, lhs__, sizeof(TYPE)); \
		memcpy(&b, rhs__, sizeof(TYPE)); \
		TYPE result__ = (TYPE)(EXPR); \
		memcpy(out__, &result__, sizeof(TYPE)); \
	} while (0)

#define LS_REG_DIVOP(TYPE, EXPR) \
	do { \
		const u32 dst__ = runtime_read_u32(fn->code, &pc); \
		const u32 lhs_offset__ = runtime_read_u32(fn->code, &pc); \
		const u32 rhs_offset__ = runtime_read_u32(fn->code, &pc); \
		TYPE a = 0; TYPE b = 0; \
		u8* lhs__ = runtime_frame_ptr(runtime, lhs_offset__); \
		u8* rhs__ = runtime_frame_ptr(runtime, rhs_offset__); \
		u8* out__ = runtime_frame_ptr(runtime, dst__); \
		memcpy(&a, lhs__, sizeof(TYPE)); \
		memcpy(&b, rhs__, sizeof(TYPE)); \
		if (!b) goto runtime_execute_function_fail; \
		TYPE result__ = (TYPE)(EXPR); \
		memcpy(out__, &result__, sizeof(TYPE)); \
	} while (0)

#define LS_REG_NEGOP(TYPE) \
	do { \
		const u32 offset__ = runtime_read_u32(fn->code, &pc); \
		TYPE value__ = 0; \
		u8* ptr__ = runtime_frame_ptr(runtime, offset__); \
		memcpy(&value__, ptr__, sizeof(TYPE)); \
		value__ = (TYPE)(0 - value__); \
		memcpy(ptr__, &value__, sizeof(TYPE)); \
	} while (0)

#define LS_REG_NOT() \
	do { \
		const u32 offset__ = runtime_read_u32(fn->code, &pc); \
		u8* ptr__ = runtime_frame_ptr(runtime, offset__); \
		*ptr__ = *ptr__ ? 0u : 1u; \
	} while (0)

#define LS_REG_CMP_NUMERIC(OP) \
	do { \
		const u32 dst__ = runtime_read_u32(fn->code, &pc); \
		const u32 lhs_offset__ = runtime_read_u32(fn->code, &pc); \
		const u32 rhs_offset__ = runtime_read_u32(fn->code, &pc); \
		const ls_type_kind kind__ = (ls_type_kind)fn->code[pc++]; \
		u8* lhs_ptr__ = runtime_frame_ptr(runtime, lhs_offset__); \
		u8* rhs_ptr__ = runtime_frame_ptr(runtime, rhs_offset__); \
		u8* out__ = runtime_frame_ptr(runtime, dst__); \
		int result__ = 0; \
		if (kind__ == LS_TYPE_STRING) { \
			void* lhs_string__ = NULL; void* rhs_string__ = NULL; \
			memcpy(&lhs_string__, lhs_ptr__, sizeof(lhs_string__)); \
			memcpy(&rhs_string__, rhs_ptr__, sizeof(rhs_string__)); \
			if (lhs_string__ == rhs_string__) result__ = 1; \
			else if (lhs_string__ && rhs_string__) result__ = string_equals(((ls_string_box*)lhs_string__)->value, ((ls_string_box*)rhs_string__)->value); \
			result__ = result__ OP 1; \
		} else if (kind__ == LS_TYPE_F32 || kind__ == LS_TYPE_F64) { \
			result__ = runtime_numeric_to_double(lhs_ptr__, kind__) OP runtime_numeric_to_double(rhs_ptr__, kind__); \
		} else { \
			result__ = runtime_numeric_to_i64(lhs_ptr__, kind__) OP runtime_numeric_to_i64(rhs_ptr__, kind__); \
		} \
		*out__ = result__ ? 1u : 0u; \
	} while (0)

static int runtime_execute_function(ls_runtime* runtime, i32 function_index) {
	const ls_function_bc* fn = runtime_find_function(runtime->bytecode, function_index);
	if (!fn) return 0;
	const u32 initial_frame_base = runtime->frame_base;
	const u32 initial_stack_top = runtime->stack_top;
	const i32 initial_result_function = runtime->result_function;
	const u32 initial_call_depth = runtime->call_depth;

	const u32 arg_base = runtime->stack_top >= fn->param_size ? runtime->stack_top - fn->param_size : 0u;
	if (fn->kind == LS_FUNCTION_NATIVE) {
		if ((u32)function_index >= runtime->native_callback_count) return 0;
		ls_native_fn callback = runtime->native_callbacks[(u32)function_index];
		if (!callback) return 0;

		const u32 expected_top = arg_base + fn->return_size;
		if (expected_top > runtime->stack_capacity) return 0;

		runtime->result_function = -1;
		ls_call_frame frame;
		frame.args = runtime->stack + arg_base;
		frame.result = runtime->stack + arg_base;
		callback(runtime, frame);

		runtime->stack_top = expected_top;
		runtime->result_function = function_index;
		runtime->frame_base = initial_frame_base;
		runtime->call_depth = initial_call_depth;
		return 1;
	}

	const u32 saved_frame_base = runtime->frame_base;
	const u32 frame_base = arg_base;
	const u32 frame_stack_top = frame_base + fn->frame_size;

	if (frame_stack_top > runtime->stack_capacity) goto runtime_execute_function_fail;

	runtime->frame_base = frame_base;
	runtime->stack_top = frame_stack_top;

	i32 current_function_index = function_index;
	u32 pc = 0;
	for (;;) {
		const ls_op op = (ls_op)fn->code[pc++];
		switch (op) {
			// TODO const table?
			case LS_OP_LOAD_CONST_1: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				u8 value = fn->code[pc++];
				u8* out = runtime_frame_ptr(runtime, dst);
				*out = value;
				break;
			}
			case LS_OP_LOAD_CONST_2: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				u8* out = runtime_frame_ptr(runtime, dst);
				memcpy(out, fn->code + pc, 2u);
				pc += 2u;
				break;
			}
			case LS_OP_LOAD_CONST_4: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				u8* out = runtime_frame_ptr(runtime, dst);
				memcpy(out, fn->code + pc, 4u);
				pc += 4u;
				break;
			}
			case LS_OP_LOAD_CONST_8: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				u8* out = runtime_frame_ptr(runtime, dst);
				memcpy(out, fn->code + pc, 8u);
				pc += 8u;
				break;
			}
			case LS_OP_LOAD_CONST_STRING: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 index = runtime_read_u32(fn->code, &pc);
				void* value = runtime_copy_string_box(runtime, runtime->bytecode->strings[index]);
				u8* out = runtime_frame_ptr(runtime, dst);
				memcpy(out, &value, sizeof(value));
				break;
			}
			case LS_OP_COPY: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 src_offset = runtime_read_u32(fn->code, &pc);
				const u32 size = runtime_read_u32(fn->code, &pc);
				u8* out = runtime_frame_ptr(runtime, dst);
				u8* src = runtime_frame_ptr(runtime, src_offset);
				memmove(out, src, size);
				break;
			}
			case LS_OP_GLOBAL_LOAD: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 offset = runtime_read_u32(fn->code, &pc);
				const u32 size = runtime_read_u32(fn->code, &pc);
				u8* out = runtime_frame_ptr(runtime, dst);
				u8* src = runtime->stack + offset;
				memmove(out, src, size);
				break;
			}
			case LS_OP_GLOBAL_STORE: {
				const u32 offset = runtime_read_u32(fn->code, &pc);
				const u32 src_offset = runtime_read_u32(fn->code, &pc);
				const u32 size = runtime_read_u32(fn->code, &pc);
				u8* dst = runtime->stack + offset;
				u8* src = runtime_frame_ptr(runtime, src_offset);
				memmove(dst, src, size);
				break;
			}
			case LS_OP_LOCAL_REF: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 offset = runtime_read_u32(fn->code, &pc);
				const u32 absolute = runtime->frame_base + offset;
				u8* out = runtime_frame_ptr(runtime, dst);
				void* ptr = runtime->stack + absolute;
				memcpy(out, &ptr, sizeof(ptr));
				break;
			}
			case LS_OP_GLOBAL_REF: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 offset = runtime_read_u32(fn->code, &pc);
				u8* out = runtime_frame_ptr(runtime, dst);
				void* ptr = runtime->stack + offset;
				memcpy(out, &ptr, sizeof(ptr));
				break;
			}
			case LS_OP_LOAD_AT: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 base_reg = runtime_read_u32(fn->code, &pc);
				const u32 index_reg = runtime_read_u32(fn->code, &pc);
				const u32 scale = runtime_read_u32(fn->code, &pc);
				const i32 offset = runtime_read_i32(fn->code, &pc);
				const u32 size = runtime_read_u32(fn->code, &pc);
				i64 index = 0;
				void* base_ptr = NULL;
				u8* index_ptr = runtime_frame_ptr(runtime, index_reg);
				u8* base_value = runtime_frame_ptr(runtime, base_reg);
				u8* out = runtime_frame_ptr(runtime, dst);
				memcpy(&index, index_ptr, 8u);
				memcpy(&base_ptr, base_value, sizeof(base_ptr));
				u8* base = (u8*)base_ptr;
				u8* addr = base + index * (i64)scale + (i64)offset;
				memmove(out, addr, size);
				break;
			}
			case LS_OP_STORE_AT: {
				const u32 base_reg = runtime_read_u32(fn->code, &pc);
				const u32 index_reg = runtime_read_u32(fn->code, &pc);
				const u32 value_reg = runtime_read_u32(fn->code, &pc);
				const u32 scale = runtime_read_u32(fn->code, &pc);
				const i32 offset = runtime_read_i32(fn->code, &pc);
				const u32 size = runtime_read_u32(fn->code, &pc);
				i64 index = 0;
				void* base_ptr = NULL;
				u8* index_ptr = runtime_frame_ptr(runtime, index_reg);
				u8* base_value = runtime_frame_ptr(runtime, base_reg);
				u8* value = runtime_frame_ptr(runtime, value_reg);
				memcpy(&index, index_ptr, 8u);
				memcpy(&base_ptr, base_value, sizeof(base_ptr));
				u8* base = (u8*)base_ptr;
				u8* addr = base + index * (i64)scale + (i64)offset;
				memmove(addr, value, size);
				break;
			}
			case LS_OP_REF_AT: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 base_reg = runtime_read_u32(fn->code, &pc);
				const u32 index_reg = runtime_read_u32(fn->code, &pc);
				const u32 scale = runtime_read_u32(fn->code, &pc);
				const i32 offset = runtime_read_i32(fn->code, &pc);
				i64 index = 0;
				void* base_ptr = NULL;
				u8* index_ptr = runtime_frame_ptr(runtime, index_reg);
				u8* base_value = runtime_frame_ptr(runtime, base_reg);
				u8* out = runtime_frame_ptr(runtime, dst);
				memcpy(&index, index_ptr, 8u);
				memcpy(&base_ptr, base_value, sizeof(base_ptr));
				u8* base = (u8*)base_ptr;
				u8* addr = base + index * (i64)scale + (i64)offset;
				memcpy(out, &addr, sizeof(addr));
				break;
			}
			case LS_OP_BOUNDS_CHECK: {
				const u32 index_reg = runtime_read_u32(fn->code, &pc);
				const u64 length = runtime_read_u64(fn->code, &pc);
				i64 index = 0;
				u8* index_ptr = runtime_frame_ptr(runtime, index_reg);
				memcpy(&index, index_ptr, 8u);
				if (index < 0 || (u64)index >= length) goto runtime_execute_function_fail;
				break;
			}
			case LS_OP_SLICE: {
				/* The resulting slice overwrites the source slice value in place. */
				const u32 slice_reg = runtime_read_u32(fn->code, &pc);
				const u32 begin_reg = runtime_read_u32(fn->code, &pc);
				const u32 end_reg = runtime_read_u32(fn->code, &pc);
				const u32 element_size = runtime_read_u32(fn->code, &pc);
				i64 end = 0, begin = 0, length = 0;
				void* base_ptr = NULL;
				u8* slice = runtime_frame_ptr(runtime, slice_reg);
				u8* begin_ptr = runtime_frame_ptr(runtime, begin_reg);
				u8* end_ptr = runtime_frame_ptr(runtime, end_reg);
				u8* out = slice;
				memcpy(&base_ptr, slice, sizeof(base_ptr));
				memcpy(&length, slice + sizeof(void*), 8u);
				memcpy(&begin, begin_ptr, 8u);
				memcpy(&end, end_ptr, 8u);
				u8* base = (u8*)base_ptr;
				if (begin < 0 || end < begin || (u64)end > (u64)length) goto runtime_execute_function_fail;
				const u64 offset = (u64)begin * element_size;
				if (element_size != 0u && offset / element_size != (u64)begin) goto runtime_execute_function_fail;
				void* new_base = base ? base + offset : NULL;
				const i64 new_length = end - begin;
				memcpy(out, &new_base, sizeof(new_base));
				memcpy(out + sizeof(void*), &new_length, 8u);
				break;
			}
			case LS_OP_SLICE_LOAD: {
				/* The loaded element overwrites the slice value in place. */
				const u32 slice_reg = runtime_read_u32(fn->code, &pc);
				const u32 index_reg = runtime_read_u32(fn->code, &pc);
				const u32 element_size = runtime_read_u32(fn->code, &pc);
				i64 index = 0;
				i64 length = 0;
				void* base_ptr = NULL;
				u8* slice = runtime_frame_ptr(runtime, slice_reg);
				u8* index_ptr = runtime_frame_ptr(runtime, index_reg);
				u8* out = slice;
				memcpy(&base_ptr, slice, sizeof(base_ptr));
				memcpy(&length, slice + sizeof(void*), 8u);
				memcpy(&index, index_ptr, 8u);
				u8* base = (u8*)base_ptr;
				if (!base || index < 0 || (u64)index >= (u64)length) goto runtime_execute_function_fail;
				const u64 offset = (u64)index * element_size;
				if (element_size != 0u && offset / element_size != (u64)index) goto runtime_execute_function_fail;
				if (element_size > 0u) memmove(out, base + offset, element_size);
				break;
			}
			case LS_OP_SLICE_STORE: {
				const u32 slice_reg = runtime_read_u32(fn->code, &pc);
				const u32 index_reg = runtime_read_u32(fn->code, &pc);
				const u32 value_reg = runtime_read_u32(fn->code, &pc);
				const u32 element_size = runtime_read_u32(fn->code, &pc);
				i64 index = 0;
				i64 length = 0;
				void* base_ptr = NULL;
				u8* slice = runtime_frame_ptr(runtime, slice_reg);
				u8* index_ptr = runtime_frame_ptr(runtime, index_reg);
				u8* value = runtime_frame_ptr(runtime, value_reg);
				memcpy(&base_ptr, slice, sizeof(base_ptr));
				memcpy(&length, slice + sizeof(void*), 8u);
				memcpy(&index, index_ptr, 8u);
				u8* base = (u8*)base_ptr;
				if (!base || index < 0 || (u64)index >= (u64)length) goto runtime_execute_function_fail;
				const u64 offset = (u64)index * element_size;
				if (element_size != 0u && offset / element_size != (u64)index) goto runtime_execute_function_fail;
				if (element_size > 0u) memmove(base + offset, value, element_size);
				break;
			}
			case LS_OP_SLICE_REF: {
				/* The resulting pointer overwrites the slice value in place. */
				const u32 slice_reg = runtime_read_u32(fn->code, &pc);
				const u32 index_reg = runtime_read_u32(fn->code, &pc);
				const u32 element_size = runtime_read_u32(fn->code, &pc);
				i64 index = 0;
				i64 length = 0;
				void* base_ptr = NULL;
				u8* slice = runtime_frame_ptr(runtime, slice_reg);
				u8* index_ptr = runtime_frame_ptr(runtime, index_reg);
				u8* out = slice;
				memcpy(&base_ptr, slice, sizeof(base_ptr));
				memcpy(&length, slice + sizeof(void*), 8u);
				memcpy(&index, index_ptr, 8u);
				u8* base = (u8*)base_ptr;
				if (!base || index < 0 || (u64)index >= (u64)length) goto runtime_execute_function_fail;
				const u64 offset = (u64)index * element_size;
				if (element_size != 0u && offset / element_size != (u64)index) goto runtime_execute_function_fail;
				void* ref = base + offset;
				memcpy(out, &ref, sizeof(ref));
				break;
			}
			case LS_OP_SLICE_LENGTH: {
				/* The length overwrites the slice value in place. */
				const u32 slice_reg = runtime_read_u32(fn->code, &pc);
				i64 length = 0;
				u8* slice = runtime_frame_ptr(runtime, slice_reg);
				u8* out = slice;
				memcpy(&length, slice + sizeof(void*), 8u);
				memcpy(out, &length, 8u);
				break;
			}
			case LS_OP_RETURN: {
				const u32 src = runtime_read_u32(fn->code, &pc);
				const u32 size = runtime_read_u32(fn->code, &pc);
				if (size > 0u) {
					u8* src_ptr = runtime_frame_ptr(runtime, src);
					if (runtime->frame_base + size > runtime->stack_capacity) goto runtime_execute_function_fail;
					memmove(runtime->stack + runtime->frame_base, src_ptr, size);
					runtime->stack_top = runtime->frame_base + size;
				}
				else {
					runtime->stack_top = runtime->frame_base;
				}
				if (runtime->call_depth == initial_call_depth) {
					runtime->result_function = current_function_index;
					runtime->frame_base = initial_frame_base;
					return 1;
				}
				{
					const runtime_call_frame caller = runtime->call_stack[--runtime->call_depth];
					fn = caller.function;
					current_function_index = caller.function_index;
					pc = caller.pc;
					runtime->frame_base = caller.frame_base;
					runtime->stack_top = caller.stack_top;
				}
				break;
			}
			case LS_OP_CALL_DIRECT: {
				const i32 callee_index = (i32)runtime_read_u32(fn->code, &pc);
				const u32 arg_top = runtime_read_u32(fn->code, &pc);
				const u32 caller_top = runtime->stack_top;
				const ls_function_bc* callee = runtime_find_function(runtime->bytecode, callee_index);
				if (!callee) goto runtime_execute_function_fail;
				
				if (callee->kind == LS_FUNCTION_NATIVE) {
					if ((u32)callee_index >= runtime->native_callback_count) goto runtime_execute_function_fail;
					ls_native_fn callback = runtime->native_callbacks[(u32)callee_index];
					if (!callback) goto runtime_execute_function_fail;
					const u32 call_stack_top = runtime->frame_base + arg_top;
					const u32 arg_base = call_stack_top >= callee->param_size ? call_stack_top - callee->param_size : 0u;
					const u32 expected_top = arg_base + callee->return_size;
					if (expected_top > runtime->stack_capacity) goto runtime_execute_function_fail;
					ls_call_frame frame;
					frame.args = runtime->stack + arg_base;
					frame.result = runtime->stack + arg_base;
					callback(runtime, frame);
					runtime->stack_top = caller_top;
					break;
				}

				if (runtime->call_depth >= LS_MAX_CALL_DEPTH) goto runtime_execute_function_fail;

				const u32 call_stack_top = runtime->frame_base + arg_top;
				const u32 callee_frame_base = call_stack_top >= callee->param_size ? call_stack_top - callee->param_size : 0u;
				const u32 callee_stack_top = callee_frame_base + callee->frame_size;
				if (callee_stack_top > runtime->stack_capacity) goto runtime_execute_function_fail;

				runtime->call_stack[runtime->call_depth] = (runtime_call_frame){ fn, current_function_index, pc, runtime->frame_base, caller_top };
				runtime->call_depth++;
				fn = callee;
				current_function_index = callee_index;
				pc = 0;
				runtime->frame_base = callee_frame_base;
				runtime->stack_top = callee_stack_top;
				break;
			}
			case LS_OP_CALL_INDIRECT: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 arg_size = runtime_read_u32(fn->code, &pc);
				const u32 return_size = runtime_read_u32(fn->code, &pc);
				const u32 arg = dst + 4u;
				u8* callee_ptr = runtime_frame_ptr(runtime, dst);
				u32 callee_index = 0;
				memcpy(&callee_index, callee_ptr, sizeof(callee_index));
				const u32 caller_top = runtime->stack_top;
				runtime->stack_top = runtime->frame_base + arg + arg_size;
				const int ok = runtime_execute_function(runtime, (i32)callee_index);
				if (ok && return_size > 0u) {
					memmove(runtime_frame_ptr(runtime, dst), runtime_frame_ptr(runtime, arg), return_size);
				}
				runtime->stack_top = caller_top;
				if (!ok) {
					runtime->frame_base = saved_frame_base;
					goto runtime_execute_function_fail;
				}
				break;
			}
			case LS_OP_CAST: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 src = runtime_read_u32(fn->code, &pc);
				const ls_type_kind src_kind = (ls_type_kind)fn->code[pc++];
				const ls_type_kind dst_kind = (ls_type_kind)fn->code[pc++];
				u8* src_ptr = runtime_frame_ptr(runtime, src);
				u8* out = runtime_frame_ptr(runtime, dst);
				switch (dst_kind) {
					case LS_TYPE_BOOL: { u8 v = runtime_numeric_to_u64(src_ptr, src_kind) != 0u ? 1u : 0u; memcpy(out, &v, 1u); break; }
					case LS_TYPE_I8:  { i8 v = (i8)runtime_numeric_to_i64(src_ptr, src_kind); memcpy(out, &v, 1u); break; }
					case LS_TYPE_U8:  { u8 v = (u8)runtime_numeric_to_u64(src_ptr, src_kind); memcpy(out, &v, 1u); break; }
					case LS_TYPE_I16: { i16 v = (i16)runtime_numeric_to_i64(src_ptr, src_kind); memcpy(out, &v, 2u); break; }
					case LS_TYPE_U16: { u16 v = (u16)runtime_numeric_to_u64(src_ptr, src_kind); memcpy(out, &v, 2u); break; }
					case LS_TYPE_I32:
					case LS_TYPE_ENUM: { i32 v = (i32)runtime_numeric_to_i64(src_ptr, src_kind); memcpy(out, &v, 4u); break; }
					case LS_TYPE_U32: { u32 v = (u32)runtime_numeric_to_u64(src_ptr, src_kind); memcpy(out, &v, 4u); break; }
					case LS_TYPE_I64: { i64 v = runtime_numeric_to_i64(src_ptr, src_kind); memcpy(out, &v, 8u); break; }
					case LS_TYPE_U64: { u64 v = runtime_numeric_to_u64(src_ptr, src_kind); memcpy(out, &v, 8u); break; }
					case LS_TYPE_F32: { f32 v = (f32)runtime_numeric_to_double(src_ptr, src_kind); memcpy(out, &v, 4u); break; }
					case LS_TYPE_F64: { f64 v = runtime_numeric_to_double(src_ptr, src_kind); memcpy(out, &v, 8u); break; }
					default: break; // should not happen
				}
				break;
			}
			case LS_OP_STRING_TO_CSTR: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 src = runtime_read_u32(fn->code, &pc);
				ls_string_box* box = NULL;
				memcpy(&box, runtime_frame_ptr(runtime, src), sizeof(box));
				if (!box || memchr(box->value.begin, '\0', (size_t)(box->value.end - box->value.begin))) goto runtime_execute_function_fail;
				const char* value = box->value.begin;
				memcpy(runtime_frame_ptr(runtime, dst), &value, sizeof(value));
				break;
			}
			case LS_OP_CSTR_TO_STRING: {
				const u32 dst = runtime_read_u32(fn->code, &pc);
				const u32 src = runtime_read_u32(fn->code, &pc);
				const char* value = NULL;
				memcpy(&value, runtime_frame_ptr(runtime, src), sizeof(value));
				if (!value) goto runtime_execute_function_fail;
				ls_string_box* box = runtime_copy_string_box(runtime, (ls_string_view){value, value + strlen(value)});
				if (!box) goto runtime_execute_function_fail;
				memcpy(runtime_frame_ptr(runtime, dst), &box, sizeof(box));
				break;
			}
			case LS_OP_NEG_I8: LS_REG_NEGOP(i8); break;
			case LS_OP_NEG_U8: LS_REG_NEGOP(u8); break;
			case LS_OP_NEG_I16: LS_REG_NEGOP(i16); break;
			case LS_OP_NEG_U16: LS_REG_NEGOP(u16); break;
			case LS_OP_NEG_I32: LS_REG_NEGOP(i32); break;
			case LS_OP_NEG_U32: LS_REG_NEGOP(u32); break;
			case LS_OP_NEG_I64: LS_REG_NEGOP(i64); break;
			case LS_OP_NEG_U64: LS_REG_NEGOP(u64); break;
			case LS_OP_NEG_F32: LS_REG_NEGOP(f32); break;
			case LS_OP_NEG_F64: LS_REG_NEGOP(f64); break;
			case LS_OP_NOT: LS_REG_NOT(); break;
			case LS_OP_ADD_I8: LS_REG_BINOP(i8, a + b); break;
			case LS_OP_ADD_U8: LS_REG_BINOP(u8, a + b); break;
			case LS_OP_ADD_I16: LS_REG_BINOP(i16, a + b); break;
			case LS_OP_ADD_U16: LS_REG_BINOP(u16, a + b); break;
			case LS_OP_ADD_I32: LS_REG_BINOP(i32, a + b); break;
			case LS_OP_ADD_U32: LS_REG_BINOP(u32, a + b); break;
			case LS_OP_ADD_I64: LS_REG_BINOP(i64, a + b); break;
			case LS_OP_ADD_U64: LS_REG_BINOP(u64, a + b); break;
			case LS_OP_SUB_I8: LS_REG_BINOP(i8, a - b); break;
			case LS_OP_SUB_U8: LS_REG_BINOP(u8, a - b); break;
			case LS_OP_SUB_I16: LS_REG_BINOP(i16, a - b); break;
			case LS_OP_SUB_U16: LS_REG_BINOP(u16, a - b); break;
			case LS_OP_SUB_I32: LS_REG_BINOP(i32, a - b); break;
			case LS_OP_SUB_U32: LS_REG_BINOP(u32, a - b); break;
			case LS_OP_SUB_I64: LS_REG_BINOP(i64, a - b); break;
			case LS_OP_SUB_U64: LS_REG_BINOP(u64, a - b); break;
			case LS_OP_MUL_I8: LS_REG_BINOP(i8, a * b); break;
			case LS_OP_MUL_U8: LS_REG_BINOP(u8, a * b); break;
			case LS_OP_MUL_I16: LS_REG_BINOP(i16, a * b); break;
			case LS_OP_MUL_U16: LS_REG_BINOP(u16, a * b); break;
			case LS_OP_MUL_I32: LS_REG_BINOP(i32, a * b); break;
			case LS_OP_MUL_U32: LS_REG_BINOP(u32, a * b); break;
			case LS_OP_MUL_I64: LS_REG_BINOP(i64, a * b); break;
			case LS_OP_MUL_U64: LS_REG_BINOP(u64, a * b); break;
			case LS_OP_DIV_I8: LS_REG_DIVOP(i8, a / b); break;
			case LS_OP_DIV_U8: LS_REG_DIVOP(u8, a / b); break;
			case LS_OP_DIV_I16: LS_REG_DIVOP(i16, a / b); break;
			case LS_OP_DIV_U16: LS_REG_DIVOP(u16, a / b); break;
			case LS_OP_DIV_I32: LS_REG_DIVOP(i32, a / b); break;
			case LS_OP_DIV_U32: LS_REG_DIVOP(u32, a / b); break;
			case LS_OP_DIV_I64: LS_REG_DIVOP(i64, a / b); break;
			case LS_OP_DIV_U64: LS_REG_DIVOP(u64, a / b); break;
			case LS_OP_ADD_F32: LS_REG_BINOP(f32, a + b); break;
			case LS_OP_SUB_F32: LS_REG_BINOP(f32, a - b); break;
			case LS_OP_MUL_F32: LS_REG_BINOP(f32, a * b); break;
			case LS_OP_DIV_F32: LS_REG_DIVOP(f32, a / b); break;
			case LS_OP_ADD_F64: LS_REG_BINOP(f64, a + b); break;
			case LS_OP_SUB_F64: LS_REG_BINOP(f64, a - b); break;
			case LS_OP_MUL_F64: LS_REG_BINOP(f64, a * b); break;
			case LS_OP_DIV_F64: LS_REG_DIVOP(f64, a / b); break;
			case LS_OP_MOD_I8: LS_REG_DIVOP(i8, a % b); break;
			case LS_OP_MOD_U8: LS_REG_DIVOP(u8, a % b); break;
			case LS_OP_MOD_I16: LS_REG_DIVOP(i16, a % b); break;
			case LS_OP_MOD_U16: LS_REG_DIVOP(u16, a % b); break;
			case LS_OP_MOD_I32: LS_REG_DIVOP(i32, a % b); break;
			case LS_OP_MOD_U32: LS_REG_DIVOP(u32, a % b); break;
			case LS_OP_MOD_I64: LS_REG_DIVOP(i64, a % b); break;
			case LS_OP_MOD_U64: LS_REG_DIVOP(u64, a % b); break;
			case LS_OP_EQ: LS_REG_CMP_NUMERIC(==); break;
			case LS_OP_NE: LS_REG_CMP_NUMERIC(!=); break;
			case LS_OP_LT: LS_REG_CMP_NUMERIC(<); break;
			case LS_OP_LE: LS_REG_CMP_NUMERIC(<=); break;
			case LS_OP_GT: LS_REG_CMP_NUMERIC(>); break;
			case LS_OP_GE: LS_REG_CMP_NUMERIC(>=); break;
			case LS_OP_JUMP: {
				const i32 offset = runtime_read_i32(fn->code, &pc);
				pc = (u32)((i32)pc + offset);
				break;
			}
			case LS_OP_JUMP_IF_FALSE: {
				const u32 cond = runtime_read_u32(fn->code, &pc);
				const i32 offset = runtime_read_i32(fn->code, &pc);
				u8* ptr = runtime_frame_ptr(runtime, cond);
				if (*ptr == 0u) pc = (u32)((i32)pc + offset);
				break;
			}
			case LS_OP_JUMP_IF_TRUE: {
				const u32 cond = runtime_read_u32(fn->code, &pc);
				const i32 offset = runtime_read_i32(fn->code, &pc);
				u8* ptr = runtime_frame_ptr(runtime, cond);
				if (*ptr != 0u) pc = (u32)((i32)pc + offset);
				break;
			}
			default:
				goto runtime_execute_function_fail;
		}
	}

	// Every function's bytecode is compiler-guaranteed to end in LS_OP_RETURN
	// (see LS_OP_RETURN above and emitReturn in bytecode_compiler.cpp), which
	// always returns out of this function directly. Reaching here means pc
	// ran off the end of fn->code without one, i.e. malformed bytecode.
runtime_execute_function_fail:
	runtime->stack_top = initial_stack_top;
	runtime->frame_base = initial_frame_base;
	runtime->result_function = initial_result_function;
	runtime->call_depth = initial_call_depth;
	return 0;
}

ls_runtime* ls_runtime_create(ls_bytecode* bytecode, ls_host* host) {
	if (!bytecode) return NULL;
	if (!host) host = bytecode->host;
	if (!host || !host->arena.allocate) return NULL;

	ls_runtime* runtime = (ls_runtime*)calloc(1, sizeof(ls_runtime));
	if (!runtime) return NULL;

	runtime->bytecode = bytecode;
	runtime->host = host;
	runtime->result_function = -1;
	runtime->arena = &host->arena;

	runtime->stack_capacity = LS_STACK_CAPACITY_BYTES;
	runtime->stack = (u8*)calloc(LS_STACK_CAPACITY_BYTES, 1u);
	if (!runtime->stack) {
		free(runtime);
		return NULL;
	}

	if (bytecode->function_count > 0u) {
		runtime->native_callbacks = (ls_native_fn*)calloc((size_t)bytecode->function_count, sizeof(ls_native_fn));
		if (!runtime->native_callbacks) {
			free(runtime->stack);
			free(runtime);
			return NULL;
		}
		runtime->native_callback_count = bytecode->function_count;
		runtime_bind_builtin_callbacks(runtime);
	}

	runtime->stack_top = bytecode->global_size;
	if (bytecode->has_global_init && bytecode->function_count > 0u) {
		if (!runtime_execute_function(runtime, (i32)(bytecode->function_count - 1u))) {
			ls_runtime_destroy(runtime);
			return NULL;
		}
	}

	return runtime;
}

void ls_runtime_destroy(ls_runtime* runtime) {
	if (!runtime) return;
	free(runtime->stack);
	free(runtime->native_callbacks);
	free(runtime);
}

ls_result ls_runtime_set_native_function_callback_by_bytecode_index(ls_runtime* runtime, int bytecode_index, ls_native_fn callback) {
	if (!runtime || bytecode_index < 0 || (u32)bytecode_index >= runtime->bytecode->function_count) return LS_RESULT_FAILURE;
	if (runtime->bytecode->functions[bytecode_index].kind != LS_FUNCTION_NATIVE) return LS_RESULT_FAILURE;
	runtime->native_callbacks[bytecode_index] = callback;
	return LS_RESULT_OK;
}

void ls_push_bool(ls_runtime* runtime, int value) { u8 v = value ? 1u : 0u; runtime_push_bytes(runtime, &v, 1u); }
void ls_push_i32(ls_runtime* runtime, i32 value) { runtime_push_bytes(runtime, &value, 4u); }
void ls_push_u32(ls_runtime* runtime, u32 value) { runtime_push_bytes(runtime, &value, 4u); }
void ls_push_i64(ls_runtime* runtime, i64 value) { runtime_push_bytes(runtime, &value, 8u); }
void ls_push_u64(ls_runtime* runtime, u64 value) { runtime_push_bytes(runtime, &value, 8u); }
void ls_push_f32(ls_runtime* runtime, float value) { runtime_push_bytes(runtime, &value, 4u); }
void ls_push_f64(ls_runtime* runtime, double value) { runtime_push_bytes(runtime, &value, 8u); }
void ls_push_string(ls_runtime* runtime, ls_string_view value) { void* p = runtime_make_string_box(runtime, value); runtime_push_bytes(runtime, &p, (u32)sizeof(p)); }
void ls_push_null(ls_runtime* runtime) { void* p = NULL; runtime_push_bytes(runtime, &p, (u32)sizeof(p)); }
void ls_push_ptr(ls_runtime* runtime, void* value) { runtime_push_bytes(runtime, &value, (u32)sizeof(value)); }

static void runtime_read_value(ls_runtime* runtime, i32 index, void* out, u32 size) {
	u8* p = NULL;
	if (index >= 0) {
		const u64 offset = (u64)(u32)index * size;
		if (offset + size <= runtime->stack_top) p = runtime->stack + offset;
	} else {
		const i64 offset = (i64)runtime->stack_top + (i64)index * (i64)size;
		if (offset >= 0 && (u64)offset + size <= runtime->stack_top) p = runtime->stack + (u32)offset;
	}
	if (p) memcpy(out, p, size);
}

const void* ls_call_result(ls_runtime* runtime, u32* size) {
	if (size) *size = 0u;
	const ls_function_bc* fn = runtime_find_function(runtime->bytecode, runtime->result_function);
	if (!fn || fn->return_size == 0u || fn->return_size > runtime->stack_top) return NULL;
	if (size) *size = fn->return_size;
	return runtime->stack + runtime->stack_top - fn->return_size;
}

i32 ls_to_bool(ls_runtime* runtime, i32 index) { u8 v = 0; runtime_read_value(runtime, index, &v, 1u); return v != 0u; }
i8 ls_to_i8(ls_runtime* runtime, i32 index) { i8 v = 0; runtime_read_value(runtime, index, &v, 1u); return v; }
u8 ls_to_u8(ls_runtime* runtime, i32 index) { u8 v = 0; runtime_read_value(runtime, index, &v, 1u); return v; }
i16 ls_to_i16(ls_runtime* runtime, i32 index) { i16 v = 0; runtime_read_value(runtime, index, &v, 2u); return v; }
u16 ls_to_u16(ls_runtime* runtime, i32 index) { u16 v = 0; runtime_read_value(runtime, index, &v, 2u); return v; }
i32 ls_to_i32(ls_runtime* runtime, i32 index) { i32 v = 0; runtime_read_value(runtime, index, &v, 4u); return v; }
u32 ls_to_u32(ls_runtime* runtime, i32 index) { u32 v = 0; runtime_read_value(runtime, index, &v, 4u); return v; }
i64 ls_to_i64(ls_runtime* runtime, i32 index) { i64 v = 0; runtime_read_value(runtime, index, &v, 8u); return v; }
u64 ls_to_u64(ls_runtime* runtime, i32 index) { u64 v = 0; runtime_read_value(runtime, index, &v, 8u); return v; }
float ls_to_f32(ls_runtime* runtime, i32 index) { float v = 0.0f; runtime_read_value(runtime, index, &v, 4u); return v; }
double ls_to_f64(ls_runtime* runtime, i32 index) { double v = 0.0; runtime_read_value(runtime, index, &v, 8u); return v; }

ls_string_view ls_to_string(ls_runtime* runtime, i32 index) {
	ls_string_view result;
	result.begin = NULL;
	result.end = NULL;
	void* p = ls_to_ptr(runtime, index);
	return p ? ((ls_string_box*)p)->value : result;
}

void* ls_to_ptr(ls_runtime* runtime, i32 index) {
	void* value = NULL;
	runtime_read_value(runtime, index, &value, (u32)sizeof(value));
	return value;
}

ls_result ls_call(ls_runtime* runtime, ls_string_view function_name) {
	i32 function_index = -1;
	if (!runtime_find_function_by_name(runtime->bytecode, function_name, &function_index)) return LS_RESULT_FAILURE;
	return runtime_execute_function(runtime, function_index) ? LS_RESULT_OK : LS_RESULT_FAILURE;
}

ls_result ls_call_index(ls_runtime* runtime, i32 function_index) {
	return runtime_execute_function(runtime, function_index) ? LS_RESULT_OK : LS_RESULT_FAILURE;
}

ls_type_kind ls_bytecode_runtime_result_kind(ls_runtime* runtime, ls_string_view function_name) {
	const ls_function_bc* fn = runtime_find_function_by_name(runtime->bytecode, function_name, NULL);
	return fn ? fn->return_kind : LS_TYPE_INVALID;
}
