#include "bytecode.h"

 // TODO lot of silent failures here, should be handled better

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct ls_string_box {
	ls_string_view value;
} ls_string_box;

typedef struct ls_runtime_slice {
	const void* data;
	i64 length;
} ls_runtime_slice;

enum {
	EXEC_FAIL = 0,
	EXEC_OK = 1,
	EXEC_SUSPENDED = 2,
};

ls_string_view ls_arg_read_string(ls_call_frame* frame) {
	ls_runtime_slice slice = {NULL, 0};
	memcpy(&slice, frame->args, sizeof(slice));
	frame->args += sizeof(slice);
	return (ls_string_view){(const char*)slice.data, (const char*)slice.data + slice.length};
}

// Content equality for slice values. Floats are compared element-wise because
// NaN is not equal to itself and +0.0 equals -0.0 despite differing bits; every
// other element kind the compiler admits here is an integral scalar whose bytes
// carry no padding, so those go through memcmp.
static int runtime_slice_equal(ls_runtime_slice a, ls_runtime_slice b, u32 element_size, ls_type_kind element_kind) {
	if (a.length != b.length) return 0;
	if (a.length == 0) return 1;
	if (!a.data || !b.data) return 0;
	if (a.data == b.data) return 1;
	if (element_kind == LS_TYPE_F32) {
		for (i64 i = 0; i < a.length; ++i) {
			f32 lhs, rhs;
			memcpy(&lhs, (const u8*)a.data + i * sizeof(f32), sizeof(lhs));
			memcpy(&rhs, (const u8*)b.data + i * sizeof(f32), sizeof(rhs));
			if (!(lhs == rhs)) return 0;
		}
		return 1;
	}
	if (element_kind == LS_TYPE_F64) {
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
		case LS_TYPE_CPTR:
			return 8u;
		case LS_TYPE_SLICE:
			return 16u;
		default:
			return 8u;
	}
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

// TODO leak?
void ls_result_string(ls_runtime* runtime, ls_call_frame* frame, ls_string_view value) {
	ls_string_box* box = runtime_copy_string_box(runtime, value);
	if (!box) return;
	ls_runtime_slice slice = {box->value.begin, (i64)(box->value.end - box->value.begin)};
	memcpy(frame->result, &slice, sizeof(slice));
	frame->result += sizeof(slice);
}

static void runtime_push_bytes(ls_runtime* runtime, const void* value, u32 size) {
	if (runtime->stack_top + size > runtime->stack_end) return;
	memcpy(runtime->stack_top, value, size);
	runtime->stack_top += size;
	runtime->result_size = 0u;
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
static void runtime_native_pow_f32(ls_runtime* runtime, ls_call_frame frame) { (void)runtime; LS_ARG(frame, f32, base); LS_ARG(frame, f32, exponent); LS_TYPED_RESULT(frame, f32, powf(base, exponent)); }
static void runtime_native_pow_f64(ls_runtime* runtime, ls_call_frame frame) { (void)runtime; LS_ARG(frame, f64, base); LS_ARG(frame, f64, exponent); LS_TYPED_RESULT(frame, f64, pow(base, exponent)); }

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
		else if (runtime_string_equals_cstr(fn->name, "pow")) runtime->native_callbacks[i] = &runtime_native_pow_f32;
		else if (runtime_string_equals_cstr(fn->name, "pow_f64")) runtime->native_callbacks[i] = &runtime_native_pow_f64;
		else if (runtime_string_equals_cstr(fn->name, "alloc")) runtime->native_callbacks[i] = &runtime_native_alloc;
		else if (runtime_string_equals_cstr(fn->name, "free")) runtime->native_callbacks[i] = &runtime_native_free;
	}
}

// Resolves the source location active at `code_offset` (the instruction about to
// execute) in `fn`, for the debug event reported at a suspend point. Mirrors
// debugger.c's runtime_source_at, which serves the same lookup for
// ls_debug_frame_location; kept separate since the two files are independent
// translation units and this is a handful of lines.
static bool runtime_debug_frame_location(const ls_bytecode* bytecode, const ls_function_bc* fn, u32 code_offset, ls_debug_location* out_location) {
	if (!fn) return false;
	const ls_bytecode_source_map_entry* result = NULL;
	for (u32 i = 0; i < fn->source_map_count; ++i) {
		const ls_bytecode_source_map_entry* entry = &fn->source_map[i];
		if (entry->code_offset > code_offset) break;
		result = entry;
	}
	if (!result || !bytecode || result->location_index >= bytecode->location_count) return false;
	const ls_bytecode_location* loc = &bytecode->locations[result->location_index];
	out_location->source_name = loc->source_name;
	out_location->line = loc->line;
	out_location->column = loc->column;
	return true;
}

static ls_string_view runtime_error_message(ls_op op) {
	static const char generic[] = "runtime error";
	static const char division_by_zero[] = "division by zero";
	static const char modulo_by_zero[] = "modulo by zero";
	static const char index_out_of_bounds[] = "index out of bounds";
	static const char invalid_function[] = "invalid function call";

	if ((op >= LS_OP_DIV_I8 && op <= LS_OP_DIV_F64) || (op >= LS_OP_DIV_I8_IMM && op <= LS_OP_DIV_F64_IMM)) {
		return (ls_string_view){division_by_zero, division_by_zero + sizeof(division_by_zero) - 1u};
	}
	if ((op >= LS_OP_MOD_I8 && op <= LS_OP_MOD_U64) || (op >= LS_OP_MOD_I8_IMM && op <= LS_OP_MOD_U64_IMM)) {
		return (ls_string_view){modulo_by_zero, modulo_by_zero + sizeof(modulo_by_zero) - 1u};
	}
	if (op >= LS_OP_LOAD_INDEXED && op <= LS_OP_SLICE_REF) return (ls_string_view){index_out_of_bounds, index_out_of_bounds + sizeof(index_out_of_bounds) - 1u};
	if (op == LS_OP_CALL_DIRECT || op == LS_OP_CALL_INDIRECT) return (ls_string_view){invalid_function, invalid_function + sizeof(invalid_function) - 1u};
	return (ls_string_view){generic, generic + sizeof(generic) - 1u};
}

static void runtime_report_error(const ls_runtime* runtime, const ls_function_bc* function, const u8* ip, ls_string_view message) {
	if (!runtime->host->print) return;
	ls_debug_location location;
	if (runtime_debug_frame_location(runtime->bytecode, function, (u32)(ip - function->code), &location) && location.source_name.begin) {
		static const char separator[] = ": ";
		char line_buffer[10];
		char* line = line_buffer + sizeof(line_buffer);
		u32 line_number = location.line;
		do {
			*--line = (char)('0' + line_number % 10u);
			line_number /= 10u;
		} while (line_number != 0u);
		runtime->host->print(runtime->host->diagnostics_userdata, location.source_name);
		runtime->host->print(runtime->host->diagnostics_userdata, (ls_string_view){separator, separator + 1u});
		runtime->host->print(runtime->host->diagnostics_userdata, (ls_string_view){line, line_buffer + sizeof(line_buffer)});
		runtime->host->print(runtime->host->diagnostics_userdata, (ls_string_view){separator + 1u, separator + sizeof(separator) - 1u});
	}
	static const char newline[] = "\n";
	runtime->host->print(runtime->host->diagnostics_userdata, message);
	runtime->host->print(runtime->host->diagnostics_userdata, (ls_string_view){newline, newline + sizeof(newline) - 1u});
}

static const ls_bytecode_breakpoint* runtime_find_breakpoint(const ls_bytecode* bytecode, const u8* code) {
	for (u32 i = 0; i < bytecode->breakpoint_count; ++i) {
		const ls_bytecode_breakpoint* breakpoint = &bytecode->breakpoints[i];
		if (breakpoint->code == code) return breakpoint;
	}
	return NULL;
}

static const runtime_step_trap* runtime_find_step_trap(const ls_runtime* runtime, const u8* code) {
	for (u32 i = 0; i < runtime->step_trap_count; ++i) {
		const runtime_step_trap* trap = &runtime->step_traps[i];
		if (trap->code == code) return trap;
	}
	return NULL;
}

// Step traps are private, short-lived patches. User breakpoints already own
// their LS_OP_BREAK bytes and are intentionally left untouched here.
static void runtime_clear_step_traps(ls_runtime* runtime) {
	for (u32 i = 0; i < runtime->step_trap_count; ++i) {
		const runtime_step_trap* trap = &runtime->step_traps[i];
		*trap->code = trap->original_byte;
	}
	runtime->step_trap_count = 0u;
}

static bool runtime_should_pause_for_step(const ls_runtime* runtime, const ls_function_bc* function, u32 code_offset) {
	ls_debug_location location;
	if (!runtime_debug_frame_location(runtime->bytecode, function, code_offset, &location)) return false;
	if (location.line == runtime->step_start_line && runtime->call_depth == runtime->step_start_call_depth) return false;
	if (runtime->step_action == LS_DEBUG_STEP_INTO) return true;
	if (runtime->step_action == LS_DEBUG_STEP_OVER) return runtime->call_depth <= runtime->step_start_call_depth;
	return runtime->call_depth < runtime->step_start_call_depth;
}

// Arm source-location traps only while executing a step. This shifts the
// debugger's work from the interpreter's hot loop to resume/suspend time.
static bool runtime_arm_step_traps(ls_runtime* runtime) {
	u32 count = 0u;
	for (u32 i = 0; i < runtime->bytecode->function_count; ++i) {
		const ls_function_bc* function = &runtime->bytecode->functions[i];
		for (u32 j = 0; j < function->source_map_count; ++j) {
			const u32 offset = function->source_map[j].code_offset;
			if (offset < function->code_size && function->code[offset] != (u8)LS_OP_BREAK) ++count;
		}
	}
	if (count > runtime->step_trap_capacity) {
		runtime_step_trap* traps = (runtime_step_trap*)realloc(runtime->step_traps, sizeof(runtime_step_trap) * count);
		if (!traps) return false;
		runtime->step_traps = traps;
		runtime->step_trap_capacity = count;
	}
	for (u32 i = 0; i < runtime->bytecode->function_count; ++i) {
		ls_function_bc* function = &runtime->bytecode->functions[i];
		for (u32 j = 0; j < function->source_map_count; ++j) {
			const u32 offset = function->source_map[j].code_offset;
			if (offset >= function->code_size || function->code[offset] == (u8)LS_OP_BREAK) continue;
			u8* code = function->code + offset;
			runtime->step_traps[runtime->step_trap_count++] = (runtime_step_trap){ code, *code };
			*code = (u8)LS_OP_BREAK;
		}
	}
	return true;
}

static bool runtime_enter_script_call(
	ls_runtime* runtime,
	const ls_function_bc** function,
	const u8** ip,
	const ls_function_bc* callee,
	u8* callee_frame,
	u8* caller_stack_top
) {
	if (runtime->call_depth >= LS_MAX_CALL_DEPTH) return false;
	u8* callee_stack_top = callee_frame + callee->frame_size;
	if (callee_stack_top > runtime->stack_end) return false;

	runtime->call_stack[runtime->call_depth] = (runtime_call_frame){ *function, *ip, runtime->frame, caller_stack_top };
	runtime->call_depth++;
	*function = callee;
	*ip = callee->code;
	runtime->frame = callee_frame;
	runtime->stack_top = callee_stack_top;
	return true;
}

static bool runtime_invoke_native(ls_runtime* runtime, u32 function_index, const ls_function_bc* function, u8* args, u8** result_stack_top) {
	if (function_index >= runtime->native_callback_count) return false;
	const ls_native_fn callback = runtime->native_callbacks[function_index];
	if (!callback) return false;

	u8* stack_top = args + function->return_size;
	if (stack_top > runtime->stack_end) return false;

	const ls_call_frame frame = { args, args };
	callback(runtime, frame);
	if (result_stack_top) *result_stack_top = stack_top;
	return true;
}

static u64 runtime_read_u64(const u8** ip) {
	u64 value = 0;
	memcpy(&value, *ip, sizeof(value));
	*ip += sizeof(value);
	return value;
}

static u32 runtime_read_u32(const u8** ip) {
	u32 value = 0;
	memcpy(&value, *ip, sizeof(value));
	*ip += sizeof(value);
	return value;
}

static i32 runtime_read_i32(const u8** ip) {
	i32 value = 0;
	memcpy(&value, *ip, sizeof(value));
	*ip += sizeof(value);
	return value;
}

static i32 runtime_read_i16(const u8** ip) {
	i16 value = 0;
	memcpy(&value, *ip, sizeof(value));
	*ip += sizeof(value);
	return (i32)value;
}

static double runtime_numeric_to_double(const u8* value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_BOOL: return (value && value[0] != 0u) ? 1.0 : 0.0;
		case LS_TYPE_I8:   { i8 v = 0; memcpy(&v, value, 1); return (double)v; }
		case LS_TYPE_U8:   { u8 v = 0; memcpy(&v, value, 1); return (double)v; }
		case LS_TYPE_I16:  { i16 v = 0; memcpy(&v, value, 2); return (double)v; }
		case LS_TYPE_U16:  { u16 v = 0; memcpy(&v, value, 2); return (double)v; }
		case LS_TYPE_I32: { i32 v = 0; memcpy(&v, value, 4); return (double)v; }
		case LS_TYPE_ENUM: { u32 v = 0; memcpy(&v, value, 4); return (double)v; }
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
		case LS_TYPE_I32: { i32 v = 0; memcpy(&v, value, 4); return (i64)v; }
		case LS_TYPE_ENUM: { u32 v = 0; memcpy(&v, value, 4); return (i64)v; }
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
		case LS_TYPE_I32: { i32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case LS_TYPE_ENUM: { u32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case LS_TYPE_U32:  { u32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case LS_TYPE_I64:  { i64 v = 0; memcpy(&v, value, 8); return (u64)v; }
		case LS_TYPE_U64:  { u64 v = 0; memcpy(&v, value, 8); return v; }
		case LS_TYPE_F32:  { f32 v = 0; memcpy(&v, value, 4); return (u64)v; }
		case LS_TYPE_F64:  { f64 v = 0; memcpy(&v, value, 8); return (u64)v; }
		default:           return 0u;
	}
}

static i64 runtime_immediate_to_i64(u64 value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_I8: return (i8)value;
		case LS_TYPE_I16: return (i16)value;
		case LS_TYPE_I32: return (i32)value;
		case LS_TYPE_I64: return (i64)value;
		case LS_TYPE_ENUM: return (u32)value;
		default: return (i64)value;
	}
}

static u64 runtime_immediate_to_u64(u64 value, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_U8: return (u8)value;
		case LS_TYPE_U16: return (u16)value;
		case LS_TYPE_U32: return (u32)value;
		case LS_TYPE_BOOL: return value != 0u;
		default: return value;
	}
}

#define LS_REG_BINOP(TYPE, EXPR) \
	do { \
		const u32 dst__ = runtime_read_u32(&ip); \
		const u32 lhs_offset__ = runtime_read_u32(&ip); \
		const u32 rhs_offset__ = runtime_read_u32(&ip); \
		TYPE a = 0; TYPE b = 0; \
		u8* lhs__ = runtime->frame + lhs_offset__; \
		u8* rhs__ = runtime->frame + rhs_offset__; \
		u8* out__ = runtime->frame + dst__; \
		memcpy(&a, lhs__, sizeof(TYPE)); \
		memcpy(&b, rhs__, sizeof(TYPE)); \
		TYPE result__ = (TYPE)(EXPR); \
		memcpy(out__, &result__, sizeof(TYPE)); \
	} while (0)

#define LS_REG_BINOP_IMM(TYPE, EXPR) \
	do { \
		const u32 dst__ = runtime_read_u32(&ip); \
		const u32 lhs_offset__ = runtime_read_u32(&ip); \
		TYPE b = 0; \
		memcpy(&b, ip, sizeof(TYPE)); \
		ip += sizeof(TYPE); \
		TYPE a = 0; \
		u8* lhs__ = runtime->frame + lhs_offset__; \
		u8* out__ = runtime->frame + dst__; \
		memcpy(&a, lhs__, sizeof(TYPE)); \
		TYPE result__ = (TYPE)(EXPR); \
		memcpy(out__, &result__, sizeof(TYPE)); \
	} while (0)

#define LS_REG_DIVOP(TYPE, EXPR) \
	do { \
		const u32 dst__ = runtime_read_u32(&ip); \
		const u32 lhs_offset__ = runtime_read_u32(&ip); \
		const u32 rhs_offset__ = runtime_read_u32(&ip); \
		TYPE a = 0; TYPE b = 0; \
		u8* lhs__ = runtime->frame + lhs_offset__; \
		u8* rhs__ = runtime->frame + rhs_offset__; \
		u8* out__ = runtime->frame + dst__; \
		memcpy(&a, lhs__, sizeof(TYPE)); \
		memcpy(&b, rhs__, sizeof(TYPE)); \
		if (!b) goto runtime_execute_function_fail; \
		TYPE result__ = (TYPE)(EXPR); \
		memcpy(out__, &result__, sizeof(TYPE)); \
	} while (0)

#define LS_REG_DIVOP_IMM(TYPE, EXPR) \
	do { \
		const u32 dst__ = runtime_read_u32(&ip); \
		const u32 lhs_offset__ = runtime_read_u32(&ip); \
		TYPE b = 0; \
		memcpy(&b, ip, sizeof(TYPE)); \
		ip += sizeof(TYPE); \
		TYPE a = 0; \
		u8* lhs__ = runtime->frame + lhs_offset__; \
		u8* out__ = runtime->frame + dst__; \
		memcpy(&a, lhs__, sizeof(TYPE)); \
		if (!b) goto runtime_execute_function_fail; \
		TYPE result__ = (TYPE)(EXPR); \
		memcpy(out__, &result__, sizeof(TYPE)); \
	} while (0)

#define LS_REG_NEGOP(TYPE) \
	do { \
		const u32 dst__ = runtime_read_u32(&ip); \
		const u32 src__ = runtime_read_u32(&ip); \
		TYPE value__ = 0; \
		TYPE result__ = 0; \
		u8* src_ptr__ = runtime->frame + src__; \
		u8* out__ = runtime->frame + dst__; \
		memcpy(&value__, src_ptr__, sizeof(TYPE)); \
		result__ = (TYPE)(0 - value__); \
		memcpy(out__, &result__, sizeof(TYPE)); \
	} while (0)

#define LS_REG_CMP_NUMERIC(OP) \
	do { \
		const u32 dst__ = runtime_read_u32(&ip); \
		const u32 lhs_offset__ = runtime_read_u32(&ip); \
		const u32 rhs_offset__ = runtime_read_u32(&ip); \
		const ls_type_kind kind__ = (ls_type_kind)*ip++; \
		u8* lhs_ptr__ = runtime->frame + lhs_offset__; \
		u8* rhs_ptr__ = runtime->frame + rhs_offset__; \
		u8* out__ = runtime->frame + dst__; \
		int result__ = 0; \
		if (kind__ == LS_TYPE_F32 || kind__ == LS_TYPE_F64) { \
			result__ = runtime_numeric_to_double(lhs_ptr__, kind__) OP runtime_numeric_to_double(rhs_ptr__, kind__); \
		} else { \
			result__ = runtime_numeric_to_i64(lhs_ptr__, kind__) OP runtime_numeric_to_i64(rhs_ptr__, kind__); \
		} \
		*out__ = result__ ? 1u : 0u; \
	} while (0)

#define LS_REG_CMP_JUMP(TYPE, OP) \
	do { \
		const u32 lhs_offset__ = runtime_read_u32(&ip); \
		const u32 rhs_offset__ = runtime_read_u32(&ip); \
		const i32 jump_offset__ = runtime_read_i16(&ip); \
		TYPE lhs__; TYPE rhs__; \
		memcpy(&lhs__, runtime->frame + lhs_offset__, sizeof(lhs__)); \
		memcpy(&rhs__, runtime->frame + rhs_offset__, sizeof(rhs__)); \
		if (!(lhs__ OP rhs__)) ip += jump_offset__; \
	} while (0)

// Runs either a fresh call to `function` or a previously suspended
// frame. The runtime stack is already parked at the correct state when
// `resume_frame` is non-NULL, so fresh-call setup is skipped in that case.
static int runtime_execute_function(ls_runtime* runtime, const ls_function_bc* function, const runtime_call_frame* resume_frame) {
	const ls_function_bc* fn = function;
	const u8* ip;
	ls_op op = (ls_op)0;
	// Restore point for the whole host call, retained across suspend/resume.
	runtime_restore_point* initial;
	if (resume_frame) {
		ASSERT(fn == resume_frame->function);
		fn = resume_frame->function;
		ip = resume_frame->ip;
		if (runtime->call_start_depth == 0u) return EXEC_FAIL;
		initial = &runtime->call_starts[runtime->call_start_depth - 1];
		runtime->is_suspended = false;

		const ls_bytecode_breakpoint* breakpoint = runtime_find_breakpoint(runtime->bytecode, ip);
		if (breakpoint) {
			op = (ls_op)breakpoint->original_byte;
			++ip;
			goto runtime_execute_function_dispatch;
		}
	} else {
		if (!fn) return EXEC_FAIL;

		// Set before the frame-size check below can jump to the fail label, so a
		// failure there (stack overflow at call entry, before the loop starts)
		// reports this call's own function/instruction instead of reading garbage.
		ip = fn->code;

		if (runtime->stack_top < runtime->stack + fn->param_size) return EXEC_FAIL;
		if (runtime->call_start_depth >= LS_MAX_CALL_DEPTH) return EXEC_FAIL;

		initial = &runtime->call_starts[runtime->call_start_depth];
		runtime->call_start_depth++;
		*initial = (runtime_restore_point){ runtime->frame, runtime->stack_top, runtime->result_size, runtime->call_depth };

		u8* args = runtime->stack_top - fn->param_size;
		if (fn->kind == LS_FUNCTION_NATIVE) {
			runtime->result_size = 0u;
			u8* result_stack_top = NULL;
			if (!runtime_invoke_native(runtime, (u32)(fn - runtime->bytecode->functions), fn, args, &result_stack_top)) goto runtime_execute_function_fail;

			runtime->stack_top = result_stack_top;
			runtime->result_size = fn->return_size;
			runtime->frame = initial->frame;
			runtime->call_depth = initial->call_depth;
			--runtime->call_start_depth;
			return EXEC_OK;
		}

		u8* frame_stack_top = args + fn->frame_size;
		if (frame_stack_top > runtime->stack_end) goto runtime_execute_function_fail;

		runtime->frame = args;
		runtime->stack_top = frame_stack_top;
	}

	for (;;) {
		op = (ls_op)*ip;
		ip++;
		
	runtime_execute_function_dispatch:
		switch (op) {
			// TODO const table?
			case LS_OP_LOAD_CONST_1: {
				const u32 dst = runtime_read_u32(&ip);
				u8 value = *ip++;
				u8* out = runtime->frame + dst;
				*out = value;
				break;
			}
			case LS_OP_LOAD_CONST_2: {
				const u32 dst = runtime_read_u32(&ip);
				u8* out = runtime->frame + dst;
				memcpy(out, ip, 2u);
				ip += 2u;
				break;
			}
			case LS_OP_LOAD_CONST_4: {
				const u32 dst = runtime_read_u32(&ip);
				u8* out = runtime->frame + dst;
				memcpy(out, ip, 4u);
				ip += 4u;
				break;
			}
			case LS_OP_LOAD_CONST_8: {
				const u32 dst = runtime_read_u32(&ip);
				u8* out = runtime->frame + dst;
				memcpy(out, ip, 8u);
				ip += 8u;
				break;
			}
			case LS_OP_COPY: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 src_offset = runtime_read_u32(&ip);
				const u32 size = runtime_read_u32(&ip);
				u8* out = runtime->frame + dst;
				u8* src = runtime->frame + src_offset;
				memmove(out, src, size);
				break;
			}
			case LS_OP_FRAME_PTR: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 offset = runtime_read_u32(&ip);
				u8* out = runtime->frame + dst;
				void* ptr = runtime->frame + offset;
				memcpy(out, &ptr, sizeof(ptr));
				break;
			}
			case LS_OP_GLOBAL_PTR: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 offset = runtime_read_u32(&ip);
				if (offset >= runtime->bytecode->global_size) goto runtime_execute_function_fail;
				void* ptr = runtime->stack + offset;
				memcpy(runtime->frame + dst, &ptr, sizeof(ptr));
				break;
			}
			case LS_OP_LOAD_PTR: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 addr = runtime_read_u32(&ip);
				const u32 size = runtime_read_u32(&ip);
				void* ptr = NULL;
				memcpy(&ptr, runtime->frame + addr, sizeof(ptr));
				u8* value = (u8*)ptr;
				if (!value) goto runtime_execute_function_fail;
				memmove(runtime->frame + dst, value, size);
				break;
			}
			case LS_OP_STORE_PTR: {
				const u32 addr = runtime_read_u32(&ip);
				const u32 src = runtime_read_u32(&ip);
				const u32 size = runtime_read_u32(&ip);
				void* ptr = NULL;
				memcpy(&ptr, runtime->frame + addr, sizeof(ptr));
				u8* value = (u8*)ptr;
				if (!value) goto runtime_execute_function_fail;
				memmove(value, runtime->frame + src, size);
				break;
			}
			case LS_OP_LOAD_INDEXED:
			case LS_OP_LOAD_INDEXED_IMM: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 base_offset = runtime_read_u32(&ip);
				const bool immediate = op == LS_OP_LOAD_INDEXED_IMM;
				const u64 immediate_index = immediate ? runtime_read_u64(&ip) : 0u;
				const u32 index_reg = immediate ? 0u : runtime_read_u32(&ip);
				const ls_type_kind index_kind = (ls_type_kind)*ip++;
				const u64 length = runtime_read_u64(&ip);
				const u32 element_size = runtime_read_u32(&ip);
				const i64 signed_index = immediate ? runtime_immediate_to_i64(immediate_index, index_kind) : runtime_numeric_to_i64(runtime->frame + index_reg, index_kind);
				const u64 index = (index_kind == LS_TYPE_I8 || index_kind == LS_TYPE_I16 || index_kind == LS_TYPE_I32 || index_kind == LS_TYPE_I64 || index_kind == LS_TYPE_ENUM)
					? (signed_index < 0 ? length : (u64)signed_index)
					: (immediate ? runtime_immediate_to_u64(immediate_index, index_kind) : runtime_numeric_to_u64(runtime->frame + index_reg, index_kind));
				if (index >= length || (element_size != 0u && index > ((u64)-1 / element_size))) goto runtime_execute_function_fail;
				memmove(runtime->frame + dst, runtime->frame + base_offset + index * element_size, element_size);
				break;
			}
			case LS_OP_STORE_INDEXED:
			case LS_OP_STORE_INDEXED_IMM: {
				const u32 base_offset = runtime_read_u32(&ip);
				const bool immediate = op == LS_OP_STORE_INDEXED_IMM;
				const u64 immediate_index = immediate ? runtime_read_u64(&ip) : 0u;
				const u32 index_reg = immediate ? 0u : runtime_read_u32(&ip);
				const ls_type_kind index_kind = (ls_type_kind)*ip++;
				const u64 length = runtime_read_u64(&ip);
				const u32 element_size = runtime_read_u32(&ip);
				const u32 src = runtime_read_u32(&ip);
				const i64 signed_index = immediate ? runtime_immediate_to_i64(immediate_index, index_kind) : runtime_numeric_to_i64(runtime->frame + index_reg, index_kind);
				const u64 index = (index_kind == LS_TYPE_I8 || index_kind == LS_TYPE_I16 || index_kind == LS_TYPE_I32 || index_kind == LS_TYPE_I64 || index_kind == LS_TYPE_ENUM)
					? (signed_index < 0 ? length : (u64)signed_index)
					: (immediate ? runtime_immediate_to_u64(immediate_index, index_kind) : runtime_numeric_to_u64(runtime->frame + index_reg, index_kind));
				if (index >= length || (element_size != 0u && index > ((u64)-1 / element_size))) goto runtime_execute_function_fail;
				memmove(runtime->frame + base_offset + index * element_size, runtime->frame + src, element_size);
				break;
			}
			case LS_OP_BOUNDS_CHECK: {
				const u32 index_reg = runtime_read_u32(&ip);
				const ls_type_kind index_kind = (ls_type_kind)*ip++;
				const u64 length = runtime_read_u64(&ip);
				u8* index_ptr = runtime->frame + index_reg;
				if (index_kind == LS_TYPE_I8 || index_kind == LS_TYPE_I16 || index_kind == LS_TYPE_I32 || index_kind == LS_TYPE_I64 || index_kind == LS_TYPE_ENUM) {
					const i64 index = runtime_numeric_to_i64(index_ptr, index_kind);
					if (index < 0 || (u64)index >= length) goto runtime_execute_function_fail;
				}
				else if (runtime_numeric_to_u64(index_ptr, index_kind) >= length) goto runtime_execute_function_fail;
				break;
			}
			case LS_OP_STRING_SLICE:
				const u32 slice_reg = runtime_read_u32(&ip);
				const u32 index = runtime_read_u32(&ip);
				ls_string_view str = runtime->bytecode->strings[index];
				u8* slice = runtime->frame + slice_reg;
				memcpy(slice, &str.begin, sizeof(str.begin));
				u64 len = str.end - str.begin;
				memcpy(slice + sizeof(void*), &len, 8u);
				break;
			case LS_OP_SLICE: {
				/* The resulting slice overwrites the source slice value in place. */
				const u32 slice_reg = runtime_read_u32(&ip);
				const u32 begin_reg = runtime_read_u32(&ip);
				const u32 end_reg = runtime_read_u32(&ip);
				const u32 element_size = runtime_read_u32(&ip);
				i64 end = 0, begin = 0, length = 0;
				void* base_ptr = NULL;
				u8* slice = runtime->frame + slice_reg;
				u8* begin_ptr = runtime->frame + begin_reg;
				u8* end_ptr = runtime->frame + end_reg;
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
				const u32 dst = runtime_read_u32(&ip);
				const u32 slice_offset = runtime_read_u32(&ip);
				const u32 index_reg = runtime_read_u32(&ip);
				const u32 element_size = runtime_read_u32(&ip);
				i64 index = 0;
				i64 length = 0;
				void* base_ptr = NULL;
				u8* slice = runtime->frame + slice_offset;
				u8* index_ptr = runtime->frame + index_reg;
				u8* out = runtime->frame + dst;
				memcpy(&base_ptr, slice, sizeof(base_ptr));
				memcpy(&length, slice + sizeof(void*), 8u);
				memcpy(&index, index_ptr, 8u);
				if (!base_ptr || index < 0 || (u64)index >= (u64)length) goto runtime_execute_function_fail;
				const u64 offset = (u64)index * element_size;
				if (element_size != 0u && offset / element_size != (u64)index) goto runtime_execute_function_fail;
				memmove(out, (u8*)base_ptr + offset, element_size);
				break;
			}
			case LS_OP_SLICE_REF: {
				/* The resulting pointer overwrites the slice value in place. */
				const u32 slice_reg = runtime_read_u32(&ip);
				const u32 index_reg = runtime_read_u32(&ip);
				const u32 element_size = runtime_read_u32(&ip);
				i64 index = 0;
				i64 length = 0;
				void* base_ptr = NULL;
				u8* slice = runtime->frame + slice_reg;
				u8* index_ptr = runtime->frame + index_reg;
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
				const u32 dst = runtime_read_u32(&ip);
				const u32 slice_reg = runtime_read_u32(&ip);
				i64 length = 0;
				u8* slice = runtime->frame + slice_reg;
				u8* out = runtime->frame + dst;
				memcpy(&length, slice + sizeof(void*), 8u);
				memcpy(out, &length, 8u);
				break;
			}
			case LS_OP_SLICE_EQ: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 lhs_reg = runtime_read_u32(&ip);
				const u32 rhs_reg = runtime_read_u32(&ip);
				const u32 element_size = runtime_read_u32(&ip);
				const ls_type_kind element_kind = (ls_type_kind)*ip++;
				ls_runtime_slice lhs = {NULL, 0};
				ls_runtime_slice rhs = {NULL, 0};
				u8* out = runtime->frame + dst;
				memcpy(&lhs, runtime->frame + lhs_reg, sizeof(lhs));
				memcpy(&rhs, runtime->frame + rhs_reg, sizeof(rhs));
				*out = (u8)(runtime_slice_equal(lhs, rhs, element_size, element_kind) ? 1u : 0u);
				break;
			}
			case LS_OP_RETURN: {
				const u32 src = runtime_read_u32(&ip);
				const u32 size = runtime_read_u32(&ip);
				if (size > 0u) {
					u8* src_ptr = runtime->frame + src;
					if (runtime->frame + size > runtime->stack_end) goto runtime_execute_function_fail;
					memmove(runtime->frame, src_ptr, size);
					runtime->stack_top = runtime->frame + size;
				}
				else {
					runtime->stack_top = runtime->frame;
				}
				if (runtime->call_depth == initial->call_depth) {
					runtime_clear_step_traps(runtime);
					runtime->step_action = LS_DEBUG_CONTINUE;
						runtime->result_size = size;
						runtime->frame = initial->frame;
						--runtime->call_start_depth;
						return 1;
				}
				const runtime_call_frame caller = runtime->call_stack[--runtime->call_depth];
				fn = caller.function;
				ip = caller.ip;
				runtime->frame = caller.frame;
				runtime->stack_top = caller.stack_top;
				break;
			}
			case LS_OP_CALL_DIRECT: {
				const u32 callee_index = runtime_read_u32(&ip);
				const u32 arg_base = runtime_read_u32(&ip);
				u8* caller_top = runtime->stack_top;
				const ls_function_bc* callee = &runtime->bytecode->functions[callee_index];
				if (runtime->frame + arg_base > caller_top) goto runtime_execute_function_fail;

				if (!runtime_enter_script_call(runtime, &fn, &ip, callee, runtime->frame + arg_base, caller_top)) goto runtime_execute_function_fail;
				break;
			}
			case LS_OP_CALL_NATIVE: {
				const u32 callee_index = runtime_read_u32(&ip);
				const u32 arg_base = runtime_read_u32(&ip);
				u8* caller_top = runtime->stack_top;
				const ls_function_bc* callee = &runtime->bytecode->functions[callee_index];
				if (runtime->frame + arg_base > caller_top) goto runtime_execute_function_fail;
				if (!runtime_invoke_native(runtime, callee_index, callee, runtime->frame + arg_base, NULL)) goto runtime_execute_function_fail;
				runtime->stack_top = caller_top;
				break;
			}
			case LS_OP_CALL_INDIRECT: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 arg_size = runtime_read_u32(&ip);
				const u32 return_size = runtime_read_u32(&ip);
				(void)return_size;
				const u32 arg = dst + 4u;
				u8* callee_ptr = runtime->frame + dst;
				u32 callee_index = 0;
				memcpy(&callee_index, callee_ptr, sizeof(callee_index));
				u8* caller_top = runtime->stack_top;
				if (callee_index >= runtime->bytecode->function_count) goto runtime_execute_function_fail;
				const ls_function_bc* callee = &runtime->bytecode->functions[callee_index];

				// The function-value slot at dst is no longer needed once
				// callee_index is read out of it above; shift the argument
				// bytes down over it so the callee's params sit at frame
				// offset 0 relative to `dst`, exactly like CALL_DIRECT's
				// arg_base. This is what lets the rest of this case be a
				// straight copy of CALL_DIRECT's push-onto-call_stack path
				// below, with zero special-casing needed in LS_OP_RETURN: the
				// callee's own RETURN deposits its result at its frame
				// (== dst), which is exactly where the caller expects it.
				if (arg_size > 0u) memmove(runtime->frame + dst, runtime->frame + arg, arg_size);

				if (callee->kind == LS_FUNCTION_NATIVE) {
					if (!runtime_invoke_native(runtime, callee_index, callee, runtime->frame + dst, NULL)) goto runtime_execute_function_fail;
					runtime->stack_top = caller_top;
					break;
				}

				if (!runtime_enter_script_call(runtime, &fn, &ip, callee, runtime->frame + dst, caller_top)) goto runtime_execute_function_fail;
				break;
			}
			case LS_OP_CAST: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 src = runtime_read_u32(&ip);
				const ls_type_kind src_kind = (ls_type_kind)*ip++;
				const ls_type_kind dst_kind = (ls_type_kind)*ip++;
				u8* src_ptr = runtime->frame + src;
				u8* out = runtime->frame + dst;
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
			case LS_OP_NOT: {
				const u32 dst = runtime_read_u32(&ip);
				const u32 src = runtime_read_u32(&ip);
				u8* src_ptr = runtime->frame + src;
				u8* out = runtime->frame + dst;
				*out = *src_ptr ? 0u : 1u;
				break;
			}
			#define LS_ARITH_BIN_CASES(OP, EXPR) \
				case LS_OP_##OP##_I8: LS_REG_BINOP(i8, a EXPR b); break; \
				case LS_OP_##OP##_U8: LS_REG_BINOP(u8, a EXPR b); break; \
				case LS_OP_##OP##_I16: LS_REG_BINOP(i16, a EXPR b); break; \
				case LS_OP_##OP##_U16: LS_REG_BINOP(u16, a EXPR b); break; \
				case LS_OP_##OP##_I32: LS_REG_BINOP(i32, a EXPR b); break; \
				case LS_OP_##OP##_U32: LS_REG_BINOP(u32, a EXPR b); break; \
				case LS_OP_##OP##_I64: LS_REG_BINOP(i64, a EXPR b); break; \
				case LS_OP_##OP##_U64: LS_REG_BINOP(u64, a EXPR b); break; \
				case LS_OP_##OP##_F32: LS_REG_BINOP(f32, a EXPR b); break; \
				case LS_OP_##OP##_F64: LS_REG_BINOP(f64, a EXPR b); break; \
				case LS_OP_##OP##_I8_IMM: LS_REG_BINOP_IMM(i8, a EXPR b); break; \
				case LS_OP_##OP##_U8_IMM: LS_REG_BINOP_IMM(u8, a EXPR b); break; \
				case LS_OP_##OP##_I16_IMM: LS_REG_BINOP_IMM(i16, a EXPR b); break; \
				case LS_OP_##OP##_U16_IMM: LS_REG_BINOP_IMM(u16, a EXPR b); break; \
				case LS_OP_##OP##_I32_IMM: LS_REG_BINOP_IMM(i32, a EXPR b); break; \
				case LS_OP_##OP##_U32_IMM: LS_REG_BINOP_IMM(u32, a EXPR b); break; \
				case LS_OP_##OP##_I64_IMM: LS_REG_BINOP_IMM(i64, a EXPR b); break; \
				case LS_OP_##OP##_U64_IMM: LS_REG_BINOP_IMM(u64, a EXPR b); break; \
				case LS_OP_##OP##_F32_IMM: LS_REG_BINOP_IMM(f32, a EXPR b); break; \
				case LS_OP_##OP##_F64_IMM: LS_REG_BINOP_IMM(f64, a EXPR b); break;
			#define LS_ARITH_INT_CASES(OP, EXPR) \
				case LS_OP_##OP##_8: LS_REG_BINOP(u8, a EXPR b); break; \
				case LS_OP_##OP##_16: LS_REG_BINOP(u16, a EXPR b); break; \
				case LS_OP_##OP##_32: LS_REG_BINOP(u32, a EXPR b); break; \
				case LS_OP_##OP##_64: LS_REG_BINOP(u64, a EXPR b); break; \
				case LS_OP_##OP##_8_IMM: LS_REG_BINOP_IMM(u8, a EXPR b); break; \
				case LS_OP_##OP##_16_IMM: LS_REG_BINOP_IMM(u16, a EXPR b); break; \
				case LS_OP_##OP##_32_IMM: LS_REG_BINOP_IMM(u32, a EXPR b); break; \
				case LS_OP_##OP##_64_IMM: LS_REG_BINOP_IMM(u64, a EXPR b); break; \
				case LS_OP_##OP##_F32: LS_REG_BINOP(f32, a EXPR b); break; \
				case LS_OP_##OP##_F64: LS_REG_BINOP(f64, a EXPR b); break; \
				case LS_OP_##OP##_F32_IMM: LS_REG_BINOP_IMM(f32, a EXPR b); break; \
				case LS_OP_##OP##_F64_IMM: LS_REG_BINOP_IMM(f64, a EXPR b); break;
			#define LS_ARITH_DIV_CASES(OP, EXPR) \
				case LS_OP_##OP##_I8: LS_REG_DIVOP(i8, a EXPR b); break; \
				case LS_OP_##OP##_U8: LS_REG_DIVOP(u8, a EXPR b); break; \
				case LS_OP_##OP##_I16: LS_REG_DIVOP(i16, a EXPR b); break; \
				case LS_OP_##OP##_U16: LS_REG_DIVOP(u16, a EXPR b); break; \
				case LS_OP_##OP##_I32: LS_REG_DIVOP(i32, a EXPR b); break; \
				case LS_OP_##OP##_U32: LS_REG_DIVOP(u32, a EXPR b); break; \
				case LS_OP_##OP##_I64: LS_REG_DIVOP(i64, a EXPR b); break; \
				case LS_OP_##OP##_U64: LS_REG_DIVOP(u64, a EXPR b); break; \
				case LS_OP_##OP##_F32: LS_REG_DIVOP(f32, a EXPR b); break; \
				case LS_OP_##OP##_F64: LS_REG_DIVOP(f64, a EXPR b); break; \
				case LS_OP_##OP##_I8_IMM: LS_REG_DIVOP_IMM(i8, a EXPR b); break; \
				case LS_OP_##OP##_U8_IMM: LS_REG_DIVOP_IMM(u8, a EXPR b); break; \
				case LS_OP_##OP##_I16_IMM: LS_REG_DIVOP_IMM(i16, a EXPR b); break; \
				case LS_OP_##OP##_U16_IMM: LS_REG_DIVOP_IMM(u16, a EXPR b); break; \
				case LS_OP_##OP##_I32_IMM: LS_REG_DIVOP_IMM(i32, a EXPR b); break; \
				case LS_OP_##OP##_U32_IMM: LS_REG_DIVOP_IMM(u32, a EXPR b); break; \
				case LS_OP_##OP##_I64_IMM: LS_REG_DIVOP_IMM(i64, a EXPR b); break; \
				case LS_OP_##OP##_U64_IMM: LS_REG_DIVOP_IMM(u64, a EXPR b); break; \
				case LS_OP_##OP##_F32_IMM: LS_REG_DIVOP_IMM(f32, a EXPR b); break; \
				case LS_OP_##OP##_F64_IMM: LS_REG_DIVOP_IMM(f64, a EXPR b); break;
			#define LS_MOD_CASES \
				case LS_OP_MOD_I8: LS_REG_DIVOP(i8, a % b); break; case LS_OP_MOD_I8_IMM: LS_REG_DIVOP_IMM(i8, a % b); break; \
				case LS_OP_MOD_U8: LS_REG_DIVOP(u8, a % b); break; case LS_OP_MOD_U8_IMM: LS_REG_DIVOP_IMM(u8, a % b); break; \
				case LS_OP_MOD_I16: LS_REG_DIVOP(i16, a % b); break; case LS_OP_MOD_I16_IMM: LS_REG_DIVOP_IMM(i16, a % b); break; \
				case LS_OP_MOD_U16: LS_REG_DIVOP(u16, a % b); break; case LS_OP_MOD_U16_IMM: LS_REG_DIVOP_IMM(u16, a % b); break; \
				case LS_OP_MOD_I32: LS_REG_DIVOP(i32, a % b); break; case LS_OP_MOD_I32_IMM: LS_REG_DIVOP_IMM(i32, a % b); break; \
				case LS_OP_MOD_U32: LS_REG_DIVOP(u32, a % b); break; case LS_OP_MOD_U32_IMM: LS_REG_DIVOP_IMM(u32, a % b); break; \
				case LS_OP_MOD_I64: LS_REG_DIVOP(i64, a % b); break; case LS_OP_MOD_I64_IMM: LS_REG_DIVOP_IMM(i64, a % b); break; \
				case LS_OP_MOD_U64: LS_REG_DIVOP(u64, a % b); break; case LS_OP_MOD_U64_IMM: LS_REG_DIVOP_IMM(u64, a % b); break;
			LS_ARITH_INT_CASES(ADD, +)
			LS_ARITH_INT_CASES(SUB, -)
			LS_ARITH_INT_CASES(MUL, *)
			LS_ARITH_DIV_CASES(DIV, /)
			LS_MOD_CASES
			#undef LS_MOD_CASES
			#undef LS_ARITH_DIV_CASES
			#undef LS_ARITH_INT_CASES
			#undef LS_ARITH_BIN_CASES
			case LS_OP_INC_I32: {
				const u32 dst = runtime_read_u32(&ip);
				i32 value = 0;
				memcpy(&value, runtime->frame + dst, 4u);
				value = (i32)((u32)value + 1u);
				memcpy(runtime->frame + dst, &value, 4u);
				break;
			}
			case LS_OP_INC_I64: {
				const u32 dst = runtime_read_u32(&ip);
				i64 value = 0;
				memcpy(&value, runtime->frame + dst, 8u);
				value = (i64)((u64)value + 1u);
				memcpy(runtime->frame + dst, &value, 8u);
				break;
			}
			case LS_OP_DEC_I32: {
				const u32 dst = runtime_read_u32(&ip);
				i32 value = 0;
				memcpy(&value, runtime->frame + dst, 4u);
				value = (i32)((u32)value - 1u);
				memcpy(runtime->frame + dst, &value, 4u);
				break;
			}
			case LS_OP_DEC_I64: {
				const u32 dst = runtime_read_u32(&ip);
				i64 value = 0;
				memcpy(&value, runtime->frame + dst, 8u);
				value = (i64)((u64)value - 1u);
				memcpy(runtime->frame + dst, &value, 8u);
				break;
			}
			case LS_OP_EQ: LS_REG_CMP_NUMERIC(==); break;
			case LS_OP_NE: LS_REG_CMP_NUMERIC(!=); break;
			case LS_OP_LT: LS_REG_CMP_NUMERIC(<); break;
			case LS_OP_LE: LS_REG_CMP_NUMERIC(<=); break;
			case LS_OP_GT: LS_REG_CMP_NUMERIC(>); break;
			case LS_OP_GE: LS_REG_CMP_NUMERIC(>=); break;
			case LS_OP_JE_I8: LS_REG_CMP_JUMP(i8, !=); break;
			case LS_OP_JGE_I8: LS_REG_CMP_JUMP(i8, <); break;
			case LS_OP_JGT_I8: LS_REG_CMP_JUMP(i8, <=); break;
			case LS_OP_JLT_I8: LS_REG_CMP_JUMP(i8, >=); break;
			case LS_OP_JLE_I8: LS_REG_CMP_JUMP(i8, >); break;
			case LS_OP_JE_U8: LS_REG_CMP_JUMP(u8, !=); break;
			case LS_OP_JGE_U8: LS_REG_CMP_JUMP(u8, <); break;
			case LS_OP_JGT_U8: LS_REG_CMP_JUMP(u8, <=); break;
			case LS_OP_JLT_U8: LS_REG_CMP_JUMP(u8, >=); break;
			case LS_OP_JLE_U8: LS_REG_CMP_JUMP(u8, >); break;
			case LS_OP_JE_I16: LS_REG_CMP_JUMP(i16, !=); break;
			case LS_OP_JGE_I16: LS_REG_CMP_JUMP(i16, <); break;
			case LS_OP_JGT_I16: LS_REG_CMP_JUMP(i16, <=); break;
			case LS_OP_JLT_I16: LS_REG_CMP_JUMP(i16, >=); break;
			case LS_OP_JLE_I16: LS_REG_CMP_JUMP(i16, >); break;
			case LS_OP_JE_U16: LS_REG_CMP_JUMP(u16, !=); break;
			case LS_OP_JGE_U16: LS_REG_CMP_JUMP(u16, <); break;
			case LS_OP_JGT_U16: LS_REG_CMP_JUMP(u16, <=); break;
			case LS_OP_JLT_U16: LS_REG_CMP_JUMP(u16, >=); break;
			case LS_OP_JLE_U16: LS_REG_CMP_JUMP(u16, >); break;
			case LS_OP_JE_I32: LS_REG_CMP_JUMP(i32, !=); break;
			case LS_OP_JGE_I32: LS_REG_CMP_JUMP(i32, <); break;
			case LS_OP_JGT_I32: LS_REG_CMP_JUMP(i32, <=); break;
			case LS_OP_JLT_I32: LS_REG_CMP_JUMP(i32, >=); break;
			case LS_OP_JLE_I32: LS_REG_CMP_JUMP(i32, >); break;
			case LS_OP_JE_U32: LS_REG_CMP_JUMP(u32, !=); break;
			case LS_OP_JGE_U32: LS_REG_CMP_JUMP(u32, <); break;
			case LS_OP_JGT_U32: LS_REG_CMP_JUMP(u32, <=); break;
			case LS_OP_JLT_U32: LS_REG_CMP_JUMP(u32, >=); break;
			case LS_OP_JLE_U32: LS_REG_CMP_JUMP(u32, >); break;
			case LS_OP_JE_I64: LS_REG_CMP_JUMP(i64, !=); break;
			case LS_OP_JGE_I64: LS_REG_CMP_JUMP(i64, <); break;
			case LS_OP_JGT_I64: LS_REG_CMP_JUMP(i64, <=); break;
			case LS_OP_JLT_I64: LS_REG_CMP_JUMP(i64, >=); break;
			case LS_OP_JLE_I64: LS_REG_CMP_JUMP(i64, >); break;
			case LS_OP_JE_U64: LS_REG_CMP_JUMP(u64, !=); break;
			case LS_OP_JGE_U64: LS_REG_CMP_JUMP(u64, <); break;
			case LS_OP_JGT_U64: LS_REG_CMP_JUMP(u64, <=); break;
			case LS_OP_JLT_U64: LS_REG_CMP_JUMP(u64, >=); break;
			case LS_OP_JLE_U64: LS_REG_CMP_JUMP(u64, >); break;
			case LS_OP_JE_F32: LS_REG_CMP_JUMP(f32, !=); break;
			case LS_OP_JGE_F32: LS_REG_CMP_JUMP(f32, <); break;
			case LS_OP_JGT_F32: LS_REG_CMP_JUMP(f32, <=); break;
			case LS_OP_JLT_F32: LS_REG_CMP_JUMP(f32, >=); break;
			case LS_OP_JLE_F32: LS_REG_CMP_JUMP(f32, >); break;
			case LS_OP_JE_F64: LS_REG_CMP_JUMP(f64, !=); break;
			case LS_OP_JGE_F64: LS_REG_CMP_JUMP(f64, <); break;
			case LS_OP_JGT_F64: LS_REG_CMP_JUMP(f64, <=); break;
			case LS_OP_JLT_F64: LS_REG_CMP_JUMP(f64, >=); break;
			case LS_OP_JLE_F64: LS_REG_CMP_JUMP(f64, >); break;
			case LS_OP_JNE_I8: LS_REG_CMP_JUMP(i8, ==); break;
			case LS_OP_JNE_U8: LS_REG_CMP_JUMP(u8, ==); break;
			case LS_OP_JNE_I16: LS_REG_CMP_JUMP(i16, ==); break;
			case LS_OP_JNE_U16: LS_REG_CMP_JUMP(u16, ==); break;
			case LS_OP_JNE_I32: LS_REG_CMP_JUMP(i32, ==); break;
			case LS_OP_JNE_U32: LS_REG_CMP_JUMP(u32, ==); break;
			case LS_OP_JNE_I64: LS_REG_CMP_JUMP(i64, ==); break;
			case LS_OP_JNE_U64: LS_REG_CMP_JUMP(u64, ==); break;
			case LS_OP_JNE_F32: LS_REG_CMP_JUMP(f32, ==); break;
			case LS_OP_JNE_F64: LS_REG_CMP_JUMP(f64, ==); break;
			case LS_OP_JUMP: {
				const i32 offset = runtime_read_i16(&ip);
				ip += offset;
				break;
			}
			case LS_OP_JZ_U8: {
				const u32 cond = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				u8* ptr = runtime->frame + cond;
				if (*ptr == 0u) ip += offset;
				break;
			}
			case LS_OP_JNZ_U8: {
				const u32 cond = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				u8* ptr = runtime->frame + cond;
				if (*ptr != 0u) ip += offset;
				break;
			}
			case LS_OP_JZ_I32: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i32 value = 0;
				memcpy(&value, runtime->frame + reg, 4u);
				if (value == 0) ip += offset;
				break;
			}
			case LS_OP_JZ_I64: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i64 value = 0;
				memcpy(&value, runtime->frame + reg, 8u);
				if (value == 0) ip += offset;
				break;
			}
			case LS_OP_JGZ_I32: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i32 value = 0;
				memcpy(&value, runtime->frame + reg, 4u);
				if (value > 0) ip += offset;
				break;
			}
			case LS_OP_JGZ_I64: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i64 value = 0;
				memcpy(&value, runtime->frame + reg, 8u);
				if (value > 0) ip += offset;
				break;
			}
			case LS_OP_RETURN_BASE: {
				if (runtime->call_depth == initial->call_depth) {
					const u32 size = fn->return_size;
					runtime_clear_step_traps(runtime);
					runtime->step_action = LS_DEBUG_CONTINUE;
					runtime->stack_top = runtime->frame + size;
					runtime->result_size = size;
					runtime->frame = initial->frame;
					--runtime->call_start_depth;
					return 1;
				}

				runtime->call_depth--;
				const runtime_call_frame caller = runtime->call_stack[runtime->call_depth];
				fn = caller.function;
				ip = caller.ip;
				runtime->frame = caller.frame;
				runtime->stack_top = caller.stack_top;
				break;
			}
			case LS_OP_JGEZ_I32: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i32 value = 0;
				memcpy(&value, runtime->frame + reg, 4u);
				if (value >= 0) ip += offset;
				break;
			}
			case LS_OP_JGEZ_I64: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i64 value = 0;
				memcpy(&value, runtime->frame + reg, 8u);
				if (value >= 0) ip += offset;
				break;
			}
			case LS_OP_JLTZ_I32: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i32 value = 0;
				memcpy(&value, runtime->frame + reg, 4u);
				if (value < 0) ip += offset;
				break;
			}
			case LS_OP_JLTZ_I64: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i64 value = 0;
				memcpy(&value, runtime->frame + reg, 8u);
				if (value < 0) ip += offset;
				break;
			}
			case LS_OP_JLEZ_I32: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i32 value = 0;
				memcpy(&value, runtime->frame + reg, 4u);
				if (value <= 0) ip += offset;
				break;
			}
			case LS_OP_JLEZ_I64: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i64 value = 0;
				memcpy(&value, runtime->frame + reg, 8u);
				if (value <= 0) ip += offset;
				break;
			}
			case LS_OP_JNZ_I32: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i32 value = 0;
				memcpy(&value, runtime->frame + reg, 4u);
				if (value != 0) ip += offset;
				break;
			}
			case LS_OP_JNZ_I64: {
				const u32 reg = runtime_read_u32(&ip);
				const i32 offset = runtime_read_i16(&ip);
				i64 value = 0;
				memcpy(&value, runtime->frame + reg, 8u);
				if (value != 0) ip += offset;
				break;
			}
			case LS_OP_BREAK: {
				// ip already points one byte past LS_OP_BREAK. Rewind it before a
				// suspend so resuming re-executes the trapped original opcode.
				const u8* code = ip - 1u;
				const u32 code_offset = (u32)(code - fn->code);
				const runtime_step_trap* step_trap = runtime_find_step_trap(runtime, code);
				const ls_bytecode_breakpoint* bp = runtime_find_breakpoint(runtime->bytecode, code);
				if (!step_trap && !bp) goto runtime_execute_function_fail;
				--ip;
				if (step_trap && runtime_should_pause_for_step(runtime, fn, code_offset)) {
					runtime->pause_event.reason = LS_DEBUG_PAUSE_STEP;
					runtime->pause_event.message = (ls_string_view){NULL, NULL};
					goto runtime_execute_function_suspend;
				}

				if (bp) {
					runtime->pause_event.reason = LS_DEBUG_PAUSE_BREAKPOINT;
					runtime->pause_event.message = (ls_string_view){NULL, NULL};
					goto runtime_execute_function_suspend;
				}

				op = (ls_op)(step_trap ? step_trap->original_byte : bp->original_byte);
				++ip;
				goto runtime_execute_function_dispatch;
			}
			default:
				goto runtime_execute_function_fail;
		}
	}

runtime_execute_function_fail:
	const ls_string_view error_message = runtime_error_message(op);
	runtime_clear_step_traps(runtime);
	// The dispatch loop advances ip while decoding operands. On this rare path,
	// recover the preceding emitted opcode from the source map instead of
	// maintaining a second instruction pointer on every successful dispatch.
	{
		const u32 code_offset = (u32)(ip - fn->code);
		const ls_bytecode_source_map_entry* entry = NULL;
		for (u32 i = 0; i < fn->source_map_count; ++i) {
			if (fn->source_map[i].code_offset >= code_offset) break;
			entry = &fn->source_map[i];
		}
		ip = entry ? fn->code + entry->code_offset : fn->code;
	}
	runtime_report_error(runtime, fn, ip, error_message);
	// A runtime error suspends using the same reified frame as LS_OP_BREAK;
	// state is left exactly as-is instead of being unwound.
	runtime->pause_event.reason = LS_DEBUG_PAUSE_ERROR;
	runtime->pause_event.message = error_message;
	goto runtime_execute_function_suspend;

	// Snapshot the call stack (innermost first) so `ls_debug_*` can report a
	// trace after `ls_call` returns failure.
	{
		u32 recorded = 0u;
		if (recorded < (u32)(sizeof(runtime->fail_frames) / sizeof(runtime->fail_frames[0]))) {
			runtime->fail_frames[recorded].function = fn;
			runtime->fail_frames[recorded].ip = ip;
			runtime->fail_frames[recorded].frame = runtime->frame;
			runtime->fail_frames[recorded].stack_top = runtime->stack_top;
			++recorded;
		}
		for (u32 i = runtime->call_depth; i > 0u && recorded < (u32)(sizeof(runtime->fail_frames) / sizeof(runtime->fail_frames[0])); --i) {
			runtime->fail_frames[recorded++] = runtime->call_stack[i - 1u];
		}
		runtime->fail_frame_count = recorded;
	}

	runtime->stack_top = initial->stack_top;
	runtime->frame = initial->frame;
	runtime->result_size = initial->result_size;
	runtime->call_depth = initial->call_depth;
	--runtime->call_start_depth;
	return EXEC_FAIL;

runtime_execute_function_suspend:
	runtime_clear_step_traps(runtime);
	// A completed STEP_OUT reports the call site the stepped-out frame
	// returned into rather than the first statement boundary reached after the
	// return (the trap this pause fires on). The caller's frame on
	// call_stack[call_depth] was saved with ip as the return address just past
	// its CALL_DIRECT (the same convention debugger.c's
	// debug_frame_lookup_offset relies on), so ip - 1 resolves back to the
	// call statement. The suspension itself stays parked at the real resume
	// point (the boundary trap) so a CONTINUE re-executes correctly.
	const int step_out_completed =
		runtime->step_action == LS_DEBUG_STEP_OUT &&
		runtime->pause_event.reason == LS_DEBUG_PAUSE_STEP &&
		runtime->call_depth < runtime->step_start_call_depth;
	runtime->step_action = LS_DEBUG_CONTINUE;
	runtime->suspended_frame = (runtime_call_frame){ fn, ip, runtime->frame, runtime->stack_top };
	// Reify the complete chain while the interpreter still owns the active
	// frames. Debug queries can then use one stable representation for both
	// runtime errors and explicit debugger pauses.
	runtime->fail_frame_count = 0u;
	if (runtime->fail_frame_count < (u32)(sizeof(runtime->fail_frames) / sizeof(runtime->fail_frames[0]))) {
		runtime->fail_frames[runtime->fail_frame_count++] = runtime->suspended_frame;
	}
	for (u32 i = runtime->call_depth; i > 0u && runtime->fail_frame_count < (u32)(sizeof(runtime->fail_frames) / sizeof(runtime->fail_frames[0])); --i) {
		runtime->fail_frames[runtime->fail_frame_count++] = runtime->call_stack[i - 1u];
	}
	runtime->is_suspended = true;
	if (step_out_completed && runtime->call_depth < LS_MAX_CALL_DEPTH) {
		const runtime_call_frame* caller = &runtime->call_stack[runtime->call_depth];
		if (caller->function && caller->ip > caller->function->code) {
			const u32 caller_offset = (u32)(caller->ip - 1u - caller->function->code);
			if (runtime_debug_frame_location(runtime->bytecode, caller->function, caller_offset, &runtime->pause_event.location)) {
				return EXEC_SUSPENDED;
			}
		}
	}
	if (!runtime_debug_frame_location(runtime->bytecode, fn, (u32)(ip - fn->code), &runtime->pause_event.location)) {
		runtime->pause_event.location = (ls_debug_location){ {NULL, NULL}, 0u, 0u };
	}
	return EXEC_SUSPENDED;
}

ls_runtime* ls_runtime_create(ls_bytecode* bytecode, ls_host* host) {
	if (!bytecode) return NULL;
	if (!host) host = bytecode->host;
	if (!host || !host->arena.allocate) return NULL;

	ls_runtime* runtime = (ls_runtime*)calloc(1, sizeof(ls_runtime));
	if (!runtime) return NULL;

	runtime->bytecode = bytecode;
	runtime->host = host;
	runtime->result_size = 0u;
	runtime->arena = &host->arena;

	runtime->stack = (u8*)calloc(LS_STACK_CAPACITY_BYTES, 1u);
	if (!runtime->stack) {
		free(runtime);
		return NULL;
	}
	runtime->stack_end = runtime->stack + LS_STACK_CAPACITY_BYTES;
	runtime->frame = runtime->stack;

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

	runtime->stack_top = runtime->stack + bytecode->global_size;
	if (bytecode->has_global_init && bytecode->function_count > 0u) {
		// No host can resume a runtime before creation completes, so a
		// suspension here is treated as creation failure below.
		if (runtime_execute_function(runtime, &bytecode->functions[bytecode->function_count - 1u], NULL) != EXEC_OK) {
			ls_runtime_destroy(runtime);
			return NULL;
		}
	}

	return runtime;
}

void ls_runtime_destroy(ls_runtime* runtime) {
	if (!runtime) return;
	runtime_clear_step_traps(runtime);
	free(runtime->stack);
	free(runtime->native_callbacks);
	free(runtime->step_traps);
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
void ls_push_string(ls_runtime* runtime, ls_string_view value) { ls_runtime_slice slice = {value.begin, (i64)(value.end - value.begin)}; runtime_push_bytes(runtime, &slice, (u32)sizeof(slice)); }
void ls_push_null(ls_runtime* runtime) { void* p = NULL; runtime_push_bytes(runtime, &p, (u32)sizeof(p)); }
void ls_push_ptr(ls_runtime* runtime, void* value) { runtime_push_bytes(runtime, &value, (u32)sizeof(value)); }

static void runtime_read_value(ls_runtime* runtime, i32 index, void* out, u32 size) {
	u8* p = NULL;
	if (index >= 0) {
		p = runtime->stack + (u32)index * size;
		if (p + size > runtime->stack_top) p = NULL;
	} else {
		p = runtime->stack_top + (i64)index * size;
		if (p < runtime->stack) p = NULL;
	}
	if (p) memcpy(out, p, size);
}

const void* ls_call_result(ls_runtime* runtime, u32* size) {
	if (size) *size = 0u;
	if (runtime->result_size == 0u || runtime->stack_top < runtime->stack + runtime->result_size) return NULL;
	if (size) *size = runtime->result_size;
	return runtime->stack_top - runtime->result_size;
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
	ls_runtime_slice slice = {NULL, 0};
	runtime_read_value(runtime, index, &slice, (u32)sizeof(slice));
	return (ls_string_view){(const char*)slice.data, (const char*)slice.data + slice.length};
}

void* ls_to_ptr(ls_runtime* runtime, i32 index) {
	void* value = NULL;
	runtime_read_value(runtime, index, &value, (u32)sizeof(value));
	return value;
}

static ls_result runtime_exec_result_to_ls_result(int exec_result) {
	switch (exec_result) {
		case EXEC_OK: return LS_RESULT_OK;
		case EXEC_SUSPENDED: return LS_RESULT_SUSPENDED;
		default: return LS_RESULT_FAILURE;
	}
}

ls_result ls_call(ls_runtime* runtime, ls_string_view function_name) {
	if (runtime->is_suspended) return LS_RESULT_FAILURE;
	const ls_function_bc* function = runtime_find_function_by_name(runtime->bytecode, function_name, NULL);
	if (!function) return LS_RESULT_FAILURE;
	runtime->fail_frame_count = 0u;
	return runtime_exec_result_to_ls_result(runtime_execute_function(runtime, function, NULL));
}

ls_result ls_call_index(ls_runtime* runtime, i32 function_index) {
	if (runtime->is_suspended) return LS_RESULT_FAILURE;
	if (function_index < 0) return LS_RESULT_FAILURE;
	if ((u32)function_index >= runtime->bytecode->function_count) return LS_RESULT_FAILURE;
	const ls_function_bc* function = &runtime->bytecode->functions[(u32)function_index];
	runtime->fail_frame_count = 0u;
	return runtime_exec_result_to_ls_result(runtime_execute_function(runtime, function, NULL));
}

ls_result ls_runtime_resume_suspended(ls_runtime* runtime) {
	if (!runtime->is_suspended) return LS_RESULT_FAILURE;
	if (runtime->step_action != LS_DEBUG_CONTINUE && !runtime_arm_step_traps(runtime)) return LS_RESULT_FAILURE;
	runtime->fail_frame_count = 0u;
	return runtime_exec_result_to_ls_result(runtime_execute_function(runtime, runtime->suspended_frame.function, &runtime->suspended_frame));
}

ls_type_kind ls_bytecode_runtime_result_kind(ls_runtime* runtime, ls_string_view function_name) {
	const ls_function_bc* fn = runtime_find_function_by_name(runtime->bytecode, function_name, NULL);
	return fn ? fn->return_kind : LS_TYPE_INVALID;
}
