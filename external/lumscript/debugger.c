#include "bytecode.h"

#include <stdlib.h>
#include <string.h>

static int debug_string_equals(ls_string_view a, ls_string_view b) {
	const ptrdiff_t a_size = a.length;
	const ptrdiff_t b_size = b.length;
	if (a_size != b_size) return 0;
	return a_size == 0 || memcmp(a.begin, b.begin, (size_t)a_size) == 0;
}

static const ls_bytecode_source_map_entry* runtime_source_at(const ls_function_bc* fn, u32 code_offset) {
	const ls_bytecode_source_map_entry* result = NULL;
	for (u32 i = 0; i < fn->source_map_count; ++i) {
		const ls_bytecode_source_map_entry* entry = &fn->source_map[i];
		if (entry->code_offset > code_offset) break;
		result = entry;
	}
	return result;
}

// Both a failed call and a live suspension expose "the call stack, innermost
// first" through the same `ls_debug_stack_depth`/`frame_function_name`/
// `frame_location` API. A failed call already has this reified in
// `fail_frames` (milestone 1). A live suspension instead has its innermost
// frame in `suspended_frame` (the loop's locals at the trap, saved because
// they'd otherwise die with the C stack frame that returned) and its
// ancestors in `call_stack[0..call_depth)`, exactly like `fail_frames` is
// assembled - so this reads from whichever is current instead of duplicating
// the assembly logic into a second array.
static u32 debug_active_frame_count(ls_runtime* runtime) {
	if (runtime->is_suspended && runtime->fail_frame_count) return runtime->fail_frame_count;
	if (runtime->is_suspended) return 1u + runtime->call_depth;
	return runtime->fail_frame_count;
}

static const runtime_call_frame* debug_active_frame(ls_runtime* runtime, u32 frame_index) {
	if (runtime->is_suspended) {
		if (runtime->fail_frame_count) return &runtime->fail_frames[frame_index];
		if (frame_index == 0u) return &runtime->suspended_frame;
		const u32 call_stack_index = runtime->call_depth - frame_index;
		return &runtime->call_stack[call_stack_index];
	}
	return &runtime->fail_frames[frame_index];
}

u32 ls_debug_stack_depth(ls_runtime* runtime) {
	return debug_active_frame_count(runtime);
}

ls_string_view ls_debug_frame_function_name(ls_runtime* runtime, u32 frame_index) {
	ls_string_view empty = {NULL, 0};
	if (frame_index >= debug_active_frame_count(runtime)) return empty;
	const runtime_call_frame* frame = debug_active_frame(runtime, frame_index);
	return frame->function ? frame->function->name : empty;
}

// The bytecode offset "at" a frame's current statement, for both source
// lookup and local-scope filtering. Ancestor frames (frame_index > 0, pulled
// from call_stack in both the fail and suspend cases) always save ip as the
// return address just past their CALL_DIRECT, so ip - 1 lands inside the call
// instruction itself. The innermost frame's ip convention instead depends on
// how it was reached: a suspend (LS_OP_BREAK, or the error-suspend path in
// runtime.c) rewinds ip to point AT the trapping instruction before saving,
// so no adjustment is needed there; a plain failure's snapshot (milestone 1,
// `fail_frames`) captures ip AFTER the failing instruction's fetch, matching
// the ancestor convention, so it needs the same ip - 1 adjustment.
static u32 debug_frame_lookup_offset(ls_runtime* runtime, u32 frame_index, const runtime_call_frame* frame) {
	const int ip_already_at_instruction = frame_index == 0u && runtime->is_suspended;
	const u32 offset = (u32)(frame->ip - frame->function->code);
	return (!ip_already_at_instruction && offset > 0u) ? offset - 1u : offset;
}

ls_result ls_debug_frame_location(ls_runtime* runtime, u32 frame_index, ls_debug_location* out_location) {
	if (!out_location) return LS_RESULT_FAILURE;
	out_location->source_name = (ls_string_view){NULL, 0};
	out_location->line = 0u;
	out_location->column = 0u;
	if (frame_index >= debug_active_frame_count(runtime)) return LS_RESULT_FAILURE;
	const runtime_call_frame* frame = debug_active_frame(runtime, frame_index);
	if (!frame->function) return LS_RESULT_FAILURE;
	const u32 lookup_offset = debug_frame_lookup_offset(runtime, frame_index, frame);
	const ls_bytecode_source_map_entry* entry = runtime_source_at(frame->function, lookup_offset);
	if (!entry) return LS_RESULT_FAILURE;
	if (!runtime->bytecode || entry->location_index >= runtime->bytecode->location_count) return LS_RESULT_FAILURE;
	const ls_bytecode_location* loc = &runtime->bytecode->locations[entry->location_index];
	out_location->source_name = loc->source_name;
	out_location->line = loc->line;
	out_location->column = loc->column;
	return LS_RESULT_OK;
}

// Finds the source map entry with the smallest line number >= `line` in
// `source_name`, breaking ties by code_offset (earliest statement on that
// line). Search spans every function since breakpoints are addressed by
// source line, not function.
static int debug_find_breakpoint_target(const ls_bytecode* bytecode, ls_string_view source_name, u32 line, u32* out_function_index, u32* out_code_offset, u32* out_line) {
	int found = 0;
	u32 best_line = 0u;
	u32 best_function_index = 0u;
	u32 best_code_offset = 0u;
	for (u32 function_index = 0; function_index < bytecode->function_count; ++function_index) {
		const ls_function_bc* fn = &bytecode->functions[function_index];
		for (u32 i = 0; i < fn->source_map_count; ++i) {
			const ls_bytecode_source_map_entry* entry = &fn->source_map[i];
			if (entry->location_index >= bytecode->location_count) continue;
			const ls_bytecode_location* loc = &bytecode->locations[entry->location_index];
			if (loc->line < line) continue;
			if (!debug_string_equals(loc->source_name, source_name)) continue;
			if (!found || loc->line < best_line || (loc->line == best_line && entry->code_offset < best_code_offset)) {
				found = 1;
				best_line = loc->line;
				best_function_index = function_index;
				best_code_offset = entry->code_offset;
			}
		}
	}
	if (!found) return 0;
	*out_function_index = best_function_index;
	*out_code_offset = best_code_offset;
	*out_line = best_line;
	return 1;
}

static ls_bytecode_breakpoint* debug_find_breakpoint(ls_bytecode* bytecode, const u8* code) {
	for (u32 i = 0; i < bytecode->breakpoint_count; ++i) {
		ls_bytecode_breakpoint* bp = &bytecode->breakpoints[i];
		if (bp->code == code) return bp;
	}
	return NULL;
}

ls_result ls_debug_set_breakpoint(ls_bytecode* bytecode, ls_string_view source_name, u32 line, u32* resolved_line) {
	if (resolved_line) *resolved_line = 0u;
	if (!bytecode) return LS_RESULT_FAILURE;

	u32 function_index = 0u;
	u32 code_offset = 0u;
	u32 found_line = 0u;
	if (!debug_find_breakpoint_target(bytecode, source_name, line, &function_index, &code_offset, &found_line)) return LS_RESULT_FAILURE;

	ls_function_bc* fn = &bytecode->functions[function_index];
	u8* code = fn->code + code_offset;
	if (debug_find_breakpoint(bytecode, code)) {
		if (resolved_line) *resolved_line = found_line;
		return LS_RESULT_OK;
	}

	if (bytecode->breakpoint_count == bytecode->breakpoint_capacity) {
		const u32 new_capacity = bytecode->breakpoint_capacity == 0u ? 8u : bytecode->breakpoint_capacity * 2u;
		ls_bytecode_breakpoint* grown = (ls_bytecode_breakpoint*)realloc(bytecode->breakpoints, sizeof(ls_bytecode_breakpoint) * new_capacity);
		if (!grown) return LS_RESULT_FAILURE;
		bytecode->breakpoints = grown;
		bytecode->breakpoint_capacity = new_capacity;
	}

	ls_bytecode_breakpoint* bp = &bytecode->breakpoints[bytecode->breakpoint_count++];
	bp->code = code;
	bp->original_byte = *code;
	*code = (u8)LS_OP_BREAK;

	if (resolved_line) *resolved_line = found_line;
	return LS_RESULT_OK;
}

ls_result ls_debug_remove_breakpoint(ls_bytecode* bytecode, ls_string_view source_name, u32 line) {
	if (!bytecode) return LS_RESULT_FAILURE;

	u32 function_index = 0u;
	u32 code_offset = 0u;
	u32 found_line = 0u;
	if (!debug_find_breakpoint_target(bytecode, source_name, line, &function_index, &code_offset, &found_line)) return LS_RESULT_FAILURE;

	const u8* code = bytecode->functions[function_index].code + code_offset;
	for (u32 i = 0; i < bytecode->breakpoint_count; ++i) {
		ls_bytecode_breakpoint* bp = &bytecode->breakpoints[i];
		if (bp->code != code) continue;
		*bp->code = bp->original_byte;
		bytecode->breakpoints[i] = bytecode->breakpoints[bytecode->breakpoint_count - 1u];
		--bytecode->breakpoint_count;
		return LS_RESULT_OK;
	}
	return LS_RESULT_FAILURE;
}

void ls_debug_remove_all_breakpoints(ls_bytecode* bytecode) {
	if (!bytecode) return;
	for (u32 i = 0; i < bytecode->breakpoint_count; ++i) {
		const ls_bytecode_breakpoint* bp = &bytecode->breakpoints[i];
		*bp->code = bp->original_byte;
	}
	bytecode->breakpoint_count = 0u;
}

// Ends a live suspension without resuming the interpreter: reports the
// suspend point as a failed-call stack trace (fail_frames, same shape
// ls_call's fail path leaves it in) and clears is_suspended so ls_call/
// ls_call_index accept new calls again. Used by ls_debug_resume(LS_DEBUG_ABORT).
static void debug_abandon_suspension(ls_runtime* runtime) {
	u32 recorded = 0u;
	if (recorded < (u32)(sizeof(runtime->fail_frames) / sizeof(runtime->fail_frames[0]))) {
		runtime->fail_frames[recorded++] = runtime->suspended_frame;
	}
	for (u32 i = runtime->call_depth; i > 0u && recorded < (u32)(sizeof(runtime->fail_frames) / sizeof(runtime->fail_frames[0])); --i) {
		runtime->fail_frames[recorded++] = runtime->call_stack[i - 1u];
	}
	runtime->fail_frame_count = recorded;
	runtime->is_suspended = false;
	ASSERT(runtime->call_start_depth > 0u);
	const runtime_restore_point* initial = &runtime->call_starts[runtime->call_start_depth - 1u];
	runtime->call_depth = initial->call_depth;
	runtime->stack_top = initial->stack_top;
	runtime->frame = initial->frame;
	runtime->result_size = initial->result_size;
	--runtime->call_start_depth;
	runtime->step_action = LS_DEBUG_CONTINUE;
}

int ls_debug_is_suspended(ls_runtime* runtime) {
	return runtime->is_suspended ? 1 : 0;
}

ls_result ls_debug_pause_event(ls_runtime* runtime, ls_debug_event* out_event) {
	if (!out_event) return LS_RESULT_FAILURE;
	if (!runtime->is_suspended) return LS_RESULT_FAILURE;
	*out_event = runtime->pause_event;
	return LS_RESULT_OK;
}

ls_result ls_debug_resume(ls_runtime* runtime, ls_debug_action action) {
	if (!runtime->is_suspended) return LS_RESULT_FAILURE;
	if (action == LS_DEBUG_ABORT) {
		debug_abandon_suspension(runtime);
		return LS_RESULT_FAILURE;
	}
	// The pause event's location is exactly "the line we're stopped at" (it
	// was populated from this same suspended_frame at suspend time), so it
	// doubles as the step's starting line without a second source-map lookup.
	// call_depth is still whatever it was at suspend (resume hasn't touched
	// it yet), i.e. the depth of the suspended innermost frame's caller chain.
	// runtime.c clears step_action at the next suspend, whatever the reason
	// (step condition met, a breakpoint, or an error) - from the host's
	// perspective any suspension ends this step attempt.
	runtime->step_action = action;
	runtime->step_start_line = runtime->pause_event.location.line;
	runtime->step_start_call_depth = runtime->call_depth;
	return ls_runtime_resume_suspended(runtime);
}

// Finds the local_index'th entry among `frame`'s locals whose scope has
// begun by the frame's current statement (see debug_frame_lookup_offset and
// ls_bytecode_local_debug_entry's scope_begin_offset), or NULL past the end.
// `ls_debug_frame_local_count` is the same walk with no target index, kept
// separate from a cached array since function->local_count is typically small.
static const ls_bytecode_local_debug_entry* debug_local_at(ls_runtime* runtime, u32 frame_index, u32 local_index, u32* out_visible_count) {
	if (frame_index >= debug_active_frame_count(runtime)) {
		if (out_visible_count) *out_visible_count = 0u;
		return NULL;
	}
	const runtime_call_frame* frame = debug_active_frame(runtime, frame_index);
	if (!frame->function) {
		if (out_visible_count) *out_visible_count = 0u;
		return NULL;
	}
	const u32 lookup_offset = debug_frame_lookup_offset(runtime, frame_index, frame);
	const ls_bytecode_local_debug_entry* found = NULL;
	u32 visible_count = 0u;
	for (u32 i = 0; i < frame->function->local_count; ++i) {
		const ls_bytecode_local_debug_entry* entry = &frame->function->locals[i];
		if (entry->scope_begin_offset > lookup_offset) continue;
		if (visible_count == local_index) found = entry;
		++visible_count;
	}
	if (out_visible_count) *out_visible_count = visible_count;
	return found;
}

u32 ls_debug_frame_local_count(ls_runtime* runtime, u32 frame_index) {
	u32 count = 0u;
	debug_local_at(runtime, frame_index, (u32)-1, &count);
	return count;
}

ls_string_view ls_debug_local_name(ls_runtime* runtime, u32 frame_index, u32 local_index) {
	ls_string_view empty = {NULL, 0};
	const ls_bytecode_local_debug_entry* entry = debug_local_at(runtime, frame_index, local_index, NULL);
	return entry ? entry->name : empty;
}

ls_type_kind ls_debug_local_kind(ls_runtime* runtime, u32 frame_index, u32 local_index) {
	const ls_bytecode_local_debug_entry* entry = debug_local_at(runtime, frame_index, local_index, NULL);
	return entry ? entry->kind : LS_TYPE_INVALID;
}

void* ls_debug_local_value(ls_runtime* runtime, u32 frame_index, u32 local_index, u32* size) {
	if (size) *size = 0u;
	const ls_bytecode_local_debug_entry* entry = debug_local_at(runtime, frame_index, local_index, NULL);
	if (!entry) return NULL;
	const runtime_call_frame* frame = debug_active_frame(runtime, frame_index);
	u8* value = frame->frame + entry->offset;
	if (value + entry->byte_size > runtime->stack_end) return NULL;
	if (size) *size = entry->byte_size;
	return value;
}

u32 ls_debug_global_count(ls_runtime* runtime) {
	return runtime->bytecode->global_debug_count;
}

ls_string_view ls_debug_global_name(ls_runtime* runtime, u32 global_index) {
	ls_string_view empty = {NULL, 0};
	if (global_index >= runtime->bytecode->global_debug_count) return empty;
	return runtime->bytecode->global_debug[global_index].name;
}

ls_type_kind ls_debug_global_kind(ls_runtime* runtime, u32 global_index) {
	if (global_index >= runtime->bytecode->global_debug_count) return LS_TYPE_INVALID;
	return runtime->bytecode->global_debug[global_index].kind;
}

void* ls_debug_global_value(ls_runtime* runtime, u32 global_index, u32* size) {
	if (size) *size = 0u;
	if (global_index >= runtime->bytecode->global_debug_count) return NULL;
	const ls_bytecode_global_debug_entry* entry = &runtime->bytecode->global_debug[global_index];
	if (runtime->stack + entry->offset + entry->byte_size > runtime->stack_end) return NULL;
	if (size) *size = entry->byte_size;
	return runtime->stack + entry->offset;
}

const ls_type* ls_debug_local_type(ls_runtime* runtime, u32 frame_index, u32 local_index) {
	const ls_bytecode_local_debug_entry* entry = debug_local_at(runtime, frame_index, local_index, NULL);
	if (!entry || entry->type_index >= runtime->bytecode->type_info_count) return NULL;
	return (const ls_type*)&runtime->bytecode->type_info[entry->type_index];
}

const ls_type* ls_debug_global_type(ls_runtime* runtime, u32 global_index) {
	if (global_index >= runtime->bytecode->global_debug_count) return NULL;
	const ls_bytecode_global_debug_entry* entry = &runtime->bytecode->global_debug[global_index];
	if (entry->type_index >= runtime->bytecode->type_info_count) return NULL;
	return (const ls_type*)&runtime->bytecode->type_info[entry->type_index];
}

ls_type_kind ls_type_get_kind(const ls_type* type) {
	return type ? type->kind : LS_TYPE_INVALID;
}

ls_string_view ls_type_get_name(const ls_type* type) {
	const ls_string_view empty = {NULL, 0};
	return type ? type->name : empty;
}

u32 ls_type_get_size(const ls_type* type) {
	return type ? type->byte_size : 0u;
}

u32 ls_type_get_alignment(const ls_type* type) {
	return type ? type->alignment : 0u;
}

u32 ls_type_struct_field_count(const ls_type* type) {
	return (type && type->kind == LS_TYPE_STRUCT) ? type->field_count : 0u;
}

ls_string_view ls_type_struct_field_name(const ls_type* type, u32 field_index) {
	const ls_string_view empty = {NULL, 0};
	if (!type || type->kind != LS_TYPE_STRUCT || !type->bytecode) return empty;
	if (field_index >= type->field_count) return empty;
	const u32 fi = type->first_field_index + field_index;
	if (fi >= type->bytecode->type_field_count) return empty;
	return type->bytecode->type_fields[fi].name;
}

const ls_type* ls_type_struct_field_type(const ls_type* type, u32 field_index) {
	if (!type || type->kind != LS_TYPE_STRUCT || !type->bytecode) return NULL;
	if (field_index >= type->field_count) return NULL;
	const u32 fi = type->first_field_index + field_index;
	if (fi >= type->bytecode->type_field_count) return NULL;
	const u32 ti = type->bytecode->type_fields[fi].type_index;
	if (ti >= type->bytecode->type_info_count) return NULL;
	return (const ls_type*)&type->bytecode->type_info[ti];
}

u32 ls_type_struct_field_offset(const ls_type* type, u32 field_index) {
	if (!type || type->kind != LS_TYPE_STRUCT || !type->bytecode) return 0u;
	if (field_index >= type->field_count) return 0u;
	const u32 fi = type->first_field_index + field_index;
	if (fi >= type->bytecode->type_field_count) return 0u;
	return type->bytecode->type_fields[fi].offset;
}

u32 ls_type_union_member_count(const ls_type* type) {
	return (type && type->kind == LS_TYPE_TAGGED_UNION) ? type->member_count : 0u;
}

const ls_type* ls_type_union_member_type(const ls_type* type, u32 member_index) {
	if (!type || type->kind != LS_TYPE_TAGGED_UNION || !type->bytecode) return NULL;
	if (member_index >= type->member_count) return NULL;
	const u32 mi = type->first_member_index + member_index;
	if (mi >= type->bytecode->type_member_count) return NULL;
	const u32 ti = type->bytecode->type_member_indices[mi];
	if (ti >= type->bytecode->type_info_count) return NULL;
	return (const ls_type*)&type->bytecode->type_info[ti];
}

i32 ls_type_union_tag(const ls_type* type, const void* value) {
	(void)type;
	if (!value) return -1;
	i32 tag;
	memcpy(&tag, value, sizeof(tag));
	return tag;
}

u32 ls_type_enum_value_count(const ls_type* type) {
	return (type && type->kind == LS_TYPE_ENUM) ? type->value_count : 0u;
}

ls_string_view ls_type_enum_value_name(const ls_type* type, u32 value_index) {
	const ls_string_view empty = {NULL, 0};
	if (!type || type->kind != LS_TYPE_ENUM || !type->bytecode) return empty;
	if (value_index >= type->value_count) return empty;
	const u32 vi = type->first_value_index + value_index;
	if (vi >= type->bytecode->type_enum_value_count) return empty;
	return type->bytecode->type_enum_values[vi].name;
}

i32 ls_type_enum_value_value(const ls_type* type, u32 value_index) {
	if (!type || type->kind != LS_TYPE_ENUM || !type->bytecode) return 0;
	if (value_index >= type->value_count) return 0;
	const u32 vi = type->first_value_index + value_index;
	if (vi >= type->bytecode->type_enum_value_count) return 0;
	return type->bytecode->type_enum_values[vi].value;
}

const ls_type* ls_type_array_element_type(const ls_type* type) {
	if (!type || !type->bytecode) return NULL;
	if (type->kind != LS_TYPE_ARRAY && type->kind != LS_TYPE_SLICE) return NULL;
	if (type->element_type_index >= type->bytecode->type_info_count) return NULL;
	return (const ls_type*)&type->bytecode->type_info[type->element_type_index];
}

u32 ls_type_array_length(const ls_type* type) {
	if (!type || type->kind != LS_TYPE_ARRAY) return 0u;
	return type->array_length;
}

bool ls_type_is_const(const ls_type* type) {
	return type && type->is_const;
}

const ls_type* ls_type_nullable_inner_type(const ls_type* type) {
	if (!type || !type->bytecode) return NULL;
	if (type->kind != LS_TYPE_NULLABLE) return NULL;
	if (type->element_type_index >= type->bytecode->type_info_count) return NULL;
	return (const ls_type*)&type->bytecode->type_info[type->element_type_index];
}

bool ls_type_nullable_is_null(const ls_type* type, const void* value) {
	(void)type;
	if (!value) return true;
	return *(const u8*)value == 0;
}

const void* ls_type_nullable_value_ptr(const ls_type* type, const void* value) {
	(void)type;
	if (!value) return NULL;
	return (const u8*)value + 1;
}
