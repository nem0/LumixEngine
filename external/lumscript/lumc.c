/*
 * lumc.c - Standalone LumScript runner/compiler
 * Usage: lumc <script.lum> [function_name] [args...]
 *
 * Compiles and runs a LumScript file. If function_name is provided,
 * calls that function with the remaining arguments. Otherwise, calls main().
 */

#define LUMIX_NO_CUSTOM_CRT
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
	NULL,
	0
};

static ls_string_view ls_from_cstr(const char* str) {
	return (ls_string_view){str, str ? str + strlen(str) : NULL};
}

static char* lumc_read_file(const char* path, const ls_host* host) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;

	long size;
	if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}

	char* data = (char*)host->allocate(host->allocator_userdata, (size_t)size + 1, 1);
	if (!data) {
		fclose(f);
		return NULL;
	}

	size_t read = fread(data, 1, (size_t)size, f);
	fclose(f);
	data[read] = '\0';
	return data;
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
	if (host) host->has_error = 1;
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

static void lumc_print_usage(void) {
	fputs("Usage: lumc <script.lum> [function_name] [args...]\n"
		"  script.lum     - Path to LumScript source file\n"
		"  function_name  - Function to call (default: main)\n"
		"  args           - Arguments passed to the function\n", stderr);
}

static void lumc_print_value(ls_value value) {
	lumc_write_value(stdout, value);
	putchar('\n');
}

static ls_value lumc_parse_arg(const char* arg) {
	char* end = NULL;
	double d = strtod(arg, &end);
	if (end != arg && *end == '\0') {
		if (strpbrk(arg, ".eE")) {
			return ls_value_make_f64(d);
		}
		return ls_value_make_i64((int64_t)d);
	}
	if (strcmp(arg, "true") == 0) return ls_value_make_bool(1);
	if (strcmp(arg, "false") == 0) return ls_value_make_bool(0);
	return ls_value_make_string(ls_from_cstr(arg));
}

static int lumc_register_builtins(lumc_context* ctx) {
	const ls_type params[1] = {ls_type_make(LS_TYPE_STRING)};
	return ls_module_add_native_function(
		ctx->module,
		ls_from_cstr("print"),
		ls_type_make(LS_TYPE_VOID),
		params,
		1,
		&lumc_native_print,
		NULL
	) >= 0;
}

static int lumc_compile(lumc_context* ctx, const char* script_path) {
	ctx->host.has_error = 0;
	return ls_module_compile(
		ctx->module,
		ls_from_cstr(ctx->source),
		ls_from_cstr(script_path),
		&ctx->host,
		NULL,
		NULL
	);
}

static int lumc_run(lumc_context* ctx, const char* function_name, int argc, char** argv) {
	size_t call_arg_count = argc > 3 ? (size_t)(argc - 3) : 0;
	if (call_arg_count > 0) {
		ctx->call_args = (ls_value*)ctx->host.allocate(ctx->host.allocator_userdata, sizeof(ls_value) * call_arg_count, 16);
		if (!ctx->call_args) return 0;
		for (size_t i = 0; i < call_arg_count; ++i) {
			ctx->call_args[i] = lumc_parse_arg(argv[i + 3]);
		}
	}

	ls_value result = ls_value_make_void();
	ctx->host.has_error = 0;
	if (!ls_runtime_call(
		ctx->runtime,
		ls_from_cstr(function_name),
		ctx->call_args,
		call_arg_count,
		&result,
		&ctx->host
	)) {
		return 0;
	}

	if (result.type.kind != LS_TYPE_VOID) lumc_print_value(result);
	return 1;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		lumc_print_usage();
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

	ctx.source = lumc_read_file(script_path, &ctx.host);
	if (!ctx.source) {
		fprintf(stderr, "Error: Cannot read file '%s'\n", script_path);
		goto cleanup;
	}

	ctx.module = ls_module_create(&ctx.host);
	if (!ctx.module) {
		fprintf(stderr, "Error: Failed to create LumScript module\n");
		goto cleanup;
	}

	if (!lumc_register_builtins(&ctx)) {
		fprintf(stderr, "Error: Failed to register native print\n");
		goto cleanup;
	}

	if (!lumc_compile(&ctx, script_path)) {
		fprintf(stderr, "Compile error\n");
		goto cleanup;
	}

	ctx.runtime = ls_runtime_create(ctx.module);
	if (!ctx.runtime) {
		fprintf(stderr, "Error: Failed to create LumScript runtime\n");
		goto cleanup;
	}

	if (!lumc_run(&ctx, function_name, argc, argv)) {
		fprintf(stderr, "Runtime error\n");
		goto cleanup;
	}

	rc = 0;

cleanup:
	if (ctx.call_args) ctx.host.deallocate(ctx.host.allocator_userdata, ctx.call_args);
	if (ctx.runtime) ls_runtime_destroy(ctx.runtime);
	if (ctx.module) ls_module_destroy(ctx.module);
	if (ctx.source) ctx.host.deallocate(ctx.host.allocator_userdata, ctx.source);
	return rc;
}
