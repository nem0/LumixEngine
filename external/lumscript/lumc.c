/*
 * lumc.c - Standalone LumScript runner/compiler
 * Usage: lumc [--dump-bytecode] <script.lum> [function_name] [args...]
 *
 * Compiles and runs a LumScript file. If function_name is provided,
 * calls that function with the remaining arguments. Otherwise, calls main().
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "arena.h"
#include "bytecode.h"
#include "capi.h"

typedef struct lumc_context {
	ls_host host;
	char* source;
	ls_module* module;
	ls_bytecode* bytecode;
	ls_runtime* runtime;
} lumc_context;

static const ls_host g_host_template = {
	{NULL, NULL, NULL},
	NULL,
	NULL
};

static ls_string_view ls_from_cstr(const char* str) {
	return (ls_string_view){str, str ? str + strlen(str) : NULL};
}

static ls_unit* lumc_find_native_function(ls_module* module, const char* name, int* out_function_index) {
	const size_t name_size = strlen(name);
	for (int unit_index = 0, unit_count = ls_module_get_unit_count(module); unit_index < unit_count; ++unit_index) {
		ls_unit* unit = ls_module_get_unit(module, unit_index);
		for (int function_index = 0, function_count = ls_unit_get_native_function_count(unit); function_index < function_count; ++function_index) {
			const ls_string_view function_name = ls_unit_get_native_function_name(unit, function_index);
			if ((size_t)(function_name.end - function_name.begin) != name_size) continue;
			if (memcmp(function_name.begin, name, name_size) == 0) {
				*out_function_index = function_index;
				return unit;
			}
		}
	}
	return NULL;
}

static void lumc_print_string(FILE* out, ls_string_view value) {
	if (!value.begin || !value.end || value.end < value.begin) return;
	fwrite(value.begin, 1, (size_t)(value.end - value.begin), out);
}

static void lumc_write_result(FILE* out, ls_runtime* runtime, ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_VOID:
			break;
		case LS_TYPE_BOOL:
			fputs(ls_to_bool(runtime, -1) ? "true" : "false", out);
			break;
		case LS_TYPE_I8:
		case LS_TYPE_I16:
		case LS_TYPE_I32:
		case LS_TYPE_ENUM:
		case LS_TYPE_UNTYPED_INT:
			fprintf(out, "%d", ls_to_i32(runtime, -1));
			break;
		case LS_TYPE_U8:
		case LS_TYPE_U16:
		case LS_TYPE_U32:
			fprintf(out, "%u", ls_to_u32(runtime, -1));
			break;
		case LS_TYPE_I64:
			fprintf(out, "%lld", (long long)ls_to_i64(runtime, -1));
			break;
		case LS_TYPE_U64:
			fprintf(out, "%llu", (unsigned long long)ls_to_u64(runtime, -1));
			break;
		case LS_TYPE_F32:
			fprintf(out, "%f", ls_to_f32(runtime, -1));
			break;
		case LS_TYPE_F64:
			fprintf(out, "%lf", ls_to_f64(runtime, -1));
			break;
		default:
			fprintf(out, "<%d>", (int)kind);
			break;
	}
}

static void lumc_diagnostics_print(void* userdata, ls_string_view msg) {
	(void)userdata;
	ls_host* host = (ls_host*)userdata;
	(void)host;
	lumc_print_string(stderr, msg);
}

static void lumc_native_print(ls_runtime* runtime, ls_call_frame frame) {
	(void)runtime;
	LS_STRING_ARG(frame, val);
	lumc_print_string(stdout, val);
	putchar('\n');
}

static const char* lumc_type_name(ls_type_kind kind) {
	switch (kind) {
		case LS_TYPE_VOID: return "void";
		case LS_TYPE_BOOL: return "bool";
		case LS_TYPE_I8: return "i8";
		case LS_TYPE_I16: return "i16";
		case LS_TYPE_I32: return "i32";
		case LS_TYPE_I64: return "i64";
		case LS_TYPE_U8: return "u8";
		case LS_TYPE_U16: return "u16";
		case LS_TYPE_U32: return "u32";
		case LS_TYPE_U64: return "u64";
		case LS_TYPE_F32: return "f32";
		case LS_TYPE_F64: return "f64";
		case LS_TYPE_ENUM: return "enum";
		case LS_TYPE_FUNCTION: return "function";
		case LS_TYPE_CPTR: return "cptr";
		case LS_TYPE_SLICE: return "slice";
		default: return "invalid";
	}
}

static const char* lumc_opcode_name(ls_op op) {
	static const char* const unary[] = {
		"NEG_I8", "NEG_U8", "NEG_I16", "NEG_U16", "NEG_I32", "NEG_U32",
		"NEG_I64", "NEG_U64", "NEG_F32", "NEG_F64"
	};
	static const char* const add[] = {"ADD_8", "ADD_16", "ADD_32", "ADD_64", "ADD_F32", "ADD_F64"};
	static const char* const sub[] = {"SUB_8", "SUB_16", "SUB_32", "SUB_64", "SUB_F32", "SUB_F64"};
	static const char* const mul[] = {"MUL_8", "MUL_16", "MUL_32", "MUL_64", "MUL_F32", "MUL_F64"};
	static const char* const div[] = {"DIV_I8", "DIV_U8", "DIV_I16", "DIV_U16", "DIV_I32", "DIV_U32", "DIV_I64", "DIV_U64", "DIV_F32", "DIV_F64"};
	static const char* const mod[] = {"MOD_I8", "MOD_U8", "MOD_I16", "MOD_U16", "MOD_I32", "MOD_U32", "MOD_I64", "MOD_U64"};
	static const char* const add_imm[] = {"ADD_8_IMM", "ADD_16_IMM", "ADD_32_IMM", "ADD_64_IMM", "ADD_F32_IMM", "ADD_F64_IMM"};
	static const char* const sub_imm[] = {"SUB_8_IMM", "SUB_16_IMM", "SUB_32_IMM", "SUB_64_IMM", "SUB_F32_IMM", "SUB_F64_IMM"};
	static const char* const mul_imm[] = {"MUL_8_IMM", "MUL_16_IMM", "MUL_32_IMM", "MUL_64_IMM", "MUL_F32_IMM", "MUL_F64_IMM"};
	static const char* const div_imm[] = {"DIV_I8_IMM", "DIV_U8_IMM", "DIV_I16_IMM", "DIV_U16_IMM", "DIV_I32_IMM", "DIV_U32_IMM", "DIV_I64_IMM", "DIV_U64_IMM", "DIV_F32_IMM", "DIV_F64_IMM"};
	static const char* const mod_imm[] = {"MOD_I8_IMM", "MOD_U8_IMM", "MOD_I16_IMM", "MOD_U16_IMM", "MOD_I32_IMM", "MOD_U32_IMM", "MOD_I64_IMM", "MOD_U64_IMM"};
	if (op >= LS_OP_NEG_I8 && op <= LS_OP_NEG_F64) return unary[(u32)op - (u32)LS_OP_NEG_I8];
	if (op >= LS_OP_ADD_8 && op <= LS_OP_ADD_F64) return add[(u32)op - (u32)LS_OP_ADD_8];
	if (op >= LS_OP_SUB_8 && op <= LS_OP_SUB_F64) return sub[(u32)op - (u32)LS_OP_SUB_8];
	if (op >= LS_OP_MUL_8 && op <= LS_OP_MUL_F64) return mul[(u32)op - (u32)LS_OP_MUL_8];
	if (op >= LS_OP_DIV_I8 && op <= LS_OP_DIV_F64) return div[(u32)op - (u32)LS_OP_DIV_I8];
	if (op >= LS_OP_MOD_I8 && op <= LS_OP_MOD_U64) return mod[(u32)op - (u32)LS_OP_MOD_I8];
	if (op >= LS_OP_ADD_8_IMM && op <= LS_OP_ADD_F64_IMM) return add_imm[(u32)op - (u32)LS_OP_ADD_8_IMM];
	if (op >= LS_OP_SUB_8_IMM && op <= LS_OP_SUB_F64_IMM) return sub_imm[(u32)op - (u32)LS_OP_SUB_8_IMM];
	if (op >= LS_OP_MUL_8_IMM && op <= LS_OP_MUL_F64_IMM) return mul_imm[(u32)op - (u32)LS_OP_MUL_8_IMM];
	if (op >= LS_OP_DIV_I8_IMM && op <= LS_OP_DIV_F64_IMM) return div_imm[(u32)op - (u32)LS_OP_DIV_I8_IMM];
	if (op >= LS_OP_MOD_I8_IMM && op <= LS_OP_MOD_U64_IMM) return mod_imm[(u32)op - (u32)LS_OP_MOD_I8_IMM];
	switch (op) {
		case LS_OP_LOAD_CONST_1: return "LOAD_CONST_1";
		case LS_OP_LOAD_CONST_2: return "LOAD_CONST_2";
		case LS_OP_LOAD_CONST_4: return "LOAD_CONST_4";
		case LS_OP_LOAD_CONST_8: return "LOAD_CONST_8";
		case LS_OP_STRING_SLICE: return "STRING_SLICE";
		case LS_OP_COPY: return "COPY";
		case LS_OP_FRAME_PTR: return "FRAME_PTR";
		case LS_OP_GLOBAL_PTR: return "GLOBAL_PTR";
		case LS_OP_LOAD_PTR: return "LOAD_PTR";
		case LS_OP_STORE_PTR: return "STORE_PTR";
		case LS_OP_LOAD_INDEXED_8: return "LOAD_INDEXED_8";
		case LS_OP_LOAD_INDEXED_16: return "LOAD_INDEXED_16";
		case LS_OP_LOAD_INDEXED_32: return "LOAD_INDEXED_32";
		case LS_OP_LOAD_INDEXED_64: return "LOAD_INDEXED_64";
		case LS_OP_STORE_INDEXED_8: return "STORE_INDEXED_8";
		case LS_OP_STORE_INDEXED_16: return "STORE_INDEXED_16";
		case LS_OP_STORE_INDEXED_32: return "STORE_INDEXED_32";
		case LS_OP_STORE_INDEXED_64: return "STORE_INDEXED_64";
		case LS_OP_BOUNDS_CHECK: return "BOUNDS_CHECK";
		case LS_OP_SLICE: return "SLICE";
		case LS_OP_SLICE_LOAD: return "SLICE_LOAD";
		case LS_OP_SLICE_REF: return "SLICE_REF";
		case LS_OP_SLICE_FIELD_LOAD_8: return "SLICE_FIELD_LOAD_8";
		case LS_OP_SLICE_FIELD_LOAD_16: return "SLICE_FIELD_LOAD_16";
		case LS_OP_SLICE_FIELD_LOAD_32: return "SLICE_FIELD_LOAD_32";
		case LS_OP_SLICE_FIELD_LOAD_64: return "SLICE_FIELD_LOAD_64";
		case LS_OP_SLICE_FIELD_STORE_8: return "SLICE_FIELD_STORE_8";
		case LS_OP_SLICE_FIELD_STORE_16: return "SLICE_FIELD_STORE_16";
		case LS_OP_SLICE_FIELD_STORE_32: return "SLICE_FIELD_STORE_32";
		case LS_OP_SLICE_FIELD_STORE_64: return "SLICE_FIELD_STORE_64";
		case LS_OP_SLICE_LENGTH: return "SLICE_LENGTH";
		case LS_OP_SLICE_EQ: return "SLICE_EQ";
		case LS_OP_INC_I32: return "INC_I32";
		case LS_OP_INC_I64: return "INC_I64";
		case LS_OP_DEC_I32: return "DEC_I32";
		case LS_OP_DEC_I64: return "DEC_I64";
		case LS_OP_NOT: return "NOT";
		case LS_OP_EQ: return "EQ";
		case LS_OP_NE: return "NE";
		case LS_OP_LT: return "LT";
		case LS_OP_LE: return "LE";
		case LS_OP_GT: return "GT";
		case LS_OP_GE: return "GE";
		#define LS_COMPARE_JUMP_NAME(TYPE) \
			case LS_OP_JE_##TYPE: return "JE_" #TYPE; \
			case LS_OP_JGE_##TYPE: return "JGE_" #TYPE; \
			case LS_OP_JGT_##TYPE: return "JGT_" #TYPE; \
			case LS_OP_JLT_##TYPE: return "JLT_" #TYPE; \
			case LS_OP_JLE_##TYPE: return "JLE_" #TYPE; \
			case LS_OP_JNE_##TYPE: return "JNE_" #TYPE;
		LS_COMPARE_JUMP_NAME(I8)
		LS_COMPARE_JUMP_NAME(U8)
		LS_COMPARE_JUMP_NAME(I16)
		LS_COMPARE_JUMP_NAME(U16)
		LS_COMPARE_JUMP_NAME(I32)
		LS_COMPARE_JUMP_NAME(U32)
		LS_COMPARE_JUMP_NAME(I64)
		LS_COMPARE_JUMP_NAME(U64)
		LS_COMPARE_JUMP_NAME(F32)
		LS_COMPARE_JUMP_NAME(F64)
		#undef LS_COMPARE_JUMP_NAME
		case LS_OP_JUMP: return "JUMP";
		case LS_OP_JZ_U8: return "JZ_U8";
		case LS_OP_JNZ_U8: return "JNZ_U8";
		case LS_OP_JZ_I32: return "JZ_I32";
		case LS_OP_JZ_I64: return "JZ_I64";
		case LS_OP_JNZ_I32: return "JNZ_I32";
		case LS_OP_JNZ_I64: return "JNZ_I64";
		case LS_OP_JGZ_I32: return "JGZ_I32";
		case LS_OP_JGZ_I64: return "JGZ_I64";
		case LS_OP_JGEZ_I32: return "JGEZ_I32";
		case LS_OP_JGEZ_I64: return "JGEZ_I64";
		case LS_OP_JLTZ_I32: return "JLTZ_I32";
		case LS_OP_JLTZ_I64: return "JLTZ_I64";
		case LS_OP_JLEZ_I32: return "JLEZ_I32";
		case LS_OP_JLEZ_I64: return "JLEZ_I64";
		case LS_OP_CALL_DIRECT: return "CALL_DIRECT";
		case LS_OP_CALL_NATIVE: return "CALL_NATIVE";
		case LS_OP_CALL_INDIRECT: return "CALL_INDIRECT";
		case LS_OP_CAST: return "CAST";
		case LS_OP_RETURN: return "RETURN";
		case LS_OP_RETURN_BASE: return "RETURN_BASE";
		default: return "UNKNOWN";
	}
}

static u32 lumc_immediate_size(ls_op op) {
	if ((op >= LS_OP_ADD_8_IMM && op <= LS_OP_ADD_F64_IMM)
		|| (op >= LS_OP_SUB_8_IMM && op <= LS_OP_SUB_F64_IMM)
		|| (op >= LS_OP_MUL_8_IMM && op <= LS_OP_MUL_F64_IMM)
		|| (op >= LS_OP_DIV_I8_IMM && op <= LS_OP_DIV_F64_IMM)) {
		const u32 index = op >= LS_OP_DIV_I8_IMM ? (u32)(op - LS_OP_DIV_I8_IMM) % 10u : (op >= LS_OP_MUL_8_IMM ? (u32)(op - LS_OP_MUL_8_IMM) % 6u : op >= LS_OP_SUB_8_IMM ? (u32)(op - LS_OP_SUB_8_IMM) % 6u : (u32)(op - LS_OP_ADD_8_IMM));
		return index < 1 ? 1u : index < 2 ? 2u : index < 3 ? 4u : index < 4 ? 8u : index == 4 ? 4u : 8u;
	}
	if (op >= LS_OP_MOD_I8_IMM && op <= LS_OP_MOD_U64_IMM) {
		const u32 index = (u32)(op - LS_OP_MOD_I8_IMM);
		return index < 2 ? 1u : index < 4 ? 2u : index < 6 ? 4u : 8u;
	}
	return 0;
}

static u32 lumc_read_u32(const u8* code, u32 size, u32* pc) {
	u32 value = 0;
	if (*pc + sizeof(value) <= size) memcpy(&value, code + *pc, sizeof(value));
	*pc += (u32)sizeof(value);
	return value;
}

static i32 lumc_read_i32(const u8* code, u32 size, u32* pc) {
	i32 value = 0;
	if (*pc + sizeof(value) <= size) memcpy(&value, code + *pc, sizeof(value));
	*pc += (u32)sizeof(value);
	return value;
}

static i32 lumc_read_i16(const u8* code, u32 size, u32* pc) {
	i16 value = 0;
	if (*pc + sizeof(value) <= size) memcpy(&value, code + *pc, sizeof(value));
	*pc += (u32)sizeof(value);
	return (i32)value;
}

static u64 lumc_read_u64(const u8* code, u32 size, u32* pc) {
	u64 value = 0;
	if (*pc + sizeof(value) <= size) memcpy(&value, code + *pc, sizeof(value));
	*pc += (u32)sizeof(value);
	return value;
}

static const ls_bytecode_source_map_entry* lumc_source_at(const ls_function_bc* fn, u32 code_offset) {
	const ls_bytecode_source_map_entry* result = NULL;
	for (u32 i = 0; i < fn->source_map_count; ++i) {
		const ls_bytecode_source_map_entry* entry = &fn->source_map[i];
		if (entry->code_offset > code_offset) break;
		result = entry;
	}
	return result;
}

static void lumc_append_source_line(char* output, size_t* output_size, size_t output_capacity, const char* source, u32 line) {
	const char* begin = source;
	if (!begin || line == 0) return;
	for (u32 current = 1; current < line && *begin; ++current) {
		while (*begin && *begin != '\n') ++begin;
		if (*begin == '\n') ++begin;
	}
	if (!*begin) return;
	const char* end = begin;
	while (*end && *end != '\r' && *end != '\n') ++end;
	if (*output_size < output_capacity) {
		int written = snprintf(output + *output_size, output_capacity - *output_size, " // %.*s", (int)(end - begin), begin);
		if (written > 0) *output_size += (size_t)written;
	}
}

static void lumc_appendf(char* output, size_t* output_size, size_t output_capacity, const char* format, ...) {
	if (*output_size >= output_capacity) return;
	va_list args;
	va_start(args, format);
	int written = vsnprintf(output + *output_size, output_capacity - *output_size, format, args);
	va_end(args);
	if (written > 0) *output_size += (size_t)written;
}

static void lumc_dump_bytecode(const ls_bytecode* bytecode, const char* source_text) {
	for (u32 function_index = 0; function_index < bytecode->function_count; ++function_index) {
		const ls_function_bc* fn = &bytecode->functions[function_index];
		printf("function %u %.*s (%s, params=%u, frame=%u, returns=%u)\n",
			function_index,
			fn->name.begin ? (int)(fn->name.end - fn->name.begin) : 0,
			fn->name.begin ? fn->name.begin : "<global_init>",
			fn->kind == LS_FUNCTION_NATIVE ? "native" : "script",
			fn->param_size, fn->frame_size, fn->return_size);

		for (u32 pc = 0; pc < fn->code_size;) {
			char line[4096];
			size_t line_size = 0;
			const u32 offset = pc;
			const ls_op op = (ls_op)fn->code[pc++];
			const ls_bytecode_source_map_entry* source = lumc_source_at(fn, offset);
			#define printf(...) lumc_appendf(line, &line_size, sizeof(line), __VA_ARGS__)
			printf("  %04u: %-20s", offset, lumc_opcode_name(op));
			switch (op) {
			case LS_OP_LOAD_CONST_1:
			case LS_OP_LOAD_CONST_2:
			case LS_OP_LOAD_CONST_4:
			case LS_OP_LOAD_CONST_8: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, value=", dst);
				const u32 width = 1u << ((u32)op - (u32)LS_OP_LOAD_CONST_1);
				for (u32 i = 0; i < width && pc + i < fn->code_size; ++i) printf(" %02x", fn->code[pc + i]);
				pc += width;
				break;
			}
			case LS_OP_COPY: {
				const u32 a = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 b = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 c = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, src=%u, size=%u", a, b, c);
				break;
			}
			case LS_OP_FRAME_PTR:
			case LS_OP_GLOBAL_PTR: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 off = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, offset=%u", dst, off);
				break;
			}
			case LS_OP_LOAD_PTR:
			case LS_OP_STORE_PTR: {
				const u32 addr = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 src = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 size = lumc_read_u32(fn->code, fn->code_size, &pc);
				if (op == LS_OP_LOAD_PTR) printf(" dst=%u, addr=%u, size=%u", addr, src, size);
				else printf(" addr=%u, src=%u, size=%u", addr, src, size);
				break;
			}
			case LS_OP_STRING_SLICE: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 string = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, string=%u", dst, string);
				break;
			}
			case LS_OP_LOAD_INDEXED_8:
			case LS_OP_LOAD_INDEXED_16:
			case LS_OP_LOAD_INDEXED_32:
			case LS_OP_LOAD_INDEXED_64:
			case LS_OP_STORE_INDEXED_8:
			case LS_OP_STORE_INDEXED_16:
			case LS_OP_STORE_INDEXED_32:
			case LS_OP_STORE_INDEXED_64: {
				const u32 a = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 b = lumc_read_u32(fn->code, fn->code_size, &pc);
				const bool is_load = op <= LS_OP_LOAD_INDEXED_64;
				const u64 index = is_load ? lumc_read_u32(fn->code, fn->code_size, &pc) : b;
				const u64 length = lumc_read_u64(fn->code, fn->code_size, &pc);
				const u32 size = lumc_read_u32(fn->code, fn->code_size, &pc);
				if (is_load) printf(" dst=%u, base=%u, index=%u, length=%llu, size=%u", a, b, (unsigned)index, (unsigned long long)length, size);
				else {
					const u32 src = lumc_read_u32(fn->code, fn->code_size, &pc);
					printf(" base=%u, index=%u, length=%llu, size=%u, src=%u", a, b, (unsigned long long)length, size, src);
				}
				break;
			}
			case LS_OP_BOUNDS_CHECK: {
				const u32 index = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u8 index_kind = fn->code[pc++];
				const u64 length = lumc_read_u64(fn->code, fn->code_size, &pc);
				printf(" index=%u, type=%u, length=%llu", index, index_kind, (unsigned long long)length);
				break;
			}
			case LS_OP_SLICE_EQ: {
				const u32 result = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 lhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 rhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 size = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u8 kind = fn->code[pc++];
				printf(" dst=%u, lhs=%u, rhs=%u, size=%u, type=%u", result, lhs, rhs, size, kind);
				break;
			}
			case LS_OP_SLICE:
			case LS_OP_SLICE_LOAD:
			case LS_OP_SLICE_REF:
			case LS_OP_SLICE_FIELD_LOAD_8:
			case LS_OP_SLICE_FIELD_LOAD_16:
			case LS_OP_SLICE_FIELD_LOAD_32:
			case LS_OP_SLICE_FIELD_LOAD_64:
			case LS_OP_SLICE_FIELD_STORE_8:
			case LS_OP_SLICE_FIELD_STORE_16:
			case LS_OP_SLICE_FIELD_STORE_32:
			case LS_OP_SLICE_FIELD_STORE_64:
			case LS_OP_SLICE_LENGTH: {
				if (op == LS_OP_SLICE_FIELD_LOAD_8 || op == LS_OP_SLICE_FIELD_LOAD_16 || op == LS_OP_SLICE_FIELD_LOAD_32 || op == LS_OP_SLICE_FIELD_LOAD_64) {
					const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 slice = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 index = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 element_size = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 field_offset = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 field_size = lumc_read_u32(fn->code, fn->code_size, &pc);
					printf(" dst=%u, slice=%u, index=%u, element_size=%u, field_offset=%u, field_size=%u", dst, slice, index, element_size, field_offset, field_size);
					break;
				}
				if (op == LS_OP_SLICE_FIELD_STORE_8 || op == LS_OP_SLICE_FIELD_STORE_16 || op == LS_OP_SLICE_FIELD_STORE_32 || op == LS_OP_SLICE_FIELD_STORE_64) {
					const u32 slice = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 index = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 element_size = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 field_offset = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 field_size = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 src = lumc_read_u32(fn->code, fn->code_size, &pc);
					printf(" slice=%u, index=%u, element_size=%u, field_offset=%u, field_size=%u, src=%u", slice, index, element_size, field_offset, field_size, src);
					break;
				}
				if (op == LS_OP_SLICE_LOAD) {
					const u32 a = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 b = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 c = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u8 index_type = fn->code[pc++];
					const u32 size = lumc_read_u32(fn->code, fn->code_size, &pc);
					printf(" dst=%u, slice=%u, index=%u, index_type=%u, size=%u", a, b, c, index_type, size);
					break;
				}
				if (op == LS_OP_SLICE_REF) {
					const u32 a = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u32 b = lumc_read_u32(fn->code, fn->code_size, &pc);
					const u8 index_type = fn->code[pc++];
					const u32 size = lumc_read_u32(fn->code, fn->code_size, &pc);
					printf(" slice=%u, index=%u, index_type=%u, element_size=%u", a, b, index_type, size);
					break;
				}
				if (op == LS_OP_SLICE_LENGTH) {
					printf(" dst=%u, slice=%u", lumc_read_u32(fn->code, fn->code_size, &pc), lumc_read_u32(fn->code, fn->code_size, &pc));
					break;
				}
				const u32 count = op == LS_OP_SLICE ? 4u : 1u;
				for (u32 i = 0; i < count; ++i) printf(" arg%u=%u", i, lumc_read_u32(fn->code, fn->code_size, &pc));
				break;
			}
			case LS_OP_ADD_8: case LS_OP_ADD_16: case LS_OP_ADD_32: case LS_OP_ADD_64: case LS_OP_ADD_F32: case LS_OP_ADD_F64:
			case LS_OP_SUB_8: case LS_OP_SUB_16: case LS_OP_SUB_32: case LS_OP_SUB_64: case LS_OP_SUB_F32: case LS_OP_SUB_F64:
			case LS_OP_MUL_8: case LS_OP_MUL_16: case LS_OP_MUL_32: case LS_OP_MUL_64: case LS_OP_MUL_F32: case LS_OP_MUL_F64:
			case LS_OP_DIV_I8: case LS_OP_DIV_U8: case LS_OP_DIV_I16: case LS_OP_DIV_U16: case LS_OP_DIV_I32: case LS_OP_DIV_U32: case LS_OP_DIV_I64: case LS_OP_DIV_U64: case LS_OP_DIV_F32: case LS_OP_DIV_F64:
			case LS_OP_MOD_I8: case LS_OP_MOD_U8: case LS_OP_MOD_I16: case LS_OP_MOD_U16: case LS_OP_MOD_I32: case LS_OP_MOD_U32: case LS_OP_MOD_I64: case LS_OP_MOD_U64: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 lhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 rhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, lhs=%u, rhs=%u", dst, lhs, rhs);
				break;
			}
			#define LS_ARITH_INT_IMM_DISASM_CASES(OP) \
				case LS_OP_##OP##_8_IMM: case LS_OP_##OP##_16_IMM: case LS_OP_##OP##_32_IMM: case LS_OP_##OP##_64_IMM: \
				case LS_OP_##OP##_F32_IMM: case LS_OP_##OP##_F64_IMM:
			#define LS_ARITH_IMM_DISASM_CASES(OP) \
				case LS_OP_##OP##_I8_IMM: case LS_OP_##OP##_U8_IMM: case LS_OP_##OP##_I16_IMM: case LS_OP_##OP##_U16_IMM: \
				case LS_OP_##OP##_I32_IMM: case LS_OP_##OP##_U32_IMM: case LS_OP_##OP##_I64_IMM: case LS_OP_##OP##_U64_IMM: \
				case LS_OP_##OP##_F32_IMM: case LS_OP_##OP##_F64_IMM:
			LS_ARITH_INT_IMM_DISASM_CASES(ADD)
			LS_ARITH_INT_IMM_DISASM_CASES(SUB)
			LS_ARITH_INT_IMM_DISASM_CASES(MUL)
			LS_ARITH_IMM_DISASM_CASES(DIV)
			case LS_OP_MOD_I8_IMM: case LS_OP_MOD_U8_IMM: case LS_OP_MOD_I16_IMM: case LS_OP_MOD_U16_IMM:
			case LS_OP_MOD_I32_IMM: case LS_OP_MOD_U32_IMM: case LS_OP_MOD_I64_IMM: case LS_OP_MOD_U64_IMM: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 lhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 size = lumc_immediate_size(op);
				printf(" dst=%u, lhs=%u, imm=0x", dst, lhs);
				for (u32 i = 0; i < size; ++i) printf("%02x", fn->code[pc++]);
				break;
			}
			#undef LS_ARITH_IMM_DISASM_CASES
			#undef LS_ARITH_INT_IMM_DISASM_CASES
			case LS_OP_INC_I32:
			case LS_OP_INC_I64:
			case LS_OP_DEC_I32:
			case LS_OP_DEC_I64:
				printf(" dst=%u", lumc_read_u32(fn->code, fn->code_size, &pc));
				break;
			case LS_OP_EQ:
			case LS_OP_NE:
			case LS_OP_LT:
			case LS_OP_LE:
			case LS_OP_GT:
			case LS_OP_GE: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 lhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 rhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const ls_type_kind kind = (ls_type_kind)fn->code[pc++];
				printf(" dst=%u, lhs=%u, rhs=%u, type=%s", dst, lhs, rhs, lumc_type_name(kind));
				break;
			}
			#define LS_COMPARE_JUMP_CASES(TYPE) \
				case LS_OP_JE_##TYPE: \
				case LS_OP_JGE_##TYPE: \
				case LS_OP_JGT_##TYPE: \
				case LS_OP_JLT_##TYPE: \
				case LS_OP_JLE_##TYPE: \
				case LS_OP_JNE_##TYPE:
			LS_COMPARE_JUMP_CASES(I8)
			LS_COMPARE_JUMP_CASES(U8)
			LS_COMPARE_JUMP_CASES(I16)
			LS_COMPARE_JUMP_CASES(U16)
			LS_COMPARE_JUMP_CASES(I32)
			LS_COMPARE_JUMP_CASES(U32)
			LS_COMPARE_JUMP_CASES(I64)
			LS_COMPARE_JUMP_CASES(U64)
			LS_COMPARE_JUMP_CASES(F32)
			LS_COMPARE_JUMP_CASES(F64)
			#undef LS_COMPARE_JUMP_CASES
			{
				const u32 lhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 rhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const i32 jump_offset = lumc_read_i16(fn->code, fn->code_size, &pc);
				printf(" lhs=%u, rhs=%u, offset=%d, target=%d", lhs, rhs, jump_offset, (i32)pc + jump_offset);
				break;
			}
			case LS_OP_JUMP: {
				const i32 jump_offset = lumc_read_i16(fn->code, fn->code_size, &pc);
				printf(" offset=%d, target=%d", jump_offset, (i32)pc + jump_offset);
				break;
			}
			case LS_OP_JZ_U8:
			case LS_OP_JNZ_U8: {
				const u32 condition = lumc_read_u32(fn->code, fn->code_size, &pc);
				const i32 jump_offset = lumc_read_i16(fn->code, fn->code_size, &pc);
				printf(" cond=%u, offset=%d, target=%d", condition, jump_offset, (i32)pc + jump_offset);
				break;
			}
			case LS_OP_JZ_I32:
			case LS_OP_JGZ_I32:
			case LS_OP_JZ_I64:
			case LS_OP_JNZ_I32:
			case LS_OP_JNZ_I64:
			case LS_OP_JGZ_I64:
			case LS_OP_JGEZ_I32:
			case LS_OP_JGEZ_I64:
			case LS_OP_JLTZ_I32:
			case LS_OP_JLTZ_I64:
			case LS_OP_JLEZ_I32:
			case LS_OP_JLEZ_I64: {
				const u32 reg = lumc_read_u32(fn->code, fn->code_size, &pc);
				const i32 jump_offset = lumc_read_i16(fn->code, fn->code_size, &pc);
				printf(" reg=%u, offset=%d, target=%d", reg, jump_offset, (i32)pc + jump_offset);
				break;
			}
			case LS_OP_CALL_DIRECT: {
				const u32 callee = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 arg_base = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" function=%u, arg_base=%u", callee, arg_base);
				break;
			}
			case LS_OP_CALL_NATIVE: {
				const u32 callee = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 arg_base = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" function=%u, arg_base=%u", callee, arg_base);
				break;
			}
			case LS_OP_CALL_INDIRECT: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 arg_size = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 return_size = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, arg_size=%u, return_size=%u", dst, arg_size, return_size);
				break;
			}
			case LS_OP_CAST: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 src = lumc_read_u32(fn->code, fn->code_size, &pc);
				const ls_type_kind src_kind = (ls_type_kind)fn->code[pc++];
				const ls_type_kind dst_kind = (ls_type_kind)fn->code[pc++];
				printf(" dst=%u, src=%u, %s -> %s", dst, src, lumc_type_name(src_kind), lumc_type_name(dst_kind));
				break;
			}
			case LS_OP_RETURN: {
				const u32 src = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 return_size = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" src=%u, size=%u", src, return_size);
				break;
			}
			case LS_OP_RETURN_BASE: {
				break;
			}
			case LS_OP_NOT:
			case LS_OP_NEG_I8: case LS_OP_NEG_U8: case LS_OP_NEG_I16: case LS_OP_NEG_U16: case LS_OP_NEG_I32: case LS_OP_NEG_U32: case LS_OP_NEG_I64: case LS_OP_NEG_U64: case LS_OP_NEG_F32: case LS_OP_NEG_F64: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 src = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, src=%u", dst, src);
				break;
			}
			default:
				break;
			}
			if (source) {
				const size_t source_column = 120;
				while (line_size < source_column && line_size + 1 < sizeof(line)) line[line_size++] = ' ';
				if (source->location_index < bytecode->location_count) {
					lumc_append_source_line(line, &line_size, sizeof(line), source_text, bytecode->locations[source->location_index].line);
				}
			}
			fputs(line, stdout);
			putchar('\n');
		}
	}
}

#undef printf

int main(int argc, char** argv) {
	if (argc < 2) {
		fputs("Usage: lumc [--dump-bytecode] <script.lum> [function_name] [args...]\n"
			"  --dump-bytecode  Compile and print human-readable bytecode\n"
			"  script.lum     - Path to LumScript source file\n"
			"  function_name  - Function to call (default: main)\n"
			"  args           - Arguments passed to the function\n", stderr);
		return 1;
	}

	lumc_context ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.host = g_host_template;
	ls_default_arena_create(&ctx.host.arena);
	ctx.host.diagnostics_userdata = &ctx.host;
	ctx.host.print = &lumc_diagnostics_print;

	int dump_bytecode = 0;
	int script_arg = 1;
	while (script_arg < argc) {
		if (strcmp(argv[script_arg], "--dump-bytecode") == 0) {
			dump_bytecode = 1;
			++script_arg;
		} else {
			break;
		}
	}
	if (argc <= script_arg) {
		fputs("Error: Missing script path\n", stderr);
		return 1;
	}
	const char* script_path = argv[script_arg];
	const char* function_name = argc > script_arg + 1 ? argv[script_arg + 1] : "main";
	int rc = 1;

	FILE* f = fopen(script_path, "rb");
	if (!f) {
		ctx.source = NULL;
	} else {
		long size;
		if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
			fclose(f);
			ctx.source = NULL;
		} else {
			ctx.source = (char*)malloc((size_t)size + 1);
			if (!ctx.source) {
				fclose(f);
			} else {
				size_t read = fread(ctx.source, 1, (size_t)size, f);
				fclose(f);
				ctx.source[read] = '\0';
			}
		}
	}
	if (!ctx.source) {
		fprintf(stderr, "Error: Cannot read file '%s'\n", script_path);
		goto cleanup;
	}

	ctx.module = ls_module_create(&ctx.host);
	if (!ctx.module) {
		fprintf(stderr, "Error: Failed to create LumScript module\n");
		goto cleanup;
	}

	if (!ls_module_compile(
		ctx.module,
		ls_from_cstr(ctx.source),
		ls_from_cstr(script_path),
		NULL,
		NULL
	)) {
		fprintf(stderr, "Compile error\n");
		goto cleanup;
	}

	ls_bytecode_compile_options opts = {.optimize = true};
	ctx.bytecode = ls_bytecode_compile(ctx.module, &ctx.host, &opts);
	if (!ctx.bytecode) {
		fputs("Error: Failed to compile IR bytecode\n", stderr);
		goto cleanup;
	}
	if (dump_bytecode) {
		lumc_dump_bytecode(ctx.bytecode, ctx.source);
		rc = 0;
		goto cleanup;
	}

	ctx.runtime = ls_runtime_create(ctx.bytecode, &ctx.host);
	if (!ctx.runtime) {
		fprintf(stderr, "Error: Failed to create bytecode runtime\n");
		goto cleanup;
	}

	int native_print = -1;
	ls_unit* native_print_unit = lumc_find_native_function(ctx.module, "print", &native_print);
	if (native_print_unit) {
		if (!ls_runtime_set_native_function_callback(ctx.runtime, native_print_unit, native_print, &lumc_native_print)) {
			fprintf(stderr, "Error: Failed to bind native print\n");
			goto cleanup;
		}
	}

	const int first_call_arg = script_arg + 2;
	size_t call_arg_count = argc > first_call_arg ? (size_t)(argc - first_call_arg) : 0;
	{
		for (size_t i = 0; i < call_arg_count; ++i) {
			char* end = NULL;
			const char* arg = argv[i + first_call_arg];
			double d = strtod(arg, &end);
			if (end != arg && *end == '\0') {
				if (strpbrk(arg, ".eE")) {
					ls_push_f64(ctx.runtime, d);
				} else {
					ls_push_i64(ctx.runtime, (i64)d);
				}
			} else if (strcmp(arg, "true") == 0) {
				ls_push_bool(ctx.runtime, 1);
			} else if (strcmp(arg, "false") == 0) {
				ls_push_bool(ctx.runtime, 0);
			} else {
				ls_push_string(ctx.runtime, ls_from_cstr(arg));
			}
		}

		// Bytecode compilation and runtime setup are intentionally outside the benchmark.
		double start = ls_platform_now_ms();
		if (!ls_call(ctx.runtime, ls_from_cstr(function_name))) {
			fprintf(stderr, "Runtime error\n");
			goto cleanup;
		}
		double elapsed_ms = ls_platform_now_ms() - start;

		{
			ls_type_kind result_kind = ls_bytecode_runtime_result_kind(ctx.runtime, ls_from_cstr(function_name));
			if (result_kind != LS_TYPE_VOID) {
				lumc_write_result(stdout, ctx.runtime, result_kind);
				putchar('\n');
			}
		}

		fprintf(stderr, "Time: %.2f ms\n", elapsed_ms);
	}

	rc = 0;

cleanup:
	if (ctx.runtime) ls_runtime_destroy(ctx.runtime);
	if (ctx.bytecode) ls_bytecode_destroy(ctx.bytecode);
	if (ctx.module) ls_module_destroy(ctx.module);
	free(ctx.source);
	ls_default_arena_destroy(&ctx.host.arena);
	return rc;
}
