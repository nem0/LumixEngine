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
	ls_runtime* runtime;
	ls_value* call_args;
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

static void lumc_write_value(FILE* out, ls_value value) {
	switch (value.type.kind) {
		case LS_TYPE_VOID:
			break;
		case LS_TYPE_BOOL:
			fputs(value.b ? "true" : "false", out);
			break;
		case LS_TYPE_I8:
		case LS_TYPE_I16:
		case LS_TYPE_I32:
			fprintf(out, "%d", value.i);
			break;
		case LS_TYPE_U8:
		case LS_TYPE_U16:
		case LS_TYPE_U32:
			fprintf(out, "%u", value.u);
			break;
		case LS_TYPE_I64:
			fprintf(out, "%lld", (long long)value.i64);
			break;
		case LS_TYPE_U64:
			fprintf(out, "%llu", (unsigned long long)value.u64);
			break;
		case LS_TYPE_F32:
			fprintf(out, "%f", value.f);
			break;
		case LS_TYPE_F64:
			fprintf(out, "%lf", value.d);
			break;
		case LS_TYPE_STRING:
			lumc_print_string(out, value.string);
			break;
		default:
			fprintf(out, "<%d>", (int)value.type.kind);
			break;
	}
}

static void lumc_diagnostics_print(void* userdata, ls_string_view msg) {
	ls_host* host = (ls_host*)userdata;
	lumc_print_string(stderr, msg);
}

static int lumc_native_print(const ls_value* args, size_t arg_count, ls_value* result, void* userdata) {
	(void)userdata;

	if (arg_count != 1 || args[0].type.kind != LS_TYPE_STRING) return 0;

	lumc_print_string(stdout, args[0].string);
	putchar('\n');
	if (result) *result = ls_value_make_void();
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

	{
		const ls_type params[1] = {ls_type_make(LS_TYPE_STRING)};
		if (ls_module_add_native_function(
			ctx.module,
			ls_from_cstr("print"),
			ls_type_make(LS_TYPE_VOID),
			params,
			1,
			&lumc_native_print,
			NULL
		) < 0) {
			fprintf(stderr, "Error: Failed to register native print\n");
			goto cleanup;
		}
	}

	if (!ls_module_compile(
		ctx.module,
		ls_from_cstr(ctx.source),
		ls_from_cstr(script_path),
		&ctx.host,
		NULL,
		NULL
	)) {
		fprintf(stderr, "Compile error\n");
		goto cleanup;
	}

	ctx.runtime = ls_runtime_create(ctx.module);
	if (!ctx.runtime) {
		fprintf(stderr, "Error: Failed to create LumScript runtime\n");
		goto cleanup;
	}

	size_t call_arg_count = argc > 3 ? (size_t)(argc - 3) : 0;
	if (call_arg_count > 0) {
		ctx.call_args = (ls_value*)ctx.host.allocate(ctx.host.allocator_userdata, sizeof(ls_value) * call_arg_count, 16);
		if (!ctx.call_args) {
			fprintf(stderr, "Error: Out of memory\n");
			goto cleanup;
		}
		for (size_t i = 0; i < call_arg_count; ++i) {
			char* end = NULL;
			const char* arg = argv[i + 3];
			double d = strtod(arg, &end);
			if (end != arg && *end == '\0') {
				if (strpbrk(arg, ".eE")) {
					ctx.call_args[i] = ls_value_make_f64(d);
				} else {
					ctx.call_args[i] = ls_value_make_i64((i64)d);
				}
			} else if (strcmp(arg, "true") == 0) {
				ctx.call_args[i] = ls_value_make_bool(1);
			} else if (strcmp(arg, "false") == 0) {
				ctx.call_args[i] = ls_value_make_bool(0);
			} else {
				ctx.call_args[i] = ls_value_make_string(ls_from_cstr(arg));
			}
		}
	}

	{
		ls_value result = ls_value_make_void();
		if (!ls_runtime_call(
			ctx.runtime,
			ls_from_cstr(function_name),
			ctx.call_args,
			call_arg_count,
			&result,
			&ctx.host
		)) {
			fprintf(stderr, "Runtime error\n");
			goto cleanup;
		}

		if (result.type.kind != LS_TYPE_VOID) {
			lumc_write_value(stdout, result);
			putchar('\n');
		}
	}

	rc = 0;

cleanup:
	if (ctx.call_args) ctx.host.deallocate(ctx.host.allocator_userdata, ctx.call_args);
	if (ctx.runtime) ls_runtime_destroy(ctx.runtime);
	if (ctx.module) ls_module_destroy(ctx.module);
	if (ctx.source) ctx.host.deallocate(ctx.host.allocator_userdata, ctx.source);
	return rc;
}
