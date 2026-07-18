/*
 * lumc.c - Standalone LumScript runner/compiler
 * Usage: lumc [--dump-bytecode] <script.lum> [function_name] [args...]
 *
 * Compiles and runs a LumScript file. If function_name is provided,
 * calls that function with the remaining arguments. Otherwise, calls main().
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <malloc.h>

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
		case LS_TYPE_STRING:
			lumc_print_string(out, ls_to_string(runtime, -1));
			break;
		default:
			fprintf(out, "<%d>", (int)kind);
			break;
	}
}

static void lumc_diagnostics_print(void* userdata, ls_string_view msg) {
	ls_host* host = (ls_host*)userdata;
	lumc_print_string(stderr, msg);
}

static void lumc_native_print(ls_runtime* runtime, ls_call_frame frame) {
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
		case LS_TYPE_STRING: return "string";
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
	static const char* const add[] = {"ADD_I8", "ADD_U8", "ADD_I16", "ADD_U16", "ADD_I32", "ADD_U32", "ADD_I64", "ADD_U64", "ADD_F32", "ADD_F64"};
	static const char* const sub[] = {"SUB_I8", "SUB_U8", "SUB_I16", "SUB_U16", "SUB_I32", "SUB_U32", "SUB_I64", "SUB_U64", "SUB_F32", "SUB_F64"};
	static const char* const mul[] = {"MUL_I8", "MUL_U8", "MUL_I16", "MUL_U16", "MUL_I32", "MUL_U32", "MUL_I64", "MUL_U64", "MUL_F32", "MUL_F64"};
	static const char* const div[] = {"DIV_I8", "DIV_U8", "DIV_I16", "DIV_U16", "DIV_I32", "DIV_U32", "DIV_I64", "DIV_U64", "DIV_F32", "DIV_F64"};
	static const char* const mod[] = {"MOD_I8", "MOD_U8", "MOD_I16", "MOD_U16", "MOD_I32", "MOD_U32", "MOD_I64", "MOD_U64"};
	if (op >= LS_OP_NEG_I8 && op <= LS_OP_NEG_F64) return unary[(u32)op - (u32)LS_OP_NEG_I8];
	if (op >= LS_OP_ADD_I8 && op <= LS_OP_ADD_F64) return add[(u32)op - (u32)LS_OP_ADD_I8];
	if (op >= LS_OP_SUB_I8 && op <= LS_OP_SUB_F64) return sub[(u32)op - (u32)LS_OP_SUB_I8];
	if (op >= LS_OP_MUL_I8 && op <= LS_OP_MUL_F64) return mul[(u32)op - (u32)LS_OP_MUL_I8];
	if (op >= LS_OP_DIV_I8 && op <= LS_OP_DIV_F64) return div[(u32)op - (u32)LS_OP_DIV_I8];
	if (op >= LS_OP_MOD_I8 && op <= LS_OP_MOD_U64) return mod[(u32)op - (u32)LS_OP_MOD_I8];
	switch (op) {
		case LS_OP_LOAD_CONST_1: return "LOAD_CONST_1";
		case LS_OP_LOAD_CONST_2: return "LOAD_CONST_2";
		case LS_OP_LOAD_CONST_4: return "LOAD_CONST_4";
		case LS_OP_LOAD_CONST_8: return "LOAD_CONST_8";
		case LS_OP_LOAD_CONST_STRING: return "LOAD_CONST_STRING";
		case LS_OP_COPY: return "COPY";
		case LS_OP_GLOBAL_LOAD: return "GLOBAL_LOAD";
		case LS_OP_GLOBAL_STORE: return "GLOBAL_STORE";
		case LS_OP_LOCAL_REF: return "LOCAL_REF";
		case LS_OP_GLOBAL_REF: return "GLOBAL_REF";
		case LS_OP_LOAD_AT: return "LOAD_AT";
		case LS_OP_STORE_AT: return "STORE_AT";
		case LS_OP_REF_AT: return "REF_AT";
		case LS_OP_BOUNDS_CHECK: return "BOUNDS_CHECK";
		case LS_OP_SLICE: return "SLICE";
		case LS_OP_SLICE_LOAD: return "SLICE_LOAD";
		case LS_OP_SLICE_STORE: return "SLICE_STORE";
		case LS_OP_SLICE_REF: return "SLICE_REF";
		case LS_OP_SLICE_LENGTH: return "SLICE_LENGTH";
		case LS_OP_NOT: return "NOT";
		case LS_OP_EQ: return "EQ";
		case LS_OP_NE: return "NE";
		case LS_OP_LT: return "LT";
		case LS_OP_LE: return "LE";
		case LS_OP_GT: return "GT";
		case LS_OP_GE: return "GE";
		#define LS_COMPARE_JUMP_NAME(TYPE) \
			case LS_OP_EQ_JUMP_FALSE_##TYPE: return "EQ_JUMP_FALSE_" #TYPE; \
			case LS_OP_NE_JUMP_FALSE_##TYPE: return "NE_JUMP_FALSE_" #TYPE; \
			case LS_OP_LT_JUMP_FALSE_##TYPE: return "LT_JUMP_FALSE_" #TYPE; \
			case LS_OP_LE_JUMP_FALSE_##TYPE: return "LE_JUMP_FALSE_" #TYPE;
		case LS_OP_EQ_JUMP_FALSE_BOOL: return "EQ_JUMP_FALSE_BOOL";
		case LS_OP_NE_JUMP_FALSE_BOOL: return "NE_JUMP_FALSE_BOOL";
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
		case LS_OP_EQ_JUMP_FALSE_STRING: return "EQ_JUMP_FALSE_STRING";
		case LS_OP_NE_JUMP_FALSE_STRING: return "NE_JUMP_FALSE_STRING";
		case LS_OP_EQ_JUMP_FALSE_ENUM: return "EQ_JUMP_FALSE_ENUM";
		case LS_OP_NE_JUMP_FALSE_ENUM: return "NE_JUMP_FALSE_ENUM";
		#undef LS_COMPARE_JUMP_NAME
		case LS_OP_JUMP: return "JUMP";
		case LS_OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
		case LS_OP_JUMP_IF_TRUE: return "JUMP_IF_TRUE";
		case LS_OP_CALL_DIRECT: return "CALL_DIRECT";
		case LS_OP_CALL_INDIRECT: return "CALL_INDIRECT";
		case LS_OP_CAST: return "CAST";
		case LS_OP_STRING_TO_CSTR: return "STRING_TO_CSTR";
		case LS_OP_CSTR_TO_STRING: return "CSTR_TO_STRING";
		case LS_OP_RETURN: return "RETURN";
		default: return "UNKNOWN";
	}
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

static u64 lumc_read_u64(const u8* code, u32 size, u32* pc) {
	u64 value = 0;
	if (*pc + sizeof(value) <= size) memcpy(&value, code + *pc, sizeof(value));
	*pc += (u32)sizeof(value);
	return value;
}

static void lumc_dump_bytecode(const ls_bytecode* bytecode) {
	for (u32 function_index = 0; function_index < bytecode->function_count; ++function_index) {
		const ls_function_bc* fn = &bytecode->functions[function_index];
		printf("function %u %.*s (%s, params=%u, frame=%u, returns=%u)\n",
			function_index,
			fn->name.begin ? (int)(fn->name.end - fn->name.begin) : 0,
			fn->name.begin ? fn->name.begin : "<global_init>",
			fn->kind == LS_FUNCTION_NATIVE ? "native" : "script",
			fn->param_size, fn->frame_size, fn->return_size);

		for (u32 pc = 0; pc < fn->code_size;) {
			const u32 offset = pc;
			const ls_op op = (ls_op)fn->code[pc++];
			printf("  %04u: %-18s", offset, lumc_opcode_name(op));
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
			case LS_OP_LOAD_CONST_STRING: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 index = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, value=string[%u]", dst, index);
				break;
			}
			case LS_OP_COPY:
			case LS_OP_GLOBAL_LOAD:
			case LS_OP_GLOBAL_STORE: {
				const u32 a = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 b = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 c = lumc_read_u32(fn->code, fn->code_size, &pc);
				if (op == LS_OP_COPY) printf(" dst=%u, src=%u, size=%u", a, b, c);
				else if (op == LS_OP_GLOBAL_LOAD) printf(" dst=%u, offset=%u, size=%u", a, b, c);
				else printf(" offset=%u, src=%u, size=%u", a, b, c);
				break;
			}
			case LS_OP_LOCAL_REF:
			case LS_OP_GLOBAL_REF: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 offset = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, offset=%u", dst, offset);
				break;
			}
			case LS_OP_LOAD_AT:
			case LS_OP_STORE_AT:
			case LS_OP_REF_AT: {
				const u32 a = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 b = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 c = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 scale = lumc_read_u32(fn->code, fn->code_size, &pc);
				const i32 offset = lumc_read_i32(fn->code, fn->code_size, &pc);
				const u32 value_size = op == LS_OP_REF_AT ? 0u : lumc_read_u32(fn->code, fn->code_size, &pc);
				if (op == LS_OP_LOAD_AT) printf(" dst=%u, base=%u, index=%u, scale=%u, offset=%d, size=%u", a, b, c, scale, offset, value_size);
				else if (op == LS_OP_STORE_AT) printf(" base=%u, index=%u, src=%u, scale=%u, offset=%d, size=%u", a, b, c, scale, offset, value_size);
				else printf(" dst=%u, base=%u, index=%u, scale=%u, offset=%d", a, b, c, scale, offset);
				break;
			}
			case LS_OP_BOUNDS_CHECK: {
				const u32 index = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u64 length = lumc_read_u64(fn->code, fn->code_size, &pc);
				printf(" index=%u, length=%llu", index, (unsigned long long)length);
				break;
			}
			case LS_OP_SLICE:
			case LS_OP_SLICE_LOAD:
			case LS_OP_SLICE_STORE:
			case LS_OP_SLICE_REF:
			case LS_OP_SLICE_LENGTH: {
				const u32 count = op == LS_OP_SLICE ? 4u : op == LS_OP_SLICE_STORE ? 4u : op == LS_OP_SLICE_LOAD || op == LS_OP_SLICE_REF ? 3u : 1u;
				for (u32 i = 0; i < count; ++i) printf(" arg%u=%u", i, lumc_read_u32(fn->code, fn->code_size, &pc));
				break;
			}
			case LS_OP_ADD_I8: case LS_OP_ADD_U8: case LS_OP_ADD_I16: case LS_OP_ADD_U16: case LS_OP_ADD_I32: case LS_OP_ADD_U32: case LS_OP_ADD_I64: case LS_OP_ADD_U64: case LS_OP_ADD_F32: case LS_OP_ADD_F64:
			case LS_OP_SUB_I8: case LS_OP_SUB_U8: case LS_OP_SUB_I16: case LS_OP_SUB_U16: case LS_OP_SUB_I32: case LS_OP_SUB_U32: case LS_OP_SUB_I64: case LS_OP_SUB_U64: case LS_OP_SUB_F32: case LS_OP_SUB_F64:
			case LS_OP_MUL_I8: case LS_OP_MUL_U8: case LS_OP_MUL_I16: case LS_OP_MUL_U16: case LS_OP_MUL_I32: case LS_OP_MUL_U32: case LS_OP_MUL_I64: case LS_OP_MUL_U64: case LS_OP_MUL_F32: case LS_OP_MUL_F64:
			case LS_OP_DIV_I8: case LS_OP_DIV_U8: case LS_OP_DIV_I16: case LS_OP_DIV_U16: case LS_OP_DIV_I32: case LS_OP_DIV_U32: case LS_OP_DIV_I64: case LS_OP_DIV_U64: case LS_OP_DIV_F32: case LS_OP_DIV_F64:
			case LS_OP_MOD_I8: case LS_OP_MOD_U8: case LS_OP_MOD_I16: case LS_OP_MOD_U16: case LS_OP_MOD_I32: case LS_OP_MOD_U32: case LS_OP_MOD_I64: case LS_OP_MOD_U64: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 lhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 rhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, lhs=%u, rhs=%u", dst, lhs, rhs);
				break;
			}
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
				case LS_OP_EQ_JUMP_FALSE_##TYPE: \
				case LS_OP_NE_JUMP_FALSE_##TYPE: \
				case LS_OP_LT_JUMP_FALSE_##TYPE: \
				case LS_OP_LE_JUMP_FALSE_##TYPE:
			case LS_OP_EQ_JUMP_FALSE_BOOL:
			case LS_OP_NE_JUMP_FALSE_BOOL:
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
			case LS_OP_EQ_JUMP_FALSE_STRING:
			case LS_OP_NE_JUMP_FALSE_STRING:
			case LS_OP_EQ_JUMP_FALSE_ENUM:
			case LS_OP_NE_JUMP_FALSE_ENUM:
			#undef LS_COMPARE_JUMP_CASES
			{
				const u32 lhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 rhs = lumc_read_u32(fn->code, fn->code_size, &pc);
				const i32 jump_offset = lumc_read_i32(fn->code, fn->code_size, &pc);
				printf(" lhs=%u, rhs=%u, offset=%d, target=%d", lhs, rhs, jump_offset, (i32)pc + jump_offset);
				break;
			}
			case LS_OP_JUMP: {
				const i32 jump_offset = lumc_read_i32(fn->code, fn->code_size, &pc);
				printf(" offset=%d, target=%d", jump_offset, (i32)pc + jump_offset);
				break;
			}
			case LS_OP_JUMP_IF_FALSE:
			case LS_OP_JUMP_IF_TRUE: {
				const u32 condition = lumc_read_u32(fn->code, fn->code_size, &pc);
				const i32 jump_offset = lumc_read_i32(fn->code, fn->code_size, &pc);
				printf(" cond=%u, offset=%d, target=%d", condition, jump_offset, (i32)pc + jump_offset);
				break;
			}
			case LS_OP_CALL_DIRECT: {
				const u32 callee = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 arg_top = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" function=%u, arg_top=%u", callee, arg_top);
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
			case LS_OP_STRING_TO_CSTR:
			case LS_OP_CSTR_TO_STRING: {
				const u32 dst = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 src = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" dst=%u, src=%u", dst, src);
				break;
			}
			case LS_OP_RETURN: {
				const u32 src = lumc_read_u32(fn->code, fn->code_size, &pc);
				const u32 return_size = lumc_read_u32(fn->code, fn->code_size, &pc);
				printf(" src=%u, size=%u", src, return_size);
				break;
			}
			case LS_OP_NOT:
			case LS_OP_NEG_I8: case LS_OP_NEG_U8: case LS_OP_NEG_I16: case LS_OP_NEG_U16: case LS_OP_NEG_I32: case LS_OP_NEG_U32: case LS_OP_NEG_I64: case LS_OP_NEG_U64: case LS_OP_NEG_F32: case LS_OP_NEG_F64: {
				printf(" dst=%u", lumc_read_u32(fn->code, fn->code_size, &pc));
				break;
			}
			default:
				break;
			}
			putchar('\n');
		}
	}
}

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

	const int dump_bytecode = strcmp(argv[1], "--dump-bytecode") == 0;
	const int script_arg = dump_bytecode ? 2 : 1;
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

	ctx.bytecode = ls_bytecode_compile(ctx.module, &ctx.host);
	if (!ctx.bytecode) {
		fprintf(stderr, "Error: Failed to compile bytecode\n");
		goto cleanup;
	}
	if (dump_bytecode) {
		lumc_dump_bytecode(ctx.bytecode);
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

		clock_t start = clock();
		if (!ls_call(ctx.runtime, ls_from_cstr(function_name))) {
			fprintf(stderr, "Runtime error\n");
			goto cleanup;
		}
		clock_t end = clock();
		double elapsed_ms = (double)(end - start) / CLOCKS_PER_SEC * 1000.0;

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
