#include "bytecode.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct ls_string_box {
	ls_string_view value;
} ls_string_box;

static int string_equals(ls_string_view a, ls_string_view b) {
	const ptrdiff_t a_size = (ptrdiff_t)(a.end - a.begin);
	const ptrdiff_t b_size = (ptrdiff_t)(b.end - b.begin);
	if (a_size != b_size) return 0;
	for (ptrdiff_t i = 0; i < a_size; ++i) {
		if (a.begin[i] != b.begin[i]) return 0;
	}
	return 1;
}

static int runtime_reserve_stack(ls_runtime* runtime, u32 required) {
	if (!runtime) return 0;
	if (required <= runtime->stack_capacity) return 1;

	u32 new_capacity = runtime->stack_capacity ? runtime->stack_capacity : 16u;
	while (new_capacity < required) {
		if (new_capacity > (u32)(~0u) / 2u) {
			new_capacity = required;
			break;
		}
		new_capacity *= 2u;
	}

	ls_value* new_stack = (ls_value*)realloc(runtime->stack, (size_t)new_capacity * sizeof(ls_value));
	if (!new_stack) return 0;
	runtime->stack = new_stack;
	runtime->stack_capacity = new_capacity;
	return 1;
}

static const ls_function_bc* runtime_find_function(const ls_bytecode* bytecode, i32 function_index) {
	if (!bytecode || function_index < 0) return NULL;
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

static void runtime_push_raw(ls_runtime* runtime, ls_value value) {
	if (!runtime) return;
	if (!runtime_reserve_stack(runtime, runtime->stack_top + 1u)) return;
	runtime->stack[runtime->stack_top++] = value;
}

static ls_value runtime_pop_raw(ls_runtime* runtime) {
	ls_value value;
	memset(&value, 0, sizeof(value));
	if (!runtime || runtime->stack_top == 0) return value;
	value = runtime->stack[--runtime->stack_top];
	return value;
}

static int runtime_string_equals_cstr(ls_string_view value, const char* cstr) {
	const char* a = value.begin;
	const char* b = cstr;
	while (a < value.end && *b) {
		if (*a++ != *b++) return 0;
	}
	return a == value.end && *b == '\0';
}

static void runtime_native_sin_f32(ls_runtime* runtime) {
	ls_push_f32(runtime, sinf(ls_to_f32(runtime, -1)));
}

static void runtime_native_cos_f32(ls_runtime* runtime) {
	ls_push_f32(runtime, cosf(ls_to_f32(runtime, -1)));
}

static void runtime_native_sqrt_f32(ls_runtime* runtime) {
	ls_push_f32(runtime, sqrtf(ls_to_f32(runtime, -1)));
}

static void runtime_native_sin_f64(ls_runtime* runtime) {
	ls_push_f64(runtime, sin(ls_to_f64(runtime, -1)));
}

static void runtime_native_cos_f64(ls_runtime* runtime) {
	ls_push_f64(runtime, cos(ls_to_f64(runtime, -1)));
}

static void runtime_native_sqrt_f64(ls_runtime* runtime) {
	ls_push_f64(runtime, sqrt(ls_to_f64(runtime, -1)));
}

static void runtime_bind_builtin_callbacks(ls_runtime* runtime) {
	if (!runtime || !runtime->bytecode || !runtime->native_callbacks) return;
	for (u32 i = 0; i < runtime->bytecode->function_count; ++i) {
		const ls_function_bc* fn = &runtime->bytecode->functions[i];
		if (fn->kind != LS_FUNCTION_NATIVE || !fn->is_builtin_native) continue;
		if (runtime_string_equals_cstr(fn->name, "sin")) runtime->native_callbacks[i] = &runtime_native_sin_f32;
		else if (runtime_string_equals_cstr(fn->name, "cos")) runtime->native_callbacks[i] = &runtime_native_cos_f32;
		else if (runtime_string_equals_cstr(fn->name, "sqrt")) runtime->native_callbacks[i] = &runtime_native_sqrt_f32;
		else if (runtime_string_equals_cstr(fn->name, "sin_f64")) runtime->native_callbacks[i] = &runtime_native_sin_f64;
		else if (runtime_string_equals_cstr(fn->name, "cos_f64")) runtime->native_callbacks[i] = &runtime_native_cos_f64;
		else if (runtime_string_equals_cstr(fn->name, "sqrt_f64")) runtime->native_callbacks[i] = &runtime_native_sqrt_f64;
	}
}

static ls_value* runtime_stack_slot(ls_runtime* runtime, i32 index) {
	if (!runtime) return NULL;
	i32 resolved = index;
	if (index < 0) {
		resolved = (i32)runtime->stack_top + index;
	}
	if (resolved < 0) return NULL;
	if ((u32)resolved >= runtime->stack_top) return NULL;
	return &runtime->stack[(u32)resolved];
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

#define LS_MAKE_FN(TYPE) \
	static ls_value runtime_make_##TYPE(TYPE value) { ls_value r; r.##TYPE##val = value; return r; }

LS_MAKE_FN(i8)
LS_MAKE_FN(u8)
LS_MAKE_FN(i16)
LS_MAKE_FN(u16)
LS_MAKE_FN(i32)
LS_MAKE_FN(u32)
LS_MAKE_FN(i64)
LS_MAKE_FN(u64)
LS_MAKE_FN(f32)
LS_MAKE_FN(f64)

#undef LS_MAKE_FN

static void runtime_store_u64(ls_value* slot, u64 value) {
	memset(slot, 0, sizeof(*slot));
	slot->u64val = value;
}

static u32 runtime_decode_base_slot(ls_runtime* runtime, ls_value base) {
	const i64 signed_base = base.i64val;
	if (signed_base < 0) {
		const u32 local_slot = (u32)(-(signed_base + 1));
		return runtime->frame_base + local_slot;
	}
	return (u32)base.u64val;
}

static int runtime_value_truthy(ls_value value) {
	return value.u64val != 0u;
}

static double runtime_numeric_to_double(ls_value value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL: return value.u64val != 0u ? 1.0 : 0.0;
		case LS_TYPE_I8:   return (double)value.i8val;
		case LS_TYPE_U8:   return (double)value.u8val;
		case LS_TYPE_I16:  return (double)value.i16val;
		case LS_TYPE_U16:  return (double)value.u16val;
		case LS_TYPE_I32:  return (double)value.i32val;
		case LS_TYPE_U32:  return (double)value.u32val;
		case LS_TYPE_I64:  return (double)value.i64val;
		case LS_TYPE_U64:  return (double)value.u64val;
		case LS_TYPE_F32:  return (double)value.f32val;
		case LS_TYPE_F64:  return value.f64val;
		case LS_TYPE_ENUM: return (double)value.i32val;
		default:           return 0.0;
	}
}

static i64 runtime_numeric_to_i64(ls_value value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL: return value.u64val != 0u ? 1 : 0;
		case LS_TYPE_I8:   return (i64)value.i8val;
		case LS_TYPE_U8:   return (i64)value.u8val;
		case LS_TYPE_I16:  return (i64)value.i16val;
		case LS_TYPE_U16:  return (i64)value.u16val;
		case LS_TYPE_I32:  return (i64)value.i32val;
		case LS_TYPE_U32:  return (i64)value.u32val;
		case LS_TYPE_I64:  return value.i64val;
		case LS_TYPE_U64:  return (i64)value.u64val;
		case LS_TYPE_F32:  return (i64)value.f32val;
		case LS_TYPE_F64:  return (i64)value.f64val;
		case LS_TYPE_ENUM: return (i64)value.i32val;
		default:           return 0;
	}
}

static u64 runtime_numeric_to_u64(ls_value value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL: return value.u64val != 0u ? 1u : 0u;
		case LS_TYPE_I8:   return (u64)value.i8val;
		case LS_TYPE_U8:   return (u64)value.u8val;
		case LS_TYPE_I16:  return (u64)value.i16val;
		case LS_TYPE_U16:  return (u64)value.u16val;
		case LS_TYPE_I32:  return (u64)value.i32val;
		case LS_TYPE_U32:  return (u64)value.u32val;
		case LS_TYPE_I64:  return (u64)value.i64val;
		case LS_TYPE_U64:  return value.u64val;
		case LS_TYPE_F32:  return (u64)value.f32val;
		case LS_TYPE_F64:  return (u64)value.f64val;
		case LS_TYPE_ENUM: return (u64)value.i32val;
		default:           return 0u;
	}
}

#define LS_CMP_BODY(OP, INVERT_STRING) \
	{ \
		const ls_type_kind kind = (ls_type_kind)fn->code[pc++]; \
		ls_value rhs = runtime_pop_raw(runtime); \
		ls_value lhs = runtime_pop_raw(runtime); \
		int result = 0; \
		switch (kind) { \
			case LS_TYPE_STRING: \
				if (lhs.cptr == rhs.cptr) { \
					result = 1; \
				} \
				else if (lhs.cptr && rhs.cptr) { \
					result = string_equals(((ls_string_box*)lhs.cptr)->value, ((ls_string_box*)rhs.cptr)->value); \
				} \
				if (INVERT_STRING) result = !result; \
				break; \
			case LS_TYPE_I8:  result = lhs.i8val  OP rhs.i8val;  break; \
			case LS_TYPE_U8:  result = lhs.u8val  OP rhs.u8val;  break; \
			case LS_TYPE_I16: result = lhs.i16val OP rhs.i16val; break; \
			case LS_TYPE_U16: result = lhs.u16val OP rhs.u16val; break; \
			case LS_TYPE_I32: result = lhs.i32val OP rhs.i32val; break; \
			case LS_TYPE_U32: result = lhs.u32val OP rhs.u32val; break; \
			case LS_TYPE_I64: result = lhs.i64val OP rhs.i64val; break; \
			case LS_TYPE_U64: result = lhs.u64val OP rhs.u64val; break; \
			case LS_TYPE_F32: result = lhs.f32val OP rhs.f32val; break; \
			case LS_TYPE_F64: result = lhs.f64val OP rhs.f64val; break; \
			default: break; \
		} \
		ls_value r; memset(&r, 0, sizeof(r)); r.u64val = result ? 1u : 0u; \
		runtime_push_raw(runtime, r); \
	}

#define LS_BINOP(TYPE, EXPR) \
	{ \
		if (runtime->stack_top >= 2) { \
			u32 top = --runtime->stack_top; \
			ls_value rhs = runtime->stack[top]; \
			ls_value lhs = runtime->stack[top - 1]; \
			const TYPE a = lhs.##TYPE##val; \
			const TYPE b = rhs.##TYPE##val; \
			runtime->stack[top - 1].##TYPE##val = EXPR; \
		} \
	}

#define LS_INT_DIVOP(TYPE, EXPR) \
	{ \
		if (runtime->stack_top >= 2) { \
			u32 top = --runtime->stack_top; \
			ls_value rhs = runtime->stack[top]; \
			ls_value lhs = runtime->stack[top - 1]; \
			const TYPE a = lhs.##TYPE##val; \
			const TYPE b = rhs.##TYPE##val; \
			if (!b) { runtime->frame_base = saved_frame_base; return 0; } \
			runtime->stack[top - 1].##TYPE##val = EXPR; \
		} \
	}

#define LS_FLOAT_DIVOP(TYPE) \
	{ \
		if (runtime->stack_top >= 2) { \
			u32 top = --runtime->stack_top; \
			ls_value rhs = runtime->stack[top]; \
			ls_value lhs = runtime->stack[top - 1]; \
			const TYPE a = lhs.##TYPE##val; \
			const TYPE b = rhs.##TYPE##val; \
			if (b == (TYPE)0) { runtime->frame_base = saved_frame_base; return 0; } \
			runtime->stack[top - 1].##TYPE##val = a / b; \
		} \
	}

#define LS_NEG(TYPE) \
	{ \
		ls_value value = runtime_pop_raw(runtime); \
		runtime_push_raw(runtime, runtime_make_##TYPE(-value.##TYPE##val)); \
	}

static ls_value runtime_cast_value(ls_value value, ls_type_kind src_kind, ls_type_kind dst_kind) {
	ls_value result;
	memset(&result, 0, sizeof(result));

	switch (dst_kind) {
		case LS_TYPE_BOOL:
			result.u64val = runtime_numeric_to_u64(value, src_kind) != 0u ? 1u : 0u;
			return result;
		case LS_TYPE_I8:  result.i8val = (i8)runtime_numeric_to_i64(value, src_kind); return result;
		case LS_TYPE_U8:  result.u8val = (u8)runtime_numeric_to_u64(value, src_kind); return result;
		case LS_TYPE_I16: result.i16val = (i16)runtime_numeric_to_i64(value, src_kind); return result;
		case LS_TYPE_U16: result.u16val = (u16)runtime_numeric_to_u64(value, src_kind); return result;
		case LS_TYPE_I32: result.i32val = (i32)runtime_numeric_to_i64(value, src_kind); return result;
		case LS_TYPE_U32: result.u32val = (u32)runtime_numeric_to_u64(value, src_kind); return result;
		case LS_TYPE_I64: result.i64val = runtime_numeric_to_i64(value, src_kind); return result;
		case LS_TYPE_U64: result.u64val = runtime_numeric_to_u64(value, src_kind); return result;
		case LS_TYPE_F32: result.f32val = (float)runtime_numeric_to_double(value, src_kind); return result;
		case LS_TYPE_F64: result.f64val = runtime_numeric_to_double(value, src_kind); return result;
		case LS_TYPE_ENUM: result.i32val = (i32)runtime_numeric_to_i64(value, src_kind); return result;
		case LS_TYPE_CPTR:
			result.cptr = value.cptr;
			return result;
		default:
			return value;
	}
}

static int runtime_execute_function(ls_runtime* runtime, i32 function_index);

static int runtime_execute_native(ls_runtime* runtime, i32 function_index) {
	if (!runtime || !runtime->native_callbacks) return 0;
	if (function_index < 0 || (u32)function_index >= runtime->native_callback_count) return 0;
	ls_native_fn callback = runtime->native_callbacks[(u32)function_index];
	if (!callback) return 0;

	const ls_function_bc* fn = runtime_find_function(runtime->bytecode, function_index);
	if (!fn) return 0;

	const u32 arg_base = runtime->stack_top >= fn->param_slot_count ? runtime->stack_top - fn->param_slot_count : 0u;
	callback(runtime);

	const u32 expected_top = arg_base + fn->return_slot_count;
	if (!runtime_reserve_stack(runtime, expected_top)) return 0;
	if (fn->return_slot_count > 0u && runtime->stack_top >= fn->return_slot_count) {
		memmove(&runtime->stack[arg_base], &runtime->stack[runtime->stack_top - fn->return_slot_count], (size_t)fn->return_slot_count * sizeof(ls_value));
	}
	runtime->stack_top = expected_top;
	return 1;
}

static int runtime_execute_function(ls_runtime* runtime, i32 function_index) {
	const ls_function_bc* fn = runtime_find_function(runtime->bytecode, function_index);
	if (!fn) return 0;
	if (fn->kind == LS_FUNCTION_NATIVE) {
		return runtime_execute_native(runtime, function_index);
	}
	if (!fn->code && fn->code_size != 0u) return 0;

	const u32 arg_base = runtime->stack_top >= fn->param_slot_count ? runtime->stack_top - fn->param_slot_count : 0u;
	const u32 saved_frame_base = runtime->frame_base;
	const u32 frame_base = arg_base;
	const u32 local_base = frame_base + fn->param_slot_count;
	const u32 frame_stack_top = local_base + fn->local_slot_count;
	const u32 stack_limit = frame_base + fn->max_stack;

	if (!runtime_reserve_stack(runtime, stack_limit)) return 0;
	for (u32 i = local_base; i < frame_stack_top; ++i) {
		memset(&runtime->stack[i], 0, sizeof(ls_value));
	}

	runtime->frame_base = frame_base;
	runtime->stack_top = frame_stack_top;

	u32 pc = 0;
	while (pc < fn->code_size) {
		const ls_op op = (ls_op)fn->code[pc++];
		switch (op) {
			case LS_OP_NOP:
				break;
			case LS_OP_LOAD_CONST_1: {
				ls_value value;
				memset(&value, 0, sizeof(value));
				value.u8val = fn->code[pc++];
				runtime_push_raw(runtime, value);
				break;
			}
			case LS_OP_LOAD_CONST_2: {
				ls_value value;
				memset(&value, 0, sizeof(value));
				memcpy(&value, fn->code + pc, 2);
				pc += 2u;
				runtime_push_raw(runtime, value);
				break;
			}
			case LS_OP_LOAD_CONST_4: {
				ls_value value;
				memset(&value, 0, sizeof(value));
				memcpy(&value, fn->code + pc, 4);
				pc += 4u;
				runtime_push_raw(runtime, value);
				break;
			}
			case LS_OP_LOAD_CONST_8: {
				u64 raw = runtime_read_u64(fn->code, &pc);
				ls_value value;
				memset(&value, 0, sizeof(value));
				if (fn->return_kind == LS_TYPE_STRING) {
					if (runtime->bytecode && raw < runtime->bytecode->string_count) {
						ls_string_box* box = runtime_make_string_box(runtime, runtime->bytecode->strings[(u32)raw]);
						value.cptr = box;
					}
				}
				else {
					runtime_store_u64(&value, raw);
				}
				runtime_push_raw(runtime, value);
				break;
			}
			case LS_OP_LOAD_CONST_STRING: {
				const u32 index = runtime_read_u32(fn->code, &pc);
				ls_value value;
				memset(&value, 0, sizeof(value));
				if (runtime->bytecode && index < runtime->bytecode->string_count) {
					ls_string_box* box = runtime_make_string_box(runtime, runtime->bytecode->strings[index]);
					value.cptr = box;
				}
				runtime_push_raw(runtime, value);
				break;
			}
			case LS_OP_LOAD_LOCAL: {
				const u32 slot = runtime_read_u32(fn->code, &pc);
				if (runtime->frame_base + slot >= runtime->stack_top) return 0;
				runtime_push_raw(runtime, runtime->stack[runtime->frame_base + slot]);
				break;
			}
			case LS_OP_STORE_LOCAL: {
				const u32 slot = runtime_read_u32(fn->code, &pc);
				ls_value value = runtime_pop_raw(runtime);
				if (runtime->frame_base + slot >= runtime->stack_top) return 0;
				runtime->stack[runtime->frame_base + slot] = value;
				break;
			}
			case LS_OP_LOAD_GLOBAL: {
				const u32 slot = runtime_read_u32(fn->code, &pc);
				if (slot >= runtime->stack_top) return 0;
				runtime_push_raw(runtime, runtime->stack[slot]);
				break;
			}
			case LS_OP_STORE_GLOBAL: {
				const u32 slot = runtime_read_u32(fn->code, &pc);
				ls_value value = runtime_pop_raw(runtime);
				if (slot >= runtime->stack_top) return 0;
				runtime->stack[slot] = value;
				break;
			}
			case LS_OP_LOCAL_REF: {
				const u32 slot = runtime_read_u32(fn->code, &pc);
				const u32 absolute_slot = runtime->frame_base + slot;
				if (absolute_slot >= runtime->stack_top) return 0;
				runtime_push_raw(runtime, runtime_make_u64(absolute_slot));
				break;
			}
			case LS_OP_GLOBAL_REF: {
				const u32 slot = runtime_read_u32(fn->code, &pc);
				if (slot >= runtime->stack_top) return 0;
				runtime_push_raw(runtime, runtime_make_u64(slot));
				break;
			}
			case LS_OP_LOAD_AT: {
				const u32 scale = runtime_read_u32(fn->code, &pc);
				const i32 offset = runtime_read_i32(fn->code, &pc);
				ls_value index = runtime_pop_raw(runtime);
				ls_value base = runtime_pop_raw(runtime);
				const i64 slot = (i64)runtime_decode_base_slot(runtime, base) + (i64)index.i64val * (i64)scale + (i64)offset;
				if (slot < 0 || (u64)slot >= runtime->stack_top) return 0;
				runtime_push_raw(runtime, runtime->stack[(u32)slot]);
				break;
			}
			case LS_OP_STORE_AT: {
				const u32 scale = runtime_read_u32(fn->code, &pc);
				const i32 offset = runtime_read_i32(fn->code, &pc);
				ls_value value = runtime_pop_raw(runtime);
				ls_value index = runtime_pop_raw(runtime);
				ls_value base = runtime_pop_raw(runtime);
				const i64 slot = (i64)runtime_decode_base_slot(runtime, base) + (i64)index.i64val * (i64)scale + (i64)offset;
				if (slot < 0 || (u64)slot >= runtime->stack_top) return 0;
				runtime->stack[(u32)slot] = value;
				break;
			}
			case LS_OP_REF_AT: {
				const u32 scale = runtime_read_u32(fn->code, &pc);
				const i32 offset = runtime_read_i32(fn->code, &pc);
				ls_value index = runtime_pop_raw(runtime);
				ls_value base = runtime_pop_raw(runtime);
				const i64 slot = (i64)runtime_decode_base_slot(runtime, base) + (i64)index.i64val * (i64)scale + (i64)offset;
				if (slot < 0 || (u64)slot >= runtime->stack_top) return 0;
				runtime_push_raw(runtime, runtime_make_u64((u64)slot));
				break;
			}
			case LS_OP_SLICE: {
				const u32 element_slots = runtime_read_u32(fn->code, &pc);
				if (runtime->stack_top < 4u) return 0;
				/* Slice operands are ordered base, length, begin, end. The base is
				   already absolute, which keeps returned slices tied to caller data. */
				const i64 end = runtime_pop_raw(runtime).i64val;
				const i64 begin = runtime_pop_raw(runtime).i64val;
				const u64 length = runtime_pop_raw(runtime).u64val;
				const u64 base = runtime_pop_raw(runtime).u64val;
				if (begin < 0 || end < begin || (u64)end > length) return 0;
				const u64 offset = (u64)begin * element_slots;
				if (element_slots != 0u && offset / element_slots != (u64)begin) return 0;
				if (base > ~(u64)0 - offset) return 0;
				runtime_push_raw(runtime, runtime_make_u64(base + offset));
				runtime_push_raw(runtime, runtime_make_u64((u64)(end - begin)));
				break;
			}
			case LS_OP_SLICE_LOAD: {
				const u32 element_slots = runtime_read_u32(fn->code, &pc);
				if (runtime->stack_top < 3u) return 0;
				const i64 index = runtime_pop_raw(runtime).i64val;
				const u64 length = runtime_pop_raw(runtime).u64val;
				const u64 base = runtime_pop_raw(runtime).u64val;
				/* Check the logical slice bound before validating the physical slot;
				   the backing array can be larger than the visible slice. */
				if (index < 0 || (u64)index >= length) return 0;
				const u64 offset = (u64)index * element_slots;
				if (element_slots != 0u && offset / element_slots != (u64)index) return 0;
				if (base > ~(u64)0 - offset || base + offset >= runtime->stack_top) return 0;
				runtime_push_raw(runtime, runtime->stack[(u32)(base + offset)]);
				break;
			}
			case LS_OP_SLICE_STORE: {
				const u32 element_slots = runtime_read_u32(fn->code, &pc);
				if (runtime->stack_top < 4u) return 0;
				const ls_value value = runtime_pop_raw(runtime);
				const i64 index = runtime_pop_raw(runtime).i64val;
				const u64 length = runtime_pop_raw(runtime).u64val;
				const u64 base = runtime_pop_raw(runtime).u64val;
				if (index < 0 || (u64)index >= length) return 0;
				const u64 offset = (u64)index * element_slots;
				if (element_slots != 0u && offset / element_slots != (u64)index) return 0;
				if (base > ~(u64)0 - offset || base + offset >= runtime->stack_top) return 0;
				runtime->stack[(u32)(base + offset)] = value;
				break;
			}
			case LS_OP_SLICE_LENGTH: {
				if (runtime->stack_top < 2u) return 0;
				const ls_value length = runtime_pop_raw(runtime);
				(void)runtime_pop_raw(runtime);
				runtime_push_raw(runtime, length);
				break;
			}
			case LS_OP_POP:
				(void)runtime_pop_raw(runtime);
				break;
			case LS_OP_RETURN: {
				if (fn->return_slot_count > 0u) {
					if (runtime->stack_top < fn->return_slot_count) return 0;
					memmove(&runtime->stack[runtime->frame_base], &runtime->stack[runtime->stack_top - fn->return_slot_count], (size_t)fn->return_slot_count * sizeof(ls_value));
					runtime->stack_top = runtime->frame_base + fn->return_slot_count;
				}
				else {
					runtime->stack_top = runtime->frame_base;
				}
				runtime->frame_base = saved_frame_base;
				return 1;
			}
			case LS_OP_ABORT:
				runtime->frame_base = saved_frame_base;
				return 0;
			case LS_OP_CALL_DIRECT: {
				const i32 callee_index = (i32)runtime_read_u32(fn->code, &pc);
				if (!runtime_execute_function(runtime, callee_index)) {
					runtime->frame_base = saved_frame_base;
					return 0;
				}
				break;
			}
			case LS_OP_CALL_INDIRECT: {
				const u32 arg_count = runtime_read_u32(fn->code, &pc);
				if (runtime->stack_top < arg_count + 1u) return 0;
				const u32 callee_slot = runtime->stack_top - arg_count - 1u;
				const i32 callee_index = (i32)runtime->stack[callee_slot].u64val;
				memmove(&runtime->stack[callee_slot], &runtime->stack[callee_slot + 1u], (size_t)arg_count * sizeof(ls_value));
				runtime->stack_top -= 1u;
				if (!runtime_execute_function(runtime, callee_index)) {
					runtime->frame_base = saved_frame_base;
					return 0;
				}
				break;
			}
			case LS_OP_CAST: {
				const ls_type_kind src_kind = (ls_type_kind)fn->code[pc++];
				const ls_type_kind dst_kind = (ls_type_kind)fn->code[pc++];
				ls_value value = runtime_pop_raw(runtime);
				value = runtime_cast_value(value, src_kind, dst_kind);
				runtime_push_raw(runtime, value);
				break;
			}
			case LS_OP_NEG_I8: LS_NEG(i8) break;
			case LS_OP_NEG_U8: LS_NEG(u8) break;
			case LS_OP_NEG_I16: LS_NEG(i16) break;
			case LS_OP_NEG_U16: LS_NEG(u16) break;
			case LS_OP_NEG_I32: LS_NEG(i32) break;
			case LS_OP_NEG_U32: LS_NEG(u32) break;
			case LS_OP_NEG_I64: LS_NEG(i64) break;
			case LS_OP_NEG_U64: LS_NEG(u64) break;
			case LS_OP_NEG_F32: LS_NEG(f32) break;
			case LS_OP_NEG_F64: LS_NEG(f64) break;
			case LS_OP_NOT: {
				ls_value value = runtime_pop_raw(runtime);
				runtime_push_raw(runtime, runtime_make_u64(runtime_value_truthy(value) ? 0u : 1u));
				break;
			}
			case LS_OP_AND: {
				ls_value rhs = runtime_pop_raw(runtime);
				ls_value lhs = runtime_pop_raw(runtime);
				runtime_push_raw(runtime, runtime_make_u64((runtime_value_truthy(lhs) && runtime_value_truthy(rhs)) ? 1u : 0u));
				break;
			}
			case LS_OP_OR: {
				ls_value rhs = runtime_pop_raw(runtime);
				ls_value lhs = runtime_pop_raw(runtime);
				runtime_push_raw(runtime, runtime_make_u64((runtime_value_truthy(lhs) || runtime_value_truthy(rhs)) ? 1u : 0u));
				break;
			}
			case LS_OP_ADD_I8: LS_BINOP(i8, a + b) break;
			case LS_OP_ADD_U8: LS_BINOP(u8, a + b) break;
			case LS_OP_ADD_I16: LS_BINOP(i16, a + b) break;
			case LS_OP_ADD_U16: LS_BINOP(u16, a + b) break;
			case LS_OP_ADD_I32: LS_BINOP(i32, a + b) break;
			case LS_OP_ADD_U32: LS_BINOP(u32, a + b) break;
			case LS_OP_ADD_I64: LS_BINOP(i64, a + b) break;
			case LS_OP_ADD_U64: LS_BINOP(u64, a + b) break;
			case LS_OP_SUB_I8: LS_BINOP(i8, a - b) break;
			case LS_OP_SUB_U8: LS_BINOP(u8, a - b) break;
			case LS_OP_SUB_I16: LS_BINOP(i16, a - b) break;
			case LS_OP_SUB_U16: LS_BINOP(u16, a - b) break;
			case LS_OP_SUB_I32: LS_BINOP(i32, a - b) break;
			case LS_OP_SUB_U32: LS_BINOP(u32, a - b) break;
			case LS_OP_SUB_I64: LS_BINOP(i64, a - b) break;
			case LS_OP_SUB_U64: LS_BINOP(u64, a - b) break;
			case LS_OP_MUL_I8: LS_BINOP(i8, a * b) break;
			case LS_OP_MUL_U8: LS_BINOP(u8, a * b) break;
			case LS_OP_MUL_I16: LS_BINOP(i16, a * b) break;
			case LS_OP_MUL_U16: LS_BINOP(u16, a * b) break;
			case LS_OP_MUL_I32: LS_BINOP(i32, a * b) break;
			case LS_OP_MUL_U32: LS_BINOP(u32, a * b) break;
			case LS_OP_MUL_I64: LS_BINOP(i64, a * b) break;
			case LS_OP_MUL_U64: LS_BINOP(u64, a * b) break;
			case LS_OP_DIV_I8:  LS_INT_DIVOP(i8,  a / b) break;
			case LS_OP_DIV_U8:  LS_INT_DIVOP(u8,  a / b) break;
			case LS_OP_DIV_I16: LS_INT_DIVOP(i16, a / b) break;
			case LS_OP_DIV_U16: LS_INT_DIVOP(u16, a / b) break;
			case LS_OP_DIV_I32: LS_INT_DIVOP(i32, a / b) break;
			case LS_OP_DIV_U32: LS_INT_DIVOP(u32, a / b) break;
			case LS_OP_DIV_I64: LS_INT_DIVOP(i64, a / b) break;
			case LS_OP_DIV_U64: LS_INT_DIVOP(u64, a / b) break;
			case LS_OP_ADD_F32: LS_BINOP(f32, a + b) break;
			case LS_OP_SUB_F32: LS_BINOP(f32, a - b) break;
			case LS_OP_MUL_F32: LS_BINOP(f32, a * b) break;
			case LS_OP_DIV_F32: LS_FLOAT_DIVOP(f32) break;
			case LS_OP_ADD_F64: LS_BINOP(f64, a + b) break;
			case LS_OP_SUB_F64: LS_BINOP(f64, a - b) break;
			case LS_OP_MUL_F64: LS_BINOP(f64, a * b) break;
			case LS_OP_DIV_F64: LS_FLOAT_DIVOP(f64) break;
			case LS_OP_MOD_I8:  LS_INT_DIVOP(i8,  a % b) break;
			case LS_OP_MOD_U8:  LS_INT_DIVOP(u8,  a % b) break;
			case LS_OP_MOD_I16: LS_INT_DIVOP(i16, a % b) break;
			case LS_OP_MOD_U16: LS_INT_DIVOP(u16, a % b) break;
			case LS_OP_MOD_I32: LS_INT_DIVOP(i32, a % b) break;
			case LS_OP_MOD_U32: LS_INT_DIVOP(u32, a % b) break;
			case LS_OP_MOD_I64: LS_INT_DIVOP(i64, a % b) break;
			case LS_OP_MOD_U64: LS_INT_DIVOP(u64, a % b) break;
			case LS_OP_EQ: LS_CMP_BODY(==, 0) break;
			case LS_OP_NE: LS_CMP_BODY(!=, 1) break;
			case LS_OP_LT: LS_CMP_BODY(<, 0)  break;
			case LS_OP_LE: LS_CMP_BODY(<=, 0) break;
			case LS_OP_GT: LS_CMP_BODY(>, 0)  break;
			case LS_OP_GE: LS_CMP_BODY(>=, 0) break;
			case LS_OP_JUMP: {
				const i32 offset = runtime_read_i32(fn->code, &pc);
				pc = (u32)((i32)pc + offset);
				break;
			}
			case LS_OP_JUMP_IF_FALSE: {
				const i32 offset = runtime_read_i32(fn->code, &pc);
				ls_value cond = runtime_pop_raw(runtime);
				if (!runtime_value_truthy(cond)) {
					pc = (u32)((i32)pc + offset);
				}
				break;
			}
			case LS_OP_JUMP_IF_TRUE: {
				const i32 offset = runtime_read_i32(fn->code, &pc);
				ls_value cond = runtime_pop_raw(runtime);
				if (runtime_value_truthy(cond)) {
					pc = (u32)((i32)pc + offset);
				}
				break;
			}
			default:
				runtime->frame_base = saved_frame_base;
				return 0;
		}
	}

	if (fn->return_slot_count > 0u) {
		if (runtime->stack_top < fn->return_slot_count) {
			runtime->frame_base = saved_frame_base;
			return 0;
		}
		memmove(&runtime->stack[runtime->frame_base], &runtime->stack[runtime->stack_top - fn->return_slot_count], (size_t)fn->return_slot_count * sizeof(ls_value));
		runtime->stack_top = runtime->frame_base + fn->return_slot_count;
	}
	else {
		runtime->stack_top = runtime->frame_base;
	}
	runtime->frame_base = saved_frame_base;
	return 1;
}

ls_runtime* ls_runtime_create(ls_bytecode* bytecode) {
	if (!bytecode) return NULL;

	ls_runtime* runtime = (ls_runtime*)calloc(1, sizeof(ls_runtime));
	if (!runtime) return NULL;

	runtime->bytecode = bytecode;
	runtime->host = bytecode->host;
	if (runtime->host && runtime->host->create_arena) {
		runtime->arena = runtime->host->create_arena();
		if (!runtime->arena) {
			free(runtime);
			return NULL;
		}
	}

	runtime->stack_capacity = bytecode->global_slot_count > 16u ? bytecode->global_slot_count : 16u;
	runtime->stack = (ls_value*)calloc((size_t)runtime->stack_capacity, sizeof(ls_value));
	if (!runtime->stack) {
		if (runtime->host && runtime->host->destroy_arena && runtime->arena) runtime->host->destroy_arena(runtime->arena);
		free(runtime);
		return NULL;
	}

	if (bytecode->function_count > 0u) {
		runtime->native_callbacks = (ls_native_fn*)calloc((size_t)bytecode->function_count, sizeof(ls_native_fn));
		if (!runtime->native_callbacks) {
			free(runtime->stack);
			if (runtime->host && runtime->host->destroy_arena && runtime->arena) runtime->host->destroy_arena(runtime->arena);
			free(runtime);
			return NULL;
		}
		runtime->native_callback_count = bytecode->function_count;
		runtime->native_callback_capacity = bytecode->function_count;
		runtime_bind_builtin_callbacks(runtime);
	}

	runtime->stack_top = bytecode->global_slot_count;
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
	free(runtime->frames);
	free(runtime->native_callbacks);
	if (runtime->host && runtime->host->destroy_arena && runtime->arena) {
		runtime->host->destroy_arena(runtime->arena);
	}
	free(runtime);
}

ls_result ls_runtime_set_native_function_callback(
	ls_runtime* runtime,
	int function_index,
	ls_native_fn callback
) {
	if (!runtime || function_index < 0) return LS_RESULT_FAILURE;
	i32 native_index = 0;
	for (u32 i = 0; i < runtime->bytecode->function_count; ++i) {
		if (runtime->bytecode->functions[i].kind != LS_FUNCTION_NATIVE) continue;
		if (native_index == function_index) {
			runtime->native_callbacks[i] = callback;
			return LS_RESULT_OK;
		}
		++native_index;
	}
	return LS_RESULT_FAILURE;
}

void ls_push_bool(ls_runtime* runtime, int value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.u64val = value ? 1u : 0u;
	runtime_push_raw(runtime, slot);
}

void ls_push_i32(ls_runtime* runtime, i32 value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.i32val = value;
	runtime_push_raw(runtime, slot);
}

void ls_push_u32(ls_runtime* runtime, u32 value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.u32val = value;
	runtime_push_raw(runtime, slot);
}

void ls_push_i64(ls_runtime* runtime, i64 value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.i64val = value;
	runtime_push_raw(runtime, slot);
}

void ls_push_u64(ls_runtime* runtime, u64 value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.u64val = value;
	runtime_push_raw(runtime, slot);
}

void ls_push_f32(ls_runtime* runtime, float value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.f32val = value;
	runtime_push_raw(runtime, slot);
}

void ls_push_f64(ls_runtime* runtime, double value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.f64val = value;
	runtime_push_raw(runtime, slot);
}

void ls_push_string(ls_runtime* runtime, ls_string_view value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.cptr = runtime_make_string_box(runtime, value);
	runtime_push_raw(runtime, slot);
}

void ls_push_null(ls_runtime* runtime) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.cptr = NULL;
	runtime_push_raw(runtime, slot);
}

void ls_push_ptr(ls_runtime* runtime, void* value) {
	ls_value slot;
	memset(&slot, 0, sizeof(slot));
	slot.cptr = value;
	runtime_push_raw(runtime, slot);
}

i32 ls_to_bool(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? (slot->u64val != 0u) : 0;
}

i8 ls_to_i8(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->i8val : 0;
}

u8 ls_to_u8(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->u8val : 0;
}

i16 ls_to_i16(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->i16val : 0;
}

u16 ls_to_u16(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->u16val : 0;
}

i32 ls_to_i32(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->i32val : 0;
}

u32 ls_to_u32(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->u32val : 0u;
}

i64 ls_to_i64(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->i64val : 0;
}

u64 ls_to_u64(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->u64val : 0u;
}

float ls_to_f32(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->f32val : 0.0f;
}

double ls_to_f64(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->f64val : 0.0;
}

ls_string_view ls_to_string(ls_runtime* runtime, i32 index) {
	ls_string_view result;
	result.begin = NULL;
	result.end = NULL;
	ls_value* slot = runtime_stack_slot(runtime, index);
	if (!slot || !slot->cptr) return result;
	return ((ls_string_box*)slot->cptr)->value;
}

void* ls_to_ptr(ls_runtime* runtime, i32 index) {
	ls_value* slot = runtime_stack_slot(runtime, index);
	return slot ? slot->cptr : NULL;
}

ls_result ls_call(ls_runtime* runtime, ls_string_view function_name) {
	i32 function_index = -1;
	if (!runtime_find_function_by_name(runtime ? runtime->bytecode : NULL, function_name, &function_index)) {
		return LS_RESULT_FAILURE;
	}
	return runtime_execute_function(runtime, function_index) ? LS_RESULT_OK : LS_RESULT_FAILURE;
}

ls_result ls_call_index(ls_runtime* runtime, i32 function_index) {
	return runtime_execute_function(runtime, function_index) ? LS_RESULT_OK : LS_RESULT_FAILURE;
}

ls_type_kind ls_bytecode_runtime_result_kind(ls_runtime* runtime, ls_string_view function_name) {
	const ls_function_bc* fn = runtime_find_function_by_name(runtime ? runtime->bytecode : NULL, function_name, NULL);
	return fn ? fn->return_kind : LS_TYPE_INVALID;
}
