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

#include <malloc.h>

#include "capi.h"

typedef struct lumc_context {
	ls_host host;
	char* source;
	ls_module* module;
	ls_bytecode* bytecode;
	ls_runtime* runtime;
} lumc_context;

static void* lumc_allocate(void* userdata, size_t size, size_t align) {
	(void)userdata;
	return _aligned_malloc(size, align);
}

static void lumc_deallocate(void* userdata, void* ptr) {
	(void)userdata;
	_aligned_free(ptr);
}

static void* lumc_reallocate(void* userdata, void* ptr, size_t new_size, size_t old_size, size_t align) {
	(void)userdata;
	(void)old_size;
	return _aligned_realloc(ptr, new_size, align);
}

static const ls_host g_host_template = {
	NULL,
	&lumc_allocate,
	&lumc_deallocate,
	&lumc_reallocate,
	NULL,
	NULL
};

static ls_string_view ls_from_cstr(const char* str) {
	return (ls_string_view){str, str ? str + strlen(str) : NULL};
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

static int lumc_native_print(ls_runtime* runtime, size_t arg_count, size_t result_count, void* userdata) {
	(void)userdata;

	if (arg_count != 1 || result_count != 0) return 0;

	lumc_print_string(stdout, ls_to_string(runtime, -1));
	putchar('\n');
	return 1;
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
			ctx.source = (char*)ctx.host.allocate(ctx.host.allocator_userdata, (size_t)size + 1, 1);
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

	int native_print = -1;
	{
		const ls_type params[1] = {ls_type_make(LS_TYPE_STRING)};
		native_print = ls_module_add_native_function(
			ctx.module,
			ls_from_cstr("print"),
			ls_type_make(LS_TYPE_VOID),
			params,
			1
		);
		if (native_print < 0) {
			fprintf(stderr, "Error: Failed to register native print\n");
			goto cleanup;
		}
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

	ctx.runtime = ls_runtime_create(ctx.bytecode);
	if (!ctx.runtime) {
		fprintf(stderr, "Error: Failed to create bytecode runtime\n");
		goto cleanup;
	}
	if (!ls_runtime_set_native_function_callback(ctx.runtime, native_print, &lumc_native_print, NULL)) {
		fprintf(stderr, "Error: Failed to bind native print\n");
		goto cleanup;
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
		if (!ls_call(ctx.runtime, ls_from_cstr(function_name), call_arg_count, 1)) {
			fprintf(stderr, "Runtime error\n");
			goto cleanup;
		}

		{
			ls_type_kind result_kind = ls_bytecode_runtime_result_kind(ctx.runtime, ls_from_cstr(function_name));
			if (result_kind != LS_TYPE_VOID) {
				lumc_write_result(stdout, ctx.runtime, result_kind);
				putchar('\n');
			}
		}
	}

	rc = 0;

cleanup:
	if (ctx.runtime) ls_runtime_destroy(ctx.runtime);
	if (ctx.bytecode) ls_bytecode_destroy(ctx.bytecode);
	if (ctx.module) ls_module_destroy(ctx.module);
	if (ctx.source) ctx.host.deallocate(ctx.host.allocator_userdata, ctx.source);
	return rc;
}
