#include "bytecode.h"

 // TODO lot of silent failures here, should be handled better

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct ex_string_box {
	ex_string_view value;
} ex_string_box;

typedef struct ex_runtime_slice {
	const void* data;
	i64 length;
} ex_runtime_slice;

ex_string_view ex_arg_read_string(ex_call_frame* frame) {
	ex_runtime_slice slice = {NULL, 0};
	memcpy(&slice, frame->args, sizeof(slice));
	frame->args += sizeof(slice);
	return (ex_string_view){(const char*)slice.data, slice.length};
}

// Content equality for slice values. Floats are compared element-wise because
// NaN is not equal to itself and +0.0 equals -0.0 despite differing bits; every
// other element kind the compiler admits here is an integral scalar whose bytes
// carry no padding, so those go through memcmp.
static int runtime_slice_equal(ex_runtime_slice a, ex_runtime_slice b, u32 element_size, ex_type_kind element_kind) {
	if (a.length != b.length) return 0;
	if (a.length == 0) return 1;
	if (!a.data || !b.data) return 0;
	if (a.data == b.data) return 1;
	if (element_kind == EX_TYPE_F32) {
		for (i64 i = 0; i < a.length; ++i) {
			f32 lhs, rhs;
			memcpy(&lhs, (const u8*)a.data + i * sizeof(f32), sizeof(lhs));
			memcpy(&rhs, (const u8*)b.data + i * sizeof(f32), sizeof(rhs));
			if (!(lhs == rhs)) return 0;
		}
		return 1;
	}
	if (element_kind == EX_TYPE_F64) {
		for (i64 i = 0; i < a.length; ++i) {
			f64 lhs, rhs;
			memcpy(&lhs, (const u8*)a.data + i * sizeof(f64), sizeof(lhs));
			memcpy(&rhs, (const u8*)b.data + i * sizeof(f64), sizeof(rhs));
			if (!(lhs == rhs)) return 0;
		}
		return 1;
	}
	return memcmp(a.data, b.data, (size_t)a.length * element_size) == 0;
}

#define EX_STACK_CAPACITY_BYTES (65536u * 8u)

static const ex_function_bc* runtime_find_function_by_name(const ex_bytecode* bytecode, ex_string_view name, i32* out_index) {
	if (!bytecode) return NULL;
	for (u32 i = 0; i < bytecode->function_count; ++i) {
		const ex_string_view function_name = bytecode->functions[i].name;
		if (function_name.length != name.length) continue;
		bool equal = true;
		for (i64 j = 0; j < name.length; ++j) {
			if (function_name.begin[j] != name.begin[j]) {
				equal = false;
				break;
			}
		}
		if (equal) {
			if (out_index) *out_index = (i32)i;
			return &bytecode->functions[i];
		}
	}
	return NULL;
}

// TODO leak?
void ex_result_string(ex_runtime* runtime, ex_call_frame* frame, ex_string_view value) {
	static const char empty[] = "";
	const i64 size = value.begin ? value.length : 0;
	char* copy = (char*)runtime->arena->allocate(runtime->arena->user_data, size + 1u, 1u);
	if (!copy) return;
	if (size > 0) memcpy(copy, value.begin, (size_t)size);
	copy[size] = '\0';

	ex_string_box* box = runtime->arena && runtime->arena->allocate
		? (ex_string_box*)runtime->arena->allocate(runtime->arena->user_data, sizeof(ex_string_box), sizeof(void*))
		: (ex_string_box*)malloc(sizeof(ex_string_box));
	if (!box) return;
	box->value = (ex_string_view){size > 0 ? copy : empty, size};
	ex_runtime_slice slice = {box->value.begin, (i64)(box->value.length)};
	memcpy(frame->result, &slice, sizeof(slice));
	frame->result += sizeof(slice);
}

static int runtime_string_equals_cstr(ex_string_view value, const char* cstr) {
	for (i64 i = 0; i < value.length; ++i) {
		if (cstr[i] == '\0' || value.begin[i] != cstr[i]) return 0;
	}
	return cstr[value.length] == '\0';
}

static void runtime_native_sin_f32(ex_runtime* runtime, ex_call_frame frame) { (void)runtime; EX_ARG(frame, f32, value); EX_TYPED_RESULT(frame, f32, sinf(value)); }
static void runtime_native_cos_f32(ex_runtime* runtime, ex_call_frame frame) { (void)runtime; EX_ARG(frame, f32, value); EX_TYPED_RESULT(frame, f32, cosf(value)); }
static void runtime_native_sqrt_f32(ex_runtime* runtime, ex_call_frame frame) { (void)runtime; EX_ARG(frame, f32, value); EX_TYPED_RESULT(frame, f32, sqrtf(value)); }
static void runtime_native_sin_f64(ex_runtime* runtime, ex_call_frame frame) { (void)runtime; EX_ARG(frame, f64, value); EX_TYPED_RESULT(frame, f64, sin(value)); }
static void runtime_native_cos_f64(ex_runtime* runtime, ex_call_frame frame) { (void)runtime; EX_ARG(frame, f64, value); EX_TYPED_RESULT(frame, f64, cos(value)); }
static void runtime_native_sqrt_f64(ex_runtime* runtime, ex_call_frame frame) { (void)runtime; EX_ARG(frame, f64, value); EX_TYPED_RESULT(frame, f64, sqrt(value)); }
static void runtime_native_pow_f32(ex_runtime* runtime, ex_call_frame frame) { (void)runtime; EX_ARG(frame, f32, base); EX_ARG(frame, f32, exponent); EX_TYPED_RESULT(frame, f32, powf(base, exponent)); }
static void runtime_native_pow_f64(ex_runtime* runtime, ex_call_frame frame) { (void)runtime; EX_ARG(frame, f64, base); EX_ARG(frame, f64, exponent); EX_TYPED_RESULT(frame, f64, pow(base, exponent)); }

/* std:mem alloc(size, align) : byte[]. Allocates `size` bytes of heap memory. */
static void runtime_native_alloc(ex_runtime* runtime, ex_call_frame frame) {
	(void)runtime;
	EX_ARG(frame, i64, size);
	EX_ARG(frame, i64, align);
	(void)align;
	void* ptr = size > 0 ? malloc((size_t)size) : NULL;
	i64 actual = ptr ? size : 0;
	memcpy(frame.result, &ptr, sizeof(ptr));
	memcpy(frame.result + sizeof(ptr), &actual, sizeof(actual));
}

/* std:mem free(memory : byte[]) : void. */
static void runtime_native_free(ex_runtime* runtime, ex_call_frame frame) {
	(void)runtime;
	EX_ARG(frame, void*, ptr);
	EX_ARG(frame, i64, size);
	(void)size;
	free(ptr);
}

static void runtime_bind_builtin_callbacks(ex_runtime* runtime) {
	// TODO shouldn't we check import path?
	for (u32 i = 0; i < runtime->bytecode->function_count; ++i) {
		const ex_function_bc* fn = &runtime->bytecode->functions[i];
		if (fn->kind != EX_FUNCTION_NATIVE || !fn->is_builtin_native) continue;
		if (runtime_string_equals_cstr(fn->name, "sin")) runtime->native_callbacks[i] = &runtime_native_sin_f32;
		else if (runtime_string_equals_cstr(fn->name, "cos")) runtime->native_callbacks[i] = &runtime_native_cos_f32;
		else if (runtime_string_equals_cstr(fn->name, "sqrt")) runtime->native_callbacks[i] = &runtime_native_sqrt_f32;
		else if (runtime_string_equals_cstr(fn->name, "sin_f64")) runtime->native_callbacks[i] = &runtime_native_sin_f64;
		else if (runtime_string_equals_cstr(fn->name, "cos_f64")) runtime->native_callbacks[i] = &runtime_native_cos_f64;
		else if (runtime_string_equals_cstr(fn->name, "sqrt_f64")) runtime->native_callbacks[i] = &runtime_native_sqrt_f64;
		else if (runtime_string_equals_cstr(fn->name, "pow")) runtime->native_callbacks[i] = &runtime_native_pow_f32;
		else if (runtime_string_equals_cstr(fn->name, "pow_f64")) runtime->native_callbacks[i] = &runtime_native_pow_f64;
		else if (runtime_string_equals_cstr(fn->name, "alloc")) runtime->native_callbacks[i] = &runtime_native_alloc;
		else if (runtime_string_equals_cstr(fn->name, "free")) runtime->native_callbacks[i] = &runtime_native_free;
	}
}

// Resolves the source location active at `code_offset` (the instruction about to
// execute) in `fn`, for the debug event reported at a suspend point. Mirrors
// debugger.c's runtime_source_at, which serves the same lookup for
// ex_debug_frame_location; kept separate since the two files are independent
// translation units and this is a handful of lines.
static bool runtime_debug_frame_location(const ex_bytecode* bytecode, const ex_function_bc* fn, u32 code_offset, ex_debug_location* out_location) {
	if (!fn) return false;
	const ex_bytecode_source_map_entry* result = NULL;
	for (u32 i = 0; i < fn->source_map_count; ++i) {
		const ex_bytecode_source_map_entry* entry = &fn->source_map[i];
		if (entry->code_offset > code_offset) break;
		result = entry;
	}
	if (!result || !bytecode || result->location_index >= bytecode->location_count) return false;
	const ex_bytecode_location* loc = &bytecode->locations[result->location_index];
	out_location->source_name = bytecode->units[loc->unit_index].source_name;
	out_location->line = loc->line;
	out_location->column = loc->column;
	return true;
}

static void runtime_report_error(const ex_task* task, const ex_function_bc* function, const u8* ip, ex_string_view message) {
	if (!task->host->print) return;
	ex_debug_location location;
	if (runtime_debug_frame_location(task->bytecode, function, (u32)(ip - function->code), &location) && location.source_name.begin) {
		static const char separator[] = ": ";
		char line_buffer[10];
		char* line = line_buffer + sizeof(line_buffer);
		u32 line_number = location.line;
		do {
			*--line = (char)('0' + line_number % 10u);
			line_number /= 10u;
		} while (line_number != 0u);
		task->host->print(task->host->diagnostics_userdata, location.source_name);
		task->host->print(task->host->diagnostics_userdata, (ex_string_view){separator, 1u});
		task->host->print(task->host->diagnostics_userdata, (ex_string_view){line, line_buffer + sizeof(line_buffer) - line});
		task->host->print(task->host->diagnostics_userdata, (ex_string_view){separator + 1u, sizeof(separator) - 2u});
	}
	static const char newline[] = "\n";
	task->host->print(task->host->diagnostics_userdata, message);
	task->host->print(task->host->diagnostics_userdata, (ex_string_view){newline, sizeof(newline) - 1u});
}

static const ex_bytecode_breakpoint* runtime_find_breakpoint(const ex_bytecode* bytecode, const u8* code) {
	for (u32 i = 0; i < bytecode->breakpoint_count; ++i) {
		const ex_bytecode_breakpoint* breakpoint = &bytecode->breakpoints[i];
		if (breakpoint->code == code) return breakpoint;
	}
	return NULL;
}

// Step traps are private, short-lived patches. User breakpoints already own
// their EX_OP_BREAK bytes and are intentionally left untouched here.
static void runtime_clear_step_traps(ex_task* task) {
	for (u32 i = 0; i < task->step_trap_count; ++i) {
		const runtime_step_trap* trap = &task->step_traps[i];
		*trap->code = trap->original_byte;
	}
	task->step_trap_count = 0u;
}

static __forceinline ex_call_result runtime_enter_script_call(
	ex_task* task,
	const ex_function_bc** function,
	const u8** ip,
	const ex_function_bc* callee,
	u8* callee_frame,
	u8* caller_stack_top
) {
	if (task->call_depth >= EX_MAX_CALL_DEPTH) return EX_CALL_RESULT_CALL_DEPTH;
	u8* callee_stack_top = callee_frame + callee->frame_size;
	if (callee_stack_top > task->stack_end) return EX_CALL_RESULT_STACK_OVERFLOW;

	task->call_stack[task->call_depth] = (runtime_call_frame){ *function, *ip, task->frame, caller_stack_top };
	task->call_depth++;
	*function = callee;
	*ip = callee->code;
	task->frame = callee_frame;
	task->stack_top = callee_stack_top;
	return EX_CALL_RESULT_OK;
}

static __forceinline bool runtime_invoke_native(ex_task* task, u32 function_index, const ex_function_bc* function, u8* args, u8** result_stack_top) {
	ex_runtime* owner = task->runtime;
	if (function_index >= owner->native_callback_count) return false; // TODO can this even happen?
	ex_native_fn callback = owner->native_callbacks[function_index];
	if (!callback) {
		if (!owner->native_resolver) return false;

		const ex_native_function_desc desc = {
			function->unit_path,
			function->name,
			(int)function_index,
			function->param_size,
			function->return_size
		};
		callback = owner->native_resolver(owner, desc, owner->native_resolver_userdata);
		owner->native_callbacks[function_index] = callback;
		if (!callback) return false;
	}

	u8* stack_top = args + function->return_size;
	if (stack_top > task->stack_end) return false;

	const ex_call_frame frame = { args, args };
	callback(owner, frame);
	if (result_stack_top) *result_stack_top = stack_top;
	return true;
}

// TODO memcpy? but see codegen/benchmarks
#define runtime_read_u64() ( (ip += sizeof(u64)), *(const u64*)(ip - sizeof(u64)) )
#define runtime_read_u32() ( (ip += sizeof(u32)), *(const u32*)(ip - sizeof(u32)) )
#define runtime_read_i32() ( (ip += sizeof(i32)), *(const i32*)(ip - sizeof(i32)) )
#define runtime_read_i16() ( (ip += sizeof(i16)), (i32)*(const i16*)(ip - sizeof(i16)) )

static double runtime_numeric_to_double(const u8* value, ex_type_kind kind) {
	switch (kind) {
		case EX_TYPE_BOOL: return (value && value[0] != 0u) ? 1.0 : 0.0;
		case EX_TYPE_I8:   { i8 v = 0; memcpy(&v, value, 1); return (double)v; }
		case EX_TYPE_U8:   { u8 v = 0; memcpy(&v, value, 1); return (double)v; }
		case EX_TYPE_I16:  { i16 v = 0; memcpy(&v, value, 2); return (double)v; }
		case EX_TYPE_U16:  { u16 v = 0; memcpy(&v, value, 2); return (double)v; }
		case EX_TYPE_I32: { i32 v = 0; memcpy(&v, value, 4); return (double)v; }
		case EX_TYPE_ENUM: { u32 v = 0; memcpy(&v, value, 4); return (double)v; }
		case EX_TYPE_U32:  { u32 v = 0; memcpy(&v, value, 4); return (double)v; }
		case EX_TYPE_I64:  { i64 v = 0; memcpy(&v, value, 8); return (double)v; }
		case EX_TYPE_U64:  { u64 v = 0; memcpy(&v, value, 8); return (double)v; }
		case EX_TYPE_F32:  { f32 v = 0; memcpy(&v, value, 4); return (double)v; }
		case EX_TYPE_F64:  { f64 v = 0; memcpy(&v, value, 8); return v; }
		default:           return 0.0;
	}
}

static i64 runtime_numeric_to_i64(const u8* value, ex_type_kind kind) {
	switch (kind) {
		case EX_TYPE_BOOL: return (value && value[0] != 0u) ? 1 : 0;
		case EX_TYPE_I8:   { i8 v = 0; memcpy(&v, value, 1); return (i64)v; }
		case EX_TYPE_U8:   { u8 v = 0; memcpy(&v, value, 1); return (i64)v; }
		case EX_TYPE_I16:  { i16 v = 0; memcpy(&v, value, 2); return (i64)v; }
		case EX_TYPE_U16:  { u16 v = 0; memcpy(&v, value, 2); return (i64)v; }
		case EX_TYPE_I32: { i32 v = 0; memcpy(&v, value, 4); return (i64)v; }
		case EX_TYPE_ENUM: { u32 v = 0; memcpy(&v, value, 4); return (i64)v; }
		case EX_TYPE_U32:  { u32 v = 0; memcpy(&v, value, 4); return (i64)v; }
		case EX_TYPE_I64:  { i64 v = 0; memcpy(&v, value, 8); return v; }
		case EX_TYPE_U64:  { u64 v = 0; memcpy(&v, value, 8); return (i64)v; }
		case EX_TYPE_F32:  { f32 v = 0; memcpy(&v, value, 4); return (i64)v; }
		case EX_TYPE_F64:  { f64 v = 0; memcpy(&v, value, 8); return (i64)v; }
		default:           return 0;
	}
}

static u64 runtime_numeric_to_u64(const u8* value, ex_type_kind kind) {
	switch (kind) {
		case EX_TYPE_BOOL: return (value && value[0] != 0u) ? 1u : 0u;
		case EX_TYPE_I8:   { i8 v = 0; memcpy(&v, value, 1); return (u64)v; }
		case EX_TYPE_U8:   { u8 v = 0; memcpy(&v, value, 1); return (u64)v; }
		case EX_TYPE_I16:  { i16 v = 0; memcpy(&v, value, 2); return (u64)v; }
		case EX_TYPE_U16:  { u16 v = 0; memcpy(&v, value, 2); return (u64)v; }
		case EX_TYPE_I32: { i32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case EX_TYPE_ENUM: { u32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case EX_TYPE_U32:  { u32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case EX_TYPE_I64:  { i64 v = 0; memcpy(&v, value, 8); return (u64)v; }
		case EX_TYPE_U64:  { u64 v = 0; memcpy(&v, value, 8); return v; }
		case EX_TYPE_F32:  { f32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case EX_TYPE_F64:  { f64 v = 0; memcpy(&v, value, 8); return (u64)v; }
		default:           return 0u;
	}
}

#define EX_REG_BINOP(TYPE, EXPR)                     \
	do {                                             \
		const u32 dst__ = runtime_read_u32();        \
		const u32 lhs_offset__ = runtime_read_u32(); \
		const u32 rhs_offset__ = runtime_read_u32(); \
		TYPE a = 0;                                  \
		TYPE b = 0;                                  \
		u8* lhs__ = frame + lhs_offset__;            \
		u8* rhs__ = frame + rhs_offset__;            \
		u8* out__ = frame + dst__;                   \
		memcpy(&a, lhs__, sizeof(TYPE));             \
		memcpy(&b, rhs__, sizeof(TYPE));             \
		TYPE result__ = (TYPE)(EXPR);                \
		memcpy(out__, &result__, sizeof(TYPE));      \
	} while (0)

#define EX_REG_BINOP_IMM(TYPE, EXPR)                 \
	do {                                             \
		const u32 dst__ = runtime_read_u32();        \
		const u32 lhs_offset__ = runtime_read_u32(); \
		TYPE b = 0;                                  \
		memcpy(&b, ip, sizeof(TYPE));                \
		ip += sizeof(TYPE);                          \
		TYPE a = 0;                                  \
		u8* lhs__ = frame + lhs_offset__;            \
		u8* out__ = frame + dst__;                   \
		memcpy(&a, lhs__, sizeof(TYPE));             \
		TYPE result__ = (TYPE)(EXPR);                \
		memcpy(out__, &result__, sizeof(TYPE));      \
	} while (0)

#define EX_REG_FMA(TYPE, EXPR)                     \
	do {                                             \
		const u32 dst__ = runtime_read_u32();        \
		const u32 lhs_offset__ = runtime_read_u32(); \
		const u32 rhs_offset__ = runtime_read_u32(); \
		const u32 add_offset__ = runtime_read_u32(); \
		TYPE a = 0;                                  \
		TYPE b = 0;                                  \
		TYPE c = 0;                                  \
		memcpy(&a, frame + lhs_offset__, sizeof(TYPE)); \
		memcpy(&b, frame + rhs_offset__, sizeof(TYPE)); \
		memcpy(&c, frame + add_offset__, sizeof(TYPE)); \
		TYPE result__ = (EXPR);                      \
		memcpy(frame + dst__, &result__, sizeof(TYPE)); \
	} while (0)

#define EX_REG_DIVOP(TYPE, EXPR)                     \
	do {                                             \
		const u32 dst__ = runtime_read_u32();        \
		const u32 lhs_offset__ = runtime_read_u32(); \
		const u32 rhs_offset__ = runtime_read_u32(); \
		TYPE a = 0;                                  \
		TYPE b = 0;                                  \
		u8* lhs__ = frame + lhs_offset__;            \
		u8* rhs__ = frame + rhs_offset__;            \
		u8* out__ = frame + dst__;                   \
		memcpy(&a, lhs__, sizeof(TYPE));             \
		memcpy(&b, rhs__, sizeof(TYPE));             \
		if (!b) goto runtime_execute_function_fail;  \
		TYPE result__ = (TYPE)(EXPR);                \
		memcpy(out__, &result__, sizeof(TYPE));      \
	} while (0)

#define EX_REG_DIVOP_IMM(TYPE, EXPR)                 \
	do {                                             \
		const u32 dst__ = runtime_read_u32();        \
		const u32 lhs_offset__ = runtime_read_u32(); \
		TYPE b = 0;                                  \
		memcpy(&b, ip, sizeof(TYPE));                \
		ip += sizeof(TYPE);                          \
		TYPE a = 0;                                  \
		u8* lhs__ = frame + lhs_offset__;            \
		u8* out__ = frame + dst__;                   \
		memcpy(&a, lhs__, sizeof(TYPE));             \
		if (!b) goto runtime_execute_function_fail;  \
		TYPE result__ = (TYPE)(EXPR);                \
		memcpy(out__, &result__, sizeof(TYPE));      \
	} while (0)

#define EX_REG_NEGOP(TYPE)                         \
	do {                                           \
		const u32 dst__ = runtime_read_u32();      \
		const u32 src__ = runtime_read_u32();      \
		TYPE value__ = 0;                          \
		TYPE result__ = 0;                         \
		u8* src_ptr__ = frame + src__;             \
		u8* out__ = frame + dst__;                 \
		memcpy(&value__, src_ptr__, sizeof(TYPE)); \
		result__ = (TYPE)(0 - value__);            \
		memcpy(out__, &result__, sizeof(TYPE));    \
	} while (0)

#define EX_REG_CMP_NUMERIC(OP)                                                                                       \
	do {                                                                                                             \
		const u32 dst__ = runtime_read_u32();                                                                        \
		const u32 lhs_offset__ = runtime_read_u32();                                                                 \
		const u32 rhs_offset__ = runtime_read_u32();                                                                 \
		const ex_type_kind kind__ = (ex_type_kind) * ip++;                                                           \
		u8* lhs_ptr__ = frame + lhs_offset__;                                                                        \
		u8* rhs_ptr__ = frame + rhs_offset__;                                                                        \
		u8* out__ = frame + dst__;                                                                                   \
		int result__ = 0;                                                                                            \
		if (kind__ == EX_TYPE_F32 || kind__ == EX_TYPE_F64) {                                                        \
			result__ = runtime_numeric_to_double(lhs_ptr__, kind__) OP runtime_numeric_to_double(rhs_ptr__, kind__); \
		} else {                                                                                                     \
			result__ = runtime_numeric_to_i64(lhs_ptr__, kind__) OP runtime_numeric_to_i64(rhs_ptr__, kind__);       \
		}                                                                                                            \
		*out__ = result__ ? 1u : 0u;                                                                                 \
	} while (0)

#define EX_REG_CMP_JUMP(TYPE, OP)                            \
	do {                                                     \
		const u32 lhs_offset__ = runtime_read_u32();         \
		const u32 rhs_offset__ = runtime_read_u32();         \
		const i32 jump_offset__ = runtime_read_i16();        \
		TYPE lhs__;                                          \
		TYPE rhs__;                                          \
		memcpy(&lhs__, frame + lhs_offset__, sizeof(lhs__)); \
		memcpy(&rhs__, frame + rhs_offset__, sizeof(rhs__)); \
		if (!(lhs__ OP rhs__)) ip += jump_offset__;          \
	} while (0)

#define EX_INDEXED_OP(OP, TYPE)                                                             \
	case EX_OP_LOAD_INDEXED_##OP: {                                                         \
		const u32 dst = runtime_read_u32();                                                 \
		const u32 base_offset = runtime_read_u32();                                         \
		const u32 index_reg = runtime_read_u32();                                           \
		const u64 length = runtime_read_u64();                                              \
		const u32 element_size = runtime_read_u32();                                        \
		TYPE index = 0;                                                                     \
		memcpy(&index, frame + index_reg, sizeof(index));                                   \
		if (index >= length) goto runtime_execute_function_fail;                            \
		memcpy(frame + dst, frame + base_offset + (u64)index * element_size, element_size); \
		break;                                                                              \
	}                                                                                       \
	case EX_OP_STORE_INDEXED_##OP: {                                                        \
		const u32 base_offset = runtime_read_u32();                                         \
		const u32 index_reg = runtime_read_u32();                                           \
		const u64 length = runtime_read_u64();                                              \
		const u32 element_size = runtime_read_u32();                                        \
		const u32 src = runtime_read_u32();                                                 \
		TYPE index = 0;                                                                     \
		memcpy(&index, frame + index_reg, sizeof(index));                                   \
		if (index >= length) goto runtime_execute_function_fail;                            \
		memcpy(frame + base_offset + (u64)index * element_size, frame + src, element_size); \
		break;                                                                              \
	}

#define EX_SLICE_FIELD_OP(OP, TYPE)                                               \
	case EX_OP_SLICE_LOAD_##OP: {                                           \
		const u32 dst = runtime_read_u32();                                       \
		const u32 slice_reg = runtime_read_u32();                                 \
		const u32 index_reg = runtime_read_u32();                                 \
		const u32 element_size = runtime_read_u32();                              \
		const u32 field_offset = runtime_read_u32();                              \
		const u32 field_size = runtime_read_u32();                                \
		void* base_ptr = NULL;                                                    \
		u64 length = 0;                                                           \
		u8* slice = frame + slice_reg;                                            \
		memcpy(&base_ptr, slice, sizeof(base_ptr));                               \
		memcpy(&length, slice + sizeof(void*), sizeof(length));                   \
		TYPE index = 0;                                                           \
		memcpy(&index, frame + index_reg, sizeof(index));                         \
		if ((u64)index >= length) goto runtime_execute_function_fail;             \
		EX_ASSERT(element_size != 0);                                                \
		u8* field_ptr = (u8*)base_ptr + (u64)index * element_size + field_offset; \
		memmove(frame + dst, field_ptr, field_size);                              \
		break;                                                                    \
	}                                                                             \
	case EX_OP_SLICE_STORE_##OP: {                                          \
		const u32 slice_reg = runtime_read_u32();                                 \
		const u32 index_reg = runtime_read_u32();                                 \
		const u32 element_size = runtime_read_u32();                              \
		const u32 field_offset = runtime_read_u32();                              \
		const u32 field_size = runtime_read_u32();                                \
		const u32 src = runtime_read_u32();                                       \
		void* base_ptr = NULL;                                                    \
		u64 length = 0;                                                           \
		u8* slice = frame + slice_reg;                                            \
		memcpy(&base_ptr, slice, sizeof(base_ptr));                               \
		memcpy(&length, slice + sizeof(void*), sizeof(length));                   \
		TYPE index = 0;                                                           \
		memcpy(&index, frame + index_reg, sizeof(index));                         \
		if ((u64)index >= length) goto runtime_execute_function_fail;             \
		EX_ASSERT(element_size != 0);                                                \
		u8* field_ptr = (u8*)base_ptr + (u64)index * element_size + field_offset; \
		memmove(field_ptr, frame + src, field_size);                              \
		break;                                                                    \
	}

// Runs either a fresh call to `function` or a previously suspended
// frame. The task stack is already parked at the correct state when
// `resume_frame` is non-NULL, so fresh-call setup is skipped in that case.
static ex_call_result runtime_execute_function(ex_task* task, const ex_function_bc* fn, const runtime_call_frame* resume_frame) {
	const u8* ip;
	u8* frame = NULL;
	ex_op op = (ex_op)0;
	ex_string_view panic_message = {NULL, 0};
	bool is_panic = false;
	ex_call_result call_result = EX_CALL_RESULT_SUSPENDED;
	ex_call_result failure_result = EX_CALL_RESULT_RUNTIME_ERROR;
	// Restore point for the whole host call, retained across suspend/resume.
	runtime_restore_point* initial;
	if (resume_frame) {
		EX_ASSERT(fn == resume_frame->function);
		fn = resume_frame->function;
		ip = resume_frame->ip;
		EX_ASSERT(task->call_start_depth > 0u);
		initial = &task->call_starts[task->call_start_depth - 1];
		// The interpreter's locals (`frame` and `stack_top`) no longer exist
		// after a suspension.  Do not rely on the cached task values here:
		// native/debugger code may have changed them while the task was
		// parked.  The suspended frame is the authoritative resume state.
		task->frame = resume_frame->frame;
		task->stack_top = resume_frame->stack_top;
		frame = resume_frame->frame;
		task->is_suspended = false;

		const bool resume_from_yield = task->pause_event.reason == EX_DEBUG_PAUSE_YIELD;
		const ex_bytecode_breakpoint* breakpoint = runtime_find_breakpoint(task->bytecode, ip);
		if (breakpoint && !resume_from_yield) {
			op = (ex_op)breakpoint->original_byte;
			++ip;
			goto runtime_execute_function_dispatch;
		}
		if (breakpoint) {
			task->pause_event.reason = EX_DEBUG_PAUSE_BREAKPOINT;
			task->pause_event.message = (ex_string_view){NULL, 0};
			goto runtime_execute_function_suspend;
		}
	} else {
		EX_ASSERT(fn);

		// Set before the frame-size check below can jump to the fail label, so a
		// failure there (stack overflow at call entry, before the loop starts)
		// reports this call's own function/instruction instead of reading garbage.
		ip = fn->code;

		EX_ASSERT(task->stack_top >= task->stack + fn->param_size);
		if (task->call_start_depth >= EX_MAX_CALL_DEPTH) return EX_CALL_RESULT_CALL_DEPTH;

		initial = &task->call_starts[task->call_start_depth];
		task->call_start_depth++;
		*initial = (runtime_restore_point){ task->frame, task->stack_top, task->result_size, task->call_depth };

		u8* args = task->stack_top - fn->param_size;
		if (fn->kind == EX_FUNCTION_NATIVE) {
			task->result_size = 0u;
			u8* result_stack_top = NULL;
			if (!runtime_invoke_native(task, (u32)(fn - task->bytecode->functions), fn, args, &result_stack_top)) goto runtime_execute_function_fail;

			task->stack_top = result_stack_top;
			task->result_size = fn->return_size;
			task->frame = initial->frame;
			task->call_depth = initial->call_depth;
			--task->call_start_depth;
			return EX_CALL_RESULT_OK;
		}

		u8* frame_stack_top = args + fn->frame_size;
		if (frame_stack_top > task->stack_end) goto runtime_execute_function_fail;

		task->frame = args;
		task->stack_top = frame_stack_top;
	}

	frame = task->frame;
	for (;;) {
		op = (ex_op)*ip;
		ip++;

	runtime_execute_function_dispatch:
		switch (op) {
			// TODO const table?
			case EX_OP_LOAD_CONST_1: {
				const u32 dst = runtime_read_u32();
				u8 value = *ip++;
				u8* out = frame + dst;
				*out = value;
				break;
			}
			case EX_OP_LOAD_CONST_2: {
				const u32 dst = runtime_read_u32();
				u8* out = frame + dst;
				memcpy(out, ip, 2u);
				ip += 2u;
				break;
			}
			case EX_OP_LOAD_CONST_4: {
				const u32 dst = runtime_read_u32();
				u8* out = frame + dst;
				memcpy(out, ip, 4u);
				ip += 4u;
				break;
			}
			case EX_OP_LOAD_CONST_8: {
				const u32 dst = runtime_read_u32();
				u8* out = frame + dst;
				memcpy(out, ip, 8u);
				ip += 8u;
				break;
			}
			case EX_OP_COPY: {
				const u32 dst = runtime_read_u32();
				const u32 src_offset = runtime_read_u32();
				const u32 size = runtime_read_u32();
				u8* out = frame + dst;
				u8* src = frame + src_offset;
				memmove(out, src, size);
				break;
			}
			case EX_OP_FRAME_PTR: {
				const u32 dst = runtime_read_u32();
				const u32 offset = runtime_read_u32();
				u8* out = frame + dst;
				void* ptr = frame + offset;
				memcpy(out, &ptr, sizeof(ptr));
				break;
			}
			case EX_OP_GLOBAL_PTR: {
				const u32 dst = runtime_read_u32();
				const u32 offset = runtime_read_u32();
				void* ptr = task->globals + offset;
				memcpy(frame + dst, &ptr, sizeof(ptr));
				break;
			}
			case EX_OP_LOAD_PTR: {
				const u32 dst = runtime_read_u32();
				const u32 addr = runtime_read_u32();
				const u32 offset = runtime_read_u32();
				const u32 size = runtime_read_u32();
				void* ptr = NULL;
				memcpy(&ptr, frame + addr, sizeof(ptr));
				u8* value = (u8*)ptr;
				if (!value) goto runtime_execute_function_fail;
				memmove(frame + dst, value + offset, size);
				break;
			}
			case EX_OP_STORE_PTR: {
				const u32 addr = runtime_read_u32();
				const u32 offset = runtime_read_u32();
				const u32 src = runtime_read_u32();
				const u32 size = runtime_read_u32();
				void* ptr = NULL;
				memcpy(&ptr, frame + addr, sizeof(ptr));
				u8* value = (u8*)ptr;
				if (!value) goto runtime_execute_function_fail;
				memmove(value + offset, frame + src, size);
				break;
			}
			EX_SLICE_FIELD_OP(8, u8)
			EX_SLICE_FIELD_OP(16, u16)
			EX_SLICE_FIELD_OP(32, u32)
			EX_SLICE_FIELD_OP(64, u64)
			EX_INDEXED_OP(8, u8)
			EX_INDEXED_OP(16, u16)
			EX_INDEXED_OP(32, u32)
			EX_INDEXED_OP(64, u64)
			case EX_OP_BOUNDS_CHECK: {
				const u32 index_reg = runtime_read_u32();
				const ex_type_kind index_kind = (ex_type_kind)*ip++;
				const u64 length = runtime_read_u64();
				// Same negative-index wraparound as LOAD_INDEXED.
				u8* index_ptr = frame + index_reg;
				if (runtime_numeric_to_u64(index_ptr, index_kind) >= length) goto runtime_execute_function_fail;
				break;
			}
			case EX_OP_STRING_SLICE: {
				const u32 slice_reg = runtime_read_u32();
				const u32 index = runtime_read_u32();
				ex_string_view str = task->bytecode->strings[index];
				u8* slice = frame + slice_reg;
				memcpy(slice, &str.begin, sizeof(str.begin));
				i64 len = str.length;
				memcpy(slice + sizeof(void*), &len, 8u);
				break;
			}
			case EX_OP_SLICE: {
				const u32 dst_reg = runtime_read_u32();
				const u32 source_reg = runtime_read_u32();
				const u32 begin_reg = runtime_read_u32();
				const u32 end_reg = runtime_read_u32();
				const u32 element_size = runtime_read_u32();
				i64 end = 0, begin = 0, length = 0;
				void* base_ptr = NULL;
				u8* source = frame + source_reg;
				u8* begin_ptr = frame + begin_reg;
				u8* end_ptr = frame + end_reg;
				u8* out = frame + dst_reg;
				memcpy(&base_ptr, source, sizeof(base_ptr));
				memcpy(&length, source + sizeof(void*), 8u);
				memcpy(&begin, begin_ptr, 8u);
				memcpy(&end, end_ptr, 8u);
				u8* base = (u8*)base_ptr;
				if (begin < 0 || end < begin || (u64)end > (u64)length) goto runtime_execute_function_fail;
				const u64 offset = (u64)begin * element_size;
				void* new_base = base ? base + offset : NULL;
				const i64 new_length = end - begin;
				memcpy(out, &new_base, sizeof(new_base));
				memcpy(out + sizeof(void*), &new_length, 8u);
				break;
			}
			case EX_OP_SLICE_REF: {
				const u32 dst = runtime_read_u32();
				const u32 slice_reg = runtime_read_u32();
				const u32 index_reg = runtime_read_u32();
				const ex_type_kind index_kind = (ex_type_kind)*ip++;
				const u32 element_size = runtime_read_u32();
				i64 length = 0;
				void* base_ptr = NULL;
				u8* slice = frame + slice_reg;
				u8* out = frame + dst;
				memcpy(&base_ptr, slice, sizeof(base_ptr));
				memcpy(&length, slice + sizeof(void*), 8u);
				const u64 index = runtime_numeric_to_u64(frame + index_reg, index_kind);
				u8* base = (u8*)base_ptr;
				if (!base || index >= (u64)length) goto runtime_execute_function_fail;
				const u64 offset = index * element_size;
				void* ref = base + offset;
				memcpy(out, &ref, sizeof(ref));
				break;
			}
			case EX_OP_SLICE_LENGTH: {
				const u32 dst = runtime_read_u32();
				const u32 slice_reg = runtime_read_u32();
				i64 length = 0;
				u8* slice = frame + slice_reg;
				u8* out = frame + dst;
				memcpy(&length, slice + sizeof(void*), 8u);
				memcpy(out, &length, 8u);
				break;
			}
			case EX_OP_SLICE_EQ: {
				const u32 dst = runtime_read_u32();
				const u32 lhs_reg = runtime_read_u32();
				const u32 rhs_reg = runtime_read_u32();
				const u32 element_size = runtime_read_u32();
				const ex_type_kind element_kind = (ex_type_kind)*ip++;
				ex_runtime_slice lhs = {NULL, 0};
				ex_runtime_slice rhs = {NULL, 0};
				u8* out = frame + dst;
				memcpy(&lhs, frame + lhs_reg, sizeof(lhs));
				memcpy(&rhs, frame + rhs_reg, sizeof(rhs));
				*out = (u8)(runtime_slice_equal(lhs, rhs, element_size, element_kind) ? 1u : 0u);
				break;
			}
			case EX_OP_PANIC: {
				const u32 message_reg = runtime_read_u32();
				ex_runtime_slice message = {NULL, 0};
				memcpy(&message, frame + message_reg, sizeof(message));
				panic_message.begin = (const char*)message.data;
				panic_message.length = message.length > 0 ? (u64)message.length : 0;
				is_panic = true;
				goto runtime_execute_function_fail;
			}
			case EX_OP_RETURN: {
				const u32 src = runtime_read_u32();
				const u32 size = runtime_read_u32();
				if (size > 0u) {
					u8* src_ptr = frame + src;
					memmove(frame, src_ptr, size);
					task->stack_top = frame + size;
				}
				else {
					task->stack_top = frame;
				}
				if (task->call_depth == initial->call_depth) {
					runtime_clear_step_traps(task);
					task->step_action = EX_DEBUG_CONTINUE;
						task->result_size = size;
						task->frame = initial->frame;
						--task->call_start_depth;
						return EX_CALL_RESULT_OK;
				}
				const runtime_call_frame caller = task->call_stack[--task->call_depth];
				fn = caller.function;
				ip = caller.ip;
				task->frame = caller.frame;
				frame = caller.frame;
				task->stack_top = caller.stack_top;
				break;
			}
			case EX_OP_CALL_DIRECT: {
				const u32 callee_index = runtime_read_u32();
				const u32 arg_base = runtime_read_u32();
				u8* caller_top = task->stack_top;
				const ex_function_bc* callee = &task->bytecode->functions[callee_index];

				ex_call_result enter_result = runtime_enter_script_call(task, &fn, &ip, callee, frame + arg_base, caller_top);
				if (enter_result != EX_CALL_RESULT_OK) {
					failure_result = enter_result;
					goto runtime_execute_function_fail;
				}
				frame = task->frame;
				break;
			}
			case EX_OP_CALL_NATIVE: {
				const u32 callee_index = runtime_read_u32();
				const u32 arg_base = runtime_read_u32();
				u8* caller_top = task->stack_top;
				const ex_function_bc* callee = &task->bytecode->functions[callee_index];
				task->frame = frame;
				if (!runtime_invoke_native(task, callee_index, callee, frame + arg_base, NULL)) goto runtime_execute_function_fail;
				frame = task->frame;
				task->stack_top = caller_top;
				break;
			}
			case EX_OP_CALL_INDIRECT: {
				const u32 dst = runtime_read_u32();
				const u32 arg_size = runtime_read_u32();
				const u32 return_size = runtime_read_u32();
				(void)return_size;
				const u32 arg = dst + 4u;
				u8* callee_ptr = frame + dst;
				u32 callee_index = 0;
				memcpy(&callee_index, callee_ptr, sizeof(callee_index));
				u8* caller_top = task->stack_top;
				if (callee_index >= task->bytecode->function_count) goto runtime_execute_function_fail;
				const ex_function_bc* callee = &task->bytecode->functions[callee_index];

				// The function-value slot at dst is no longer needed once
				// callee_index is read out of it above; shift the argument
				// bytes down over it so the callee's params sit at frame
				// offset 0 relative to `dst`, exactly like CALL_DIRECT's
				// arg_base. This is what lets the rest of this case be a
				// straight copy of CALL_DIRECT's push-onto-call_stack path
				// below, with zero special-casing needed in EX_OP_RETURN: the
				// callee's own RETURN deposits its result at its frame
				// (== dst), which is exactly where the caller expects it.
				if (arg_size > 0u) memmove(frame + dst, frame + arg, arg_size);

				if (callee->kind == EX_FUNCTION_NATIVE) {
					task->frame = frame;
					if (!runtime_invoke_native(task, callee_index, callee, frame + dst, NULL)) goto runtime_execute_function_fail;
					frame = task->frame;
					task->stack_top = caller_top;
					break;
				}

				ex_call_result enter_result = runtime_enter_script_call(task, &fn, &ip, callee, frame + dst, caller_top);
				if (enter_result != EX_CALL_RESULT_OK) {
					failure_result = enter_result;
					goto runtime_execute_function_fail;
				}
				frame = task->frame;
				break;
			}
			case EX_OP_CAST: {
				const u32 dst = runtime_read_u32();
				const u32 src = runtime_read_u32();
				const ex_type_kind src_kind = (ex_type_kind)*ip++;
				const ex_type_kind dst_kind = (ex_type_kind)*ip++;
				u8* src_ptr = frame + src;
				u8* out = frame + dst;
				switch (dst_kind) {
					case EX_TYPE_BOOL: { u8 v = runtime_numeric_to_u64(src_ptr, src_kind) != 0u ? 1u : 0u; memcpy(out, &v, 1u); break; }
					case EX_TYPE_I8:  { i8 v = (i8)runtime_numeric_to_i64(src_ptr, src_kind); memcpy(out, &v, 1u); break; }
					case EX_TYPE_U8:  { u8 v = (u8)runtime_numeric_to_u64(src_ptr, src_kind); memcpy(out, &v, 1u); break; }
					case EX_TYPE_I16: { i16 v = (i16)runtime_numeric_to_i64(src_ptr, src_kind); memcpy(out, &v, 2u); break; }
					case EX_TYPE_U16: { u16 v = (u16)runtime_numeric_to_u64(src_ptr, src_kind); memcpy(out, &v, 2u); break; }
					case EX_TYPE_I32:
					case EX_TYPE_ENUM: { i32 v = (i32)runtime_numeric_to_i64(src_ptr, src_kind); memcpy(out, &v, 4u); break; }
					case EX_TYPE_U32: { u32 v = (u32)runtime_numeric_to_u64(src_ptr, src_kind); memcpy(out, &v, 4u); break; }
					case EX_TYPE_I64: { i64 v = runtime_numeric_to_i64(src_ptr, src_kind); memcpy(out, &v, 8u); break; }
					case EX_TYPE_U64: { u64 v = runtime_numeric_to_u64(src_ptr, src_kind); memcpy(out, &v, 8u); break; }
					case EX_TYPE_F32: { f32 v = (f32)runtime_numeric_to_double(src_ptr, src_kind); memcpy(out, &v, 4u); break; }
					case EX_TYPE_F64: { f64 v = runtime_numeric_to_double(src_ptr, src_kind); memcpy(out, &v, 8u); break; }
					default: break; // should not happen
				}
				break;
			}
			case EX_OP_NEG_I8: EX_REG_NEGOP(i8); break;
			case EX_OP_NEG_U8: EX_REG_NEGOP(u8); break;
			case EX_OP_NEG_I16: EX_REG_NEGOP(i16); break;
			case EX_OP_NEG_U16: EX_REG_NEGOP(u16); break;
			case EX_OP_NEG_I32: EX_REG_NEGOP(i32); break;
			case EX_OP_NEG_U32: EX_REG_NEGOP(u32); break;
			case EX_OP_NEG_I64: EX_REG_NEGOP(i64); break;
			case EX_OP_NEG_U64: EX_REG_NEGOP(u64); break;
			case EX_OP_NEG_F32: EX_REG_NEGOP(f32); break;
			case EX_OP_NEG_F64: EX_REG_NEGOP(f64); break;
			case EX_OP_NOT: {
				const u32 dst = runtime_read_u32();
				const u32 src = runtime_read_u32();
				u8* src_ptr = frame + src;
				u8* out = frame + dst;
				*out = *src_ptr ? 0u : 1u;
				break;
			}
			#define EX_ARITH_BIN_CASES(OP, EXPR) \
				case EX_OP_##OP##_I8: EX_REG_BINOP(i8, a EXPR b); break; \
				case EX_OP_##OP##_U8: EX_REG_BINOP(u8, a EXPR b); break; \
				case EX_OP_##OP##_I16: EX_REG_BINOP(i16, a EXPR b); break; \
				case EX_OP_##OP##_U16: EX_REG_BINOP(u16, a EXPR b); break; \
				case EX_OP_##OP##_I32: EX_REG_BINOP(i32, a EXPR b); break; \
				case EX_OP_##OP##_U32: EX_REG_BINOP(u32, a EXPR b); break; \
				case EX_OP_##OP##_I64: EX_REG_BINOP(i64, a EXPR b); break; \
				case EX_OP_##OP##_U64: EX_REG_BINOP(u64, a EXPR b); break; \
				case EX_OP_##OP##_F32: EX_REG_BINOP(f32, a EXPR b); break; \
				case EX_OP_##OP##_F64: EX_REG_BINOP(f64, a EXPR b); break; \
				case EX_OP_##OP##_I8_IMM: EX_REG_BINOP_IMM(i8, a EXPR b); break; \
				case EX_OP_##OP##_U8_IMM: EX_REG_BINOP_IMM(u8, a EXPR b); break; \
				case EX_OP_##OP##_I16_IMM: EX_REG_BINOP_IMM(i16, a EXPR b); break; \
				case EX_OP_##OP##_U16_IMM: EX_REG_BINOP_IMM(u16, a EXPR b); break; \
				case EX_OP_##OP##_I32_IMM: EX_REG_BINOP_IMM(i32, a EXPR b); break; \
				case EX_OP_##OP##_U32_IMM: EX_REG_BINOP_IMM(u32, a EXPR b); break; \
				case EX_OP_##OP##_I64_IMM: EX_REG_BINOP_IMM(i64, a EXPR b); break; \
				case EX_OP_##OP##_U64_IMM: EX_REG_BINOP_IMM(u64, a EXPR b); break; \
				case EX_OP_##OP##_F32_IMM: EX_REG_BINOP_IMM(f32, a EXPR b); break; \
				case EX_OP_##OP##_F64_IMM: EX_REG_BINOP_IMM(f64, a EXPR b); break;
			#define EX_ARITH_INT_CASES(OP, EXPR) \
				case EX_OP_##OP##_8: EX_REG_BINOP(u8, a EXPR b); break; \
				case EX_OP_##OP##_16: EX_REG_BINOP(u16, a EXPR b); break; \
				case EX_OP_##OP##_32: EX_REG_BINOP(u32, a EXPR b); break; \
				case EX_OP_##OP##_64: EX_REG_BINOP(u64, a EXPR b); break; \
				case EX_OP_##OP##_8_IMM: EX_REG_BINOP_IMM(u8, a EXPR b); break; \
				case EX_OP_##OP##_16_IMM: EX_REG_BINOP_IMM(u16, a EXPR b); break; \
				case EX_OP_##OP##_32_IMM: EX_REG_BINOP_IMM(u32, a EXPR b); break; \
				case EX_OP_##OP##_64_IMM: EX_REG_BINOP_IMM(u64, a EXPR b); break; \
				case EX_OP_##OP##_F32: EX_REG_BINOP(f32, a EXPR b); break; \
				case EX_OP_##OP##_F64: EX_REG_BINOP(f64, a EXPR b); break; \
				case EX_OP_##OP##_F32_IMM: EX_REG_BINOP_IMM(f32, a EXPR b); break; \
				case EX_OP_##OP##_F64_IMM: EX_REG_BINOP_IMM(f64, a EXPR b); break;
			#define EX_ARITH_DIV_CASES(OP, EXPR) \
				case EX_OP_##OP##_I8: EX_REG_DIVOP(i8, a EXPR b); break; \
				case EX_OP_##OP##_U8: EX_REG_DIVOP(u8, a EXPR b); break; \
				case EX_OP_##OP##_I16: EX_REG_DIVOP(i16, a EXPR b); break; \
				case EX_OP_##OP##_U16: EX_REG_DIVOP(u16, a EXPR b); break; \
				case EX_OP_##OP##_I32: EX_REG_DIVOP(i32, a EXPR b); break; \
				case EX_OP_##OP##_U32: EX_REG_DIVOP(u32, a EXPR b); break; \
				case EX_OP_##OP##_I64: EX_REG_DIVOP(i64, a EXPR b); break; \
				case EX_OP_##OP##_U64: EX_REG_DIVOP(u64, a EXPR b); break; \
				case EX_OP_##OP##_F32: EX_REG_DIVOP(f32, a EXPR b); break; \
				case EX_OP_##OP##_F64: EX_REG_DIVOP(f64, a EXPR b); break; \
				case EX_OP_##OP##_I8_IMM: EX_REG_DIVOP_IMM(i8, a EXPR b); break; \
				case EX_OP_##OP##_U8_IMM: EX_REG_DIVOP_IMM(u8, a EXPR b); break; \
				case EX_OP_##OP##_I16_IMM: EX_REG_DIVOP_IMM(i16, a EXPR b); break; \
				case EX_OP_##OP##_U16_IMM: EX_REG_DIVOP_IMM(u16, a EXPR b); break; \
				case EX_OP_##OP##_I32_IMM: EX_REG_DIVOP_IMM(i32, a EXPR b); break; \
				case EX_OP_##OP##_U32_IMM: EX_REG_DIVOP_IMM(u32, a EXPR b); break; \
				case EX_OP_##OP##_I64_IMM: EX_REG_DIVOP_IMM(i64, a EXPR b); break; \
				case EX_OP_##OP##_U64_IMM: EX_REG_DIVOP_IMM(u64, a EXPR b); break; \
				case EX_OP_##OP##_F32_IMM: EX_REG_DIVOP_IMM(f32, a EXPR b); break; \
				case EX_OP_##OP##_F64_IMM: EX_REG_DIVOP_IMM(f64, a EXPR b); break;
			#define EX_MOD_CASES \
				case EX_OP_MOD_I8: EX_REG_DIVOP(i8, a % b); break; case EX_OP_MOD_I8_IMM: EX_REG_DIVOP_IMM(i8, a % b); break; \
				case EX_OP_MOD_U8: EX_REG_DIVOP(u8, a % b); break; case EX_OP_MOD_U8_IMM: EX_REG_DIVOP_IMM(u8, a % b); break; \
				case EX_OP_MOD_I16: EX_REG_DIVOP(i16, a % b); break; case EX_OP_MOD_I16_IMM: EX_REG_DIVOP_IMM(i16, a % b); break; \
				case EX_OP_MOD_U16: EX_REG_DIVOP(u16, a % b); break; case EX_OP_MOD_U16_IMM: EX_REG_DIVOP_IMM(u16, a % b); break; \
				case EX_OP_MOD_I32: EX_REG_DIVOP(i32, a % b); break; case EX_OP_MOD_I32_IMM: EX_REG_DIVOP_IMM(i32, a % b); break; \
				case EX_OP_MOD_U32: EX_REG_DIVOP(u32, a % b); break; case EX_OP_MOD_U32_IMM: EX_REG_DIVOP_IMM(u32, a % b); break; \
				case EX_OP_MOD_I64: EX_REG_DIVOP(i64, a % b); break; case EX_OP_MOD_I64_IMM: EX_REG_DIVOP_IMM(i64, a % b); break; \
				case EX_OP_MOD_U64: EX_REG_DIVOP(u64, a % b); break; case EX_OP_MOD_U64_IMM: EX_REG_DIVOP_IMM(u64, a % b); break;
			EX_ARITH_INT_CASES(ADD, +)
			EX_ARITH_INT_CASES(SUB, -)
			EX_ARITH_INT_CASES(MUL, *)
			EX_ARITH_DIV_CASES(DIV, /)
			EX_MOD_CASES
			case EX_OP_MADD_F32: EX_REG_FMA(f32, fmaf(a, b, c)); break;
			case EX_OP_MADD_F64: EX_REG_FMA(f64, fma(a, b, c)); break;
			case EX_OP_MSUB_F32: EX_REG_FMA(f32, fmaf(a, b, -c)); break;
			case EX_OP_MSUB_F64: EX_REG_FMA(f64, fma(a, b, -c)); break;
			case EX_OP_NMADD_F32: EX_REG_FMA(f32, fmaf(-a, b, c)); break;
			case EX_OP_NMADD_F64: EX_REG_FMA(f64, fma(-a, b, c)); break;
			case EX_OP_NMSUB_F32: EX_REG_FMA(f32, fmaf(-a, b, -c)); break;
			case EX_OP_NMSUB_F64: EX_REG_FMA(f64, fma(-a, b, -c)); break;
			#undef EX_MOD_CASES
			#undef EX_ARITH_DIV_CASES
			#undef EX_ARITH_INT_CASES
			#undef EX_ARITH_BIN_CASES
			case EX_OP_INC_I32: {
				const u32 dst = runtime_read_u32();
				i32 value = 0;
				memcpy(&value, frame + dst, 4u);
				value = (i32)((u32)value + 1u);
				memcpy(frame + dst, &value, 4u);
				break;
			}
			case EX_OP_INC_I64: {
				const u32 dst = runtime_read_u32();
				i64 value = 0;
				memcpy(&value, frame + dst, 8u);
				value = (i64)((u64)value + 1u);
				memcpy(frame + dst, &value, 8u);
				break;
			}
			case EX_OP_DEC_I32: {
				const u32 dst = runtime_read_u32();
				i32 value = 0;
				memcpy(&value, frame + dst, 4u);
				value = (i32)((u32)value - 1u);
				memcpy(frame + dst, &value, 4u);
				break;
			}
			case EX_OP_DEC_I64: {
				const u32 dst = runtime_read_u32();
				i64 value = 0;
				memcpy(&value, frame + dst, 8u);
				value = (i64)((u64)value - 1u);
				memcpy(frame + dst, &value, 8u);
				break;
			}
			case EX_OP_EQ: EX_REG_CMP_NUMERIC(==); break;
			case EX_OP_NE: EX_REG_CMP_NUMERIC(!=); break;
			case EX_OP_LT: EX_REG_CMP_NUMERIC(<); break;
			case EX_OP_LE: EX_REG_CMP_NUMERIC(<=); break;
			case EX_OP_GT: EX_REG_CMP_NUMERIC(>); break;
			case EX_OP_GE: EX_REG_CMP_NUMERIC(>=); break;
			case EX_OP_JE_I8: EX_REG_CMP_JUMP(i8, !=); break;
			case EX_OP_JGE_I8: EX_REG_CMP_JUMP(i8, <); break;
			case EX_OP_JGT_I8: EX_REG_CMP_JUMP(i8, <=); break;
			case EX_OP_JLT_I8: EX_REG_CMP_JUMP(i8, >=); break;
			case EX_OP_JLE_I8: EX_REG_CMP_JUMP(i8, >); break;
			case EX_OP_JE_U8: EX_REG_CMP_JUMP(u8, !=); break;
			case EX_OP_JGE_U8: EX_REG_CMP_JUMP(u8, <); break;
			case EX_OP_JGT_U8: EX_REG_CMP_JUMP(u8, <=); break;
			case EX_OP_JLT_U8: EX_REG_CMP_JUMP(u8, >=); break;
			case EX_OP_JLE_U8: EX_REG_CMP_JUMP(u8, >); break;
			case EX_OP_JE_I16: EX_REG_CMP_JUMP(i16, !=); break;
			case EX_OP_JGE_I16: EX_REG_CMP_JUMP(i16, <); break;
			case EX_OP_JGT_I16: EX_REG_CMP_JUMP(i16, <=); break;
			case EX_OP_JLT_I16: EX_REG_CMP_JUMP(i16, >=); break;
			case EX_OP_JLE_I16: EX_REG_CMP_JUMP(i16, >); break;
			case EX_OP_JE_U16: EX_REG_CMP_JUMP(u16, !=); break;
			case EX_OP_JGE_U16: EX_REG_CMP_JUMP(u16, <); break;
			case EX_OP_JGT_U16: EX_REG_CMP_JUMP(u16, <=); break;
			case EX_OP_JLT_U16: EX_REG_CMP_JUMP(u16, >=); break;
			case EX_OP_JLE_U16: EX_REG_CMP_JUMP(u16, >); break;
			case EX_OP_JE_I32: EX_REG_CMP_JUMP(i32, !=); break;
			case EX_OP_JGE_I32: EX_REG_CMP_JUMP(i32, <); break;
			case EX_OP_JGT_I32: EX_REG_CMP_JUMP(i32, <=); break;
			case EX_OP_JLT_I32: EX_REG_CMP_JUMP(i32, >=); break;
			case EX_OP_JLE_I32: EX_REG_CMP_JUMP(i32, >); break;
			case EX_OP_JE_U32: EX_REG_CMP_JUMP(u32, !=); break;
			case EX_OP_JGE_U32: EX_REG_CMP_JUMP(u32, <); break;
			case EX_OP_JGT_U32: EX_REG_CMP_JUMP(u32, <=); break;
			case EX_OP_JLT_U32: EX_REG_CMP_JUMP(u32, >=); break;
			case EX_OP_JLE_U32: EX_REG_CMP_JUMP(u32, >); break;
			case EX_OP_JE_I64: EX_REG_CMP_JUMP(i64, !=); break;
			case EX_OP_JGE_I64: EX_REG_CMP_JUMP(i64, <); break;
			case EX_OP_JGT_I64: EX_REG_CMP_JUMP(i64, <=); break;
			case EX_OP_JLT_I64: EX_REG_CMP_JUMP(i64, >=); break;
			case EX_OP_JLE_I64: EX_REG_CMP_JUMP(i64, >); break;
			case EX_OP_JE_U64: EX_REG_CMP_JUMP(u64, !=); break;
			case EX_OP_JGE_U64: EX_REG_CMP_JUMP(u64, <); break;
			case EX_OP_JGT_U64: EX_REG_CMP_JUMP(u64, <=); break;
			case EX_OP_JLT_U64: EX_REG_CMP_JUMP(u64, >=); break;
			case EX_OP_JLE_U64: EX_REG_CMP_JUMP(u64, >); break;
			case EX_OP_JE_F32: EX_REG_CMP_JUMP(f32, !=); break;
			case EX_OP_JGE_F32: EX_REG_CMP_JUMP(f32, <); break;
			case EX_OP_JGT_F32: EX_REG_CMP_JUMP(f32, <=); break;
			case EX_OP_JLT_F32: EX_REG_CMP_JUMP(f32, >=); break;
			case EX_OP_JLE_F32: EX_REG_CMP_JUMP(f32, >); break;
			case EX_OP_JE_F64: EX_REG_CMP_JUMP(f64, !=); break;
			case EX_OP_JGE_F64: EX_REG_CMP_JUMP(f64, <); break;
			case EX_OP_JGT_F64: EX_REG_CMP_JUMP(f64, <=); break;
			case EX_OP_JLT_F64: EX_REG_CMP_JUMP(f64, >=); break;
			case EX_OP_JLE_F64: EX_REG_CMP_JUMP(f64, >); break;
			case EX_OP_JNE_I8: EX_REG_CMP_JUMP(i8, ==); break;
			case EX_OP_JNE_U8: EX_REG_CMP_JUMP(u8, ==); break;
			case EX_OP_JNE_I16: EX_REG_CMP_JUMP(i16, ==); break;
			case EX_OP_JNE_U16: EX_REG_CMP_JUMP(u16, ==); break;
			case EX_OP_JNE_I32: EX_REG_CMP_JUMP(i32, ==); break;
			case EX_OP_JNE_U32: EX_REG_CMP_JUMP(u32, ==); break;
			case EX_OP_JNE_I64: EX_REG_CMP_JUMP(i64, ==); break;
			case EX_OP_JNE_U64: EX_REG_CMP_JUMP(u64, ==); break;
			case EX_OP_JNE_F32: EX_REG_CMP_JUMP(f32, ==); break;
			case EX_OP_JNE_F64: EX_REG_CMP_JUMP(f64, ==); break;
			case EX_OP_JUMP: {
				const i32 offset = runtime_read_i16();
				ip += offset;
				break;
			}
			case EX_OP_JZ_U8: {
				const u32 cond = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				u8* ptr = frame + cond;
				if (*ptr == 0u) ip += offset;
				break;
			}
			case EX_OP_JNZ_U8: {
				const u32 cond = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				u8* ptr = frame + cond;
				if (*ptr != 0u) ip += offset;
				break;
			}
			case EX_OP_JZ_I32: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i32 value = 0;
				memcpy(&value, frame + reg, 4u);
				if (value == 0) ip += offset;
				break;
			}
			case EX_OP_JZ_I64: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i64 value = 0;
				memcpy(&value, frame + reg, 8u);
				if (value == 0) ip += offset;
				break;
			}
			case EX_OP_JGZ_I32: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i32 value = 0;
				memcpy(&value, frame + reg, 4u);
				if (value > 0) ip += offset;
				break;
			}
			case EX_OP_JGZ_I64: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i64 value = 0;
				memcpy(&value, frame + reg, 8u);
				if (value > 0) ip += offset;
				break;
			}
			case EX_OP_RETURN_BASE: {
				if (task->call_depth == initial->call_depth) {
					const u32 size = fn->return_size;
					runtime_clear_step_traps(task);
					task->step_action = EX_DEBUG_CONTINUE;
					task->stack_top = frame + size;
					task->result_size = size;
					task->frame = initial->frame;
					--task->call_start_depth;
					return EX_CALL_RESULT_OK;
				}

				task->call_depth--;
				const runtime_call_frame caller = task->call_stack[task->call_depth];
				fn = caller.function;
				ip = caller.ip;
				task->frame = caller.frame;
				frame = caller.frame;
				task->stack_top = caller.stack_top;
				break;
			}
			case EX_OP_JGEZ_I32: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i32 value = 0;
				memcpy(&value, frame + reg, 4u);
				if (value >= 0) ip += offset;
				break;
			}
			case EX_OP_JGEZ_I64: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i64 value = 0;
				memcpy(&value, frame + reg, 8u);
				if (value >= 0) ip += offset;
				break;
			}
			case EX_OP_JLTZ_I32: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i32 value = 0;
				memcpy(&value, frame + reg, 4u);
				if (value < 0) ip += offset;
				break;
			}
			case EX_OP_JLTZ_I64: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i64 value = 0;
				memcpy(&value, frame + reg, 8u);
				if (value < 0) ip += offset;
				break;
			}
			case EX_OP_JLEZ_I32: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i32 value = 0;
				memcpy(&value, frame + reg, 4u);
				if (value <= 0) ip += offset;
				break;
			}
			case EX_OP_JLEZ_I64: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i64 value = 0;
				memcpy(&value, frame + reg, 8u);
				if (value <= 0) ip += offset;
				break;
			}
			case EX_OP_JNZ_I32: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i32 value = 0;
				memcpy(&value, frame + reg, 4u);
				if (value != 0) ip += offset;
				break;
			}
			case EX_OP_JNZ_I64: {
				const u32 reg = runtime_read_u32();
				const i32 offset = runtime_read_i16();
				i64 value = 0;
				memcpy(&value, frame + reg, 8u);
				if (value != 0) ip += offset;
				break;
			}
			case EX_OP_YIELD: {
				task->suspended_yield_destination = runtime_read_u32();
				task->suspended_yield_type = runtime_read_u32();
				task->suspended_yield_size = runtime_read_u32();
				task->pause_event.reason = EX_DEBUG_PAUSE_YIELD;
				task->pause_event.message = (ex_string_view){NULL, 0};
				goto runtime_execute_function_suspend;
			}
			case EX_OP_BREAK: {
				// ip already points one byte past EX_OP_BREAK. Rewind it before a
				// suspend so resuming re-executes the trapped original opcode.
				const u8* code = ip - 1u;
				const u32 code_offset = (u32)(code - fn->code);
				const runtime_step_trap* step_trap = NULL;
				for (u32 i = 0; i < task->step_trap_count; ++i) {
					const runtime_step_trap* candidate = &task->step_traps[i];
					if (candidate->code == code) {
						step_trap = candidate;
						break;
					}
				}
				const ex_bytecode_breakpoint* bp = runtime_find_breakpoint(task->bytecode, code);
				if (!step_trap && !bp) goto runtime_execute_function_fail;
				--ip;
				if (step_trap) {
					ex_debug_location location;
					bool pause_for_step = runtime_debug_frame_location(task->bytecode, fn, code_offset, &location);
					if (pause_for_step && location.line == task->step_start_line && task->call_depth == task->step_start_call_depth) pause_for_step = false;
					if (pause_for_step && task->step_action == EX_DEBUG_STEP_OVER) pause_for_step = task->call_depth <= task->step_start_call_depth;
					else if (pause_for_step && task->step_action != EX_DEBUG_STEP_INTO) pause_for_step = task->call_depth < task->step_start_call_depth;
					if (pause_for_step) {
						task->pause_event.reason = EX_DEBUG_PAUSE_STEP;
						task->pause_event.message = (ex_string_view){NULL, 0};
						goto runtime_execute_function_suspend;
					}
				}

				if (bp) {
					task->pause_event.reason = EX_DEBUG_PAUSE_BREAKPOINT;
					task->pause_event.message = (ex_string_view){NULL, 0};
					goto runtime_execute_function_suspend;
				}

				op = (ex_op)(step_trap ? step_trap->original_byte : bp->original_byte);
				++ip;
				goto runtime_execute_function_dispatch;
			}
			default:
				goto runtime_execute_function_fail;
		}
	}

runtime_execute_function_fail:
	runtime_clear_step_traps(task);
	// The dispatch loop advances ip while decoding operands. On this rare path,
	// recover the preceding emitted opcode from the source map instead of
	// maintaining a second instruction pointer on every successful dispatch.
	{
		const u32 code_offset = (u32)(ip - fn->code);
		const ex_bytecode_source_map_entry* entry = NULL;
		for (u32 i = 0; i < fn->source_map_count; ++i) {
			if (fn->source_map[i].code_offset >= code_offset) break;
			entry = &fn->source_map[i];
		}
		ip = entry ? fn->code + entry->code_offset : fn->code;
	}
	call_result = 	is_panic 													? EX_CALL_RESULT_PANIC
					: op >= EX_OP_DIV_I8 && op <= EX_OP_DIV_F64 				? EX_CALL_RESULT_DIVISION_BY_ZERO
					: op >= EX_OP_DIV_I8_IMM && op <= EX_OP_DIV_F64_IMM 		? EX_CALL_RESULT_DIVISION_BY_ZERO
					: op >= EX_OP_MOD_I8 && op <= EX_OP_MOD_U64 				? EX_CALL_RESULT_MODULO_BY_ZERO
					: op >= EX_OP_MOD_I8_IMM && op <= EX_OP_MOD_U64_IMM			? EX_CALL_RESULT_MODULO_BY_ZERO
					: op >= EX_OP_LOAD_INDEXED_8 && op <= EX_OP_SLICE_STORE_64	? EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS
					: op == EX_OP_CALL_DIRECT									? EX_CALL_RESULT_INVALID_FUNCTION_CALL
					: op == EX_OP_CALL_NATIVE									? EX_CALL_RESULT_INVALID_FUNCTION_CALL
					: op == EX_OP_CALL_INDIRECT 								? EX_CALL_RESULT_INVALID_FUNCTION_CALL
					: EX_CALL_RESULT_RUNTIME_ERROR;
	if (failure_result != EX_CALL_RESULT_RUNTIME_ERROR) call_result = failure_result;
	if (is_panic) runtime_report_error(task, fn, ip, panic_message);
	// A task error suspends using the same reified frame as EX_OP_BREAK;
	// state is left exactly as-is instead of being unwound.
	task->pause_event.reason = EX_DEBUG_PAUSE_ERROR;
	task->pause_event.message = is_panic ? panic_message : (ex_string_view){NULL, 0};

runtime_execute_function_suspend:
	runtime_clear_step_traps(task);
	// A completed STEP_OUT reports the call site the stepped-out frame
	// returned into rather than the first statement boundary reached after the
	// return (the trap this pause fires on). The caller's frame on
	// call_stack[call_depth] was saved with ip as the return address just past
	// its CALL_DIRECT (the same convention debugger.c's
	// debug_frame_lookup_offset relies on), so ip - 1 resolves back to the
	// call statement. The suspension itself stays parked at the real resume
	// point (the boundary trap) so a CONTINUE re-executes correctly.
	const int step_out_completed =
		task->step_action == EX_DEBUG_STEP_OUT &&
		task->pause_event.reason == EX_DEBUG_PAUSE_STEP &&
		task->call_depth < task->step_start_call_depth;
	task->step_action = EX_DEBUG_CONTINUE;
	task->suspended_frame = (runtime_call_frame){ fn, ip, task->frame, task->stack_top };
	// Reify the complete chain while the interpreter still owns the active
	// frames. Debug queries can then use one stable representation for both
	// task errors and explicit debugger pauses.
	task->fail_frame_count = 0u;
	task->fail_frames[task->fail_frame_count] = task->suspended_frame;
	task->fail_frame_count++;

	for (u32 i = task->call_depth; i > 0u && task->fail_frame_count < (u32)(sizeof(task->fail_frames) / sizeof(task->fail_frames[0])); --i) {
		task->fail_frames[task->fail_frame_count] = task->call_stack[i - 1u];
		task->fail_frame_count++;
	}
	task->is_suspended = true;
	if (step_out_completed && task->call_depth < EX_MAX_CALL_DEPTH) {
		const runtime_call_frame* caller = &task->call_stack[task->call_depth];
		if (caller->function && caller->ip > caller->function->code) {
			const u32 caller_offset = (u32)(caller->ip - 1u - caller->function->code);
			if (runtime_debug_frame_location(task->bytecode, caller->function, caller_offset, &task->pause_event.location)) {
				return EX_CALL_RESULT_SUSPENDED;
			}
		}
	}
	if (!runtime_debug_frame_location(task->bytecode, fn, (u32)(ip - fn->code), &task->pause_event.location)) {
		task->pause_event.location = (ex_debug_location){ {NULL, 0}, 0u, 0u };
	}
	return call_result;
}

ex_runtime* ex_runtime_create(ex_bytecode* bytecode, ex_host* host) {
	if (!bytecode) return NULL;
	if (!host) host = bytecode->host;
	if (!host || !host->arena.allocate) return NULL;

	ex_runtime* runtime = (ex_runtime*)calloc(1, sizeof(ex_runtime));
	if (!runtime) return NULL;

	runtime->bytecode = bytecode;
	runtime->host = host;
	runtime->arena = &host->arena;
	runtime->global_size = bytecode->global_size;
	runtime->globals = (u8*)calloc(bytecode->global_size ? bytecode->global_size : 1u, 1u);
	if (!runtime->globals) {
		free(runtime);
		return NULL;
	}
	if (bytecode->function_count > 0u) {
		runtime->native_callbacks = (ex_native_fn*)calloc((size_t)bytecode->function_count, sizeof(ex_native_fn));
		if (!runtime->native_callbacks) {
			free(runtime->globals);
			free(runtime);
			return NULL;
		}
		runtime->native_callback_count = bytecode->function_count;
		runtime_bind_builtin_callbacks(runtime);
	}

	if (bytecode->has_global_init && bytecode->function_count > 0u) {
		// Global initialization gets a private stack, but its writes target the
		// runtime-global storage. A suspension during creation is unsupported.
		ex_task* init_task = ex_task_create(runtime);
		if (!init_task || runtime_execute_function(init_task, &bytecode->functions[bytecode->function_count - 1u], NULL) != EX_CALL_RESULT_OK) {
			if (init_task) ex_task_destroy(init_task);
			ex_runtime_destroy(runtime);
			return NULL;
		}
		ex_task_destroy(init_task);
	}

	return runtime;
}

void ex_runtime_destroy(ex_runtime* runtime) {
	if (!runtime) return;
	free(runtime->globals);
	free(runtime->native_callbacks);
	free(runtime);
}

ex_task* ex_task_create(ex_runtime* runtime) {
	if (!runtime || !runtime->bytecode) return NULL;
	ex_task* task = (ex_task*)calloc(1, sizeof(ex_task));
	if (!task) return NULL;

	task->runtime = runtime;
	task->host = runtime->host;
	task->bytecode = runtime->bytecode;
	task->globals = runtime->globals;
	task->stack = (u8*)calloc(EX_STACK_CAPACITY_BYTES, 1u);
	if (!task->stack) {
		free(task);
		return NULL;
	}
	task->stack_end = task->stack + EX_STACK_CAPACITY_BYTES;
	task->stack_top = task->stack;
	task->frame = task->stack;
	task->result_size = 0u;
	task->call_depth = 0u;
	task->call_start_depth = 0u;
	task->fail_frame_count = 0u;
	task->is_suspended = false;
	task->step_action = EX_DEBUG_CONTINUE;
	task->step_traps = NULL;
	task->step_trap_count = 0u;
	task->step_trap_capacity = 0u;
	task->state = EX_TASK_READY;
	return task;
}

void ex_task_destroy(ex_task* task) {
	if (!task) return;
	runtime_clear_step_traps(task);
	free(task->stack);
	free(task->step_traps);
	free(task);
}

ex_task_state ex_task_get_state(const ex_task* task) {
	return task ? task->state : EX_TASK_FAILED;
}

ex_result ex_runtime_set_native_resolver(ex_runtime* runtime, ex_native_resolver_fn resolver, void* userdata) {
	if (!runtime) return EX_RESULT_INVALID_ARGUMENT;
	runtime->native_resolver = resolver;
	runtime->native_resolver_userdata = userdata;
	return EX_RESULT_OK;
}

static void task_read_value(ex_task* task, i32 index, void* out, u32 size) {
	if (!task || !out) return;
	u8* p = NULL;
	if (index == -1) {
		if (task->result_size > 0u && task->stack_top >= task->stack + task->result_size)
			p = task->stack_top - task->result_size;
	} else if (index >= 0) {
		p = task->stack + (u32)index * size;
		if (p + size > task->stack_top) p = NULL;
	}
	if (p) memcpy(out, p, size);
}

const void* ex_task_result(ex_task* task, u32* size) {
	if (size) *size = 0u;
	if (!task || task->result_size == 0u || task->stack_top < task->stack + task->result_size) return NULL;
	if (size) *size = task->result_size;
	return task->stack_top - task->result_size;
}

i32 ex_task_to_bool(ex_task* task, i32 index) { u8 v = 0; task_read_value(task, index, &v, 1u); return v != 0u; }
i8 ex_task_to_i8(ex_task* task, i32 index) { i8 v = 0; task_read_value(task, index, &v, 1u); return v; }
u8 ex_task_to_u8(ex_task* task, i32 index) { u8 v = 0; task_read_value(task, index, &v, 1u); return v; }
i16 ex_task_to_i16(ex_task* task, i32 index) { i16 v = 0; task_read_value(task, index, &v, 2u); return v; }
u16 ex_task_to_u16(ex_task* task, i32 index) { u16 v = 0; task_read_value(task, index, &v, 2u); return v; }
i32 ex_task_to_i32(ex_task* task, i32 index) { i32 v = 0; task_read_value(task, index, &v, 4u); return v; }
u32 ex_task_to_u32(ex_task* task, i32 index) { u32 v = 0; task_read_value(task, index, &v, 4u); return v; }
i64 ex_task_to_i64(ex_task* task, i32 index) { i64 v = 0; task_read_value(task, index, &v, 8u); return v; }
u64 ex_task_to_u64(ex_task* task, i32 index) { u64 v = 0; task_read_value(task, index, &v, 8u); return v; }
float ex_task_to_f32(ex_task* task, i32 index) { float v = 0.0f; task_read_value(task, index, &v, 4u); return v; }
double ex_task_to_f64(ex_task* task, i32 index) { double v = 0.0; task_read_value(task, index, &v, 8u); return v; }

ex_string_view ex_task_to_string(ex_task* task, i32 index) {
	ex_runtime_slice slice = {NULL, 0};
	task_read_value(task, index, &slice, (u32)sizeof(slice));
	return (ex_string_view){(const char*)slice.data, slice.length};
}

void* ex_task_to_ptr(ex_task* task, i32 index) {
	void* value = NULL;
	task_read_value(task, index, &value, (u32)sizeof(value));
	return value;
}

ex_call_result ex_call(ex_task* task, ex_string_view function_name, const void* args, u32 args_size) {
	if (!task) return EX_CALL_RESULT_INVALID_ARGUMENT;
	if (task->executing || task->is_suspended) return EX_CALL_RESULT_INVALID_STATE;

	task->result_size = 0u;
	const ex_function_bc* function = runtime_find_function_by_name(task->bytecode, function_name, NULL);
	if (!function) return EX_CALL_RESULT_FUNCTION_NOT_FOUND;
	
	if (args_size != function->param_size) {
		task->state = EX_TASK_FAILED;
		return EX_CALL_RESULT_INVALID_ARGUMENT;
	}
	if (args_size > 0u && !args) {
		task->state = EX_TASK_FAILED;
		return EX_CALL_RESULT_INVALID_ARGUMENT;
	}
	if (function->param_size > EX_STACK_CAPACITY_BYTES) {
		task->state = EX_TASK_FAILED;
		return EX_CALL_RESULT_INVALID_ARGUMENT;
	}

	runtime_clear_step_traps(task);
	task->stack_top = task->stack;
	task->frame = task->stack;
	task->result_size = 0u;
	task->call_depth = 0u;
	task->call_start_depth = 0u;
	task->fail_frame_count = 0u;
	task->is_suspended = false;
	if (args_size) memcpy(task->stack, args, args_size);
	task->stack_top += args_size;

	task->executing = true;
	const ex_call_result result = runtime_execute_function(task, function, NULL);
	task->executing = false;
	if (result == EX_CALL_RESULT_SUSPENDED) task->state = EX_TASK_SUSPENDED;
	else if (result == EX_CALL_RESULT_OK) task->state = EX_TASK_READY;
	else task->state = EX_TASK_FAILED;
	return result;
}

ex_call_result ex_task_resume_suspended(ex_task* task) {
	if (!task) return EX_CALL_RESULT_INVALID_ARGUMENT;
	if (!task->is_suspended) return EX_CALL_RESULT_NOT_SUSPENDED;
	if (task->step_action != EX_DEBUG_CONTINUE) {
		// Arm source-location traps only while executing a step. This shifts the
		// debugger's work from the interpreter's hot loop to resume time.
		u32 count = 0u;
		for (u32 i = 0; i < task->bytecode->function_count; ++i) {
			const ex_function_bc* function = &task->bytecode->functions[i];
			for (u32 j = 0; j < function->source_map_count; ++j) {
				const u32 offset = function->source_map[j].code_offset;
				if (offset < function->code_size && function->code[offset] != (u8)EX_OP_BREAK) ++count;
			}
		}
		if (count > task->step_trap_capacity) {
			runtime_step_trap* traps = (runtime_step_trap*)realloc(task->step_traps, sizeof(runtime_step_trap) * count);
			if (!traps) return EX_CALL_RESULT_OUT_OF_MEMORY;
			task->step_traps = traps;
			task->step_trap_capacity = count;
		}
		for (u32 i = 0; i < task->bytecode->function_count; ++i) {
			ex_function_bc* function = &task->bytecode->functions[i];
			for (u32 j = 0; j < function->source_map_count; ++j) {
				const u32 offset = function->source_map[j].code_offset;
				if (offset >= function->code_size || function->code[offset] == (u8)EX_OP_BREAK) continue;
				u8* code = function->code + offset;
				task->step_traps[task->step_trap_count++] = (runtime_step_trap){ code, *code };
				*code = (u8)EX_OP_BREAK;
			}
		}
	}
	task->fail_frame_count = 0u;
	return runtime_execute_function(task, task->suspended_frame.function, &task->suspended_frame);
}

ex_call_result ex_task_resume(ex_task* task, const ex_type* type, const void* data, u32 size) {
	if (!task) return EX_CALL_RESULT_INVALID_ARGUMENT;
	if (task->executing) return EX_CALL_RESULT_ALREADY_EXECUTING;
	if (!task->is_suspended) return EX_CALL_RESULT_NOT_SUSPENDED;
	if (task->pause_event.reason != EX_DEBUG_PAUSE_YIELD) return EX_CALL_RESULT_NOT_RESUMABLE;

	const bool expects_value = task->suspended_yield_type != EX_TYPE_INDEX_NONE;
	if (!expects_value) {
		if (type || data || size != 0u) return EX_CALL_RESULT_INVALID_YIELD_VALUE;
	}
	else {
		if (!type || type->bytecode != task->bytecode
			|| task->suspended_yield_type >= task->bytecode->type_info_count
			|| type != &task->bytecode->type_info[task->suspended_yield_type]
			|| !data || size != task->suspended_yield_size)
		{
			return EX_CALL_RESULT_INVALID_YIELD_VALUE;
		}
		memcpy(task->suspended_frame.frame + task->suspended_yield_destination, data, size);
	}

	task->executing = true;
	const ex_call_result result = ex_task_resume_suspended(task);
	task->executing = false;
	if (result == EX_CALL_RESULT_SUSPENDED) task->state = EX_TASK_SUSPENDED;
	else if (result == EX_CALL_RESULT_OK) task->state = EX_TASK_READY;
	else task->state = EX_TASK_FAILED;
	return result;
}

ex_type_kind ex_bytecode_runtime_result_kind(ex_runtime* runtime, ex_string_view function_name) {
	const ex_function_bc* fn = runtime_find_function_by_name(runtime->bytecode, function_name, NULL);
	return fn ? fn->return_kind : EX_TYPE_INVALID;
}
