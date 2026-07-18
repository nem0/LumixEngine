/*
 * lumc.c - Standalone LumScript runner/compiler
 * Usage: lumc <script.lum> [function_name] [args...]
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

int main(int argc, char** argv) {
	if (argc < 2) {
		fputs("Usage: lumc <script.lum> [function_name] [args...]\n"
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

	const char* script_path = argv[1];
	const char* function_name = argc > 2 ? argv[2] : "main";
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

	size_t call_arg_count = argc > 3 ? (size_t)(argc - 3) : 0;
	{
		for (size_t i = 0; i < call_arg_count; ++i) {
			char* end = NULL;
			const char* arg = argv[i + 3];
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
