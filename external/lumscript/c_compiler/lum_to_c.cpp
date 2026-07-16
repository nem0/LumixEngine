// Standalone AST-to-C compiler. Kept separate from the bytecode runner.
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "../arena.h"
#include "../capi.h"
#include "c_compiler.h"

static void* allocate(void*, size_t size, size_t align) { return _aligned_malloc(size, align); }
static void deallocate(void*, void* ptr) { _aligned_free(ptr); }
static void* reallocate(void*, void* ptr, size_t new_size, size_t, size_t align) { return _aligned_realloc(ptr, new_size, align); }

static void diagnostic(void*, ls_string_view text) {
	if (text.begin && text.end) fwrite(text.begin, 1, (size_t)(text.end - text.begin), stderr);
}

static void writeC(void* user_data, ls_string_view text) {
	if (text.begin && text.end) fwrite(text.begin, 1, (size_t)(text.end - text.begin), (FILE*)user_data);
}

static void discardC(void*, ls_string_view) {}

static ls_string_view stringView(const char* text) {
	return {text, text + strlen(text)};
}

int main(int argc, char** argv) {
	if (argc < 2 || argc > 3) {
		fputs("Usage: lum_to_c <script.lum> [output.c]\n", stderr);
		return 1;
	}

	ls_host host = {};
	host.allocate = allocate;
	host.deallocate = deallocate;
	host.reallocate = reallocate;
	host.create_arena = ls_default_arena_create;
	host.destroy_arena = ls_default_arena_destroy;
	host.print = diagnostic;

	const char* input_path = argv[1];
	FILE* input = fopen(input_path, "rb");
	if (!input) { fprintf(stderr, "Error: Cannot read file '%s'\n", input_path); return 1; }
	fseek(input, 0, SEEK_END);
	const long size = ftell(input);
	fseek(input, 0, SEEK_SET);
	if (size < 0) { fclose(input); fprintf(stderr, "Error: Cannot read file '%s'\n", input_path); return 1; }
	char* source = (char*)allocate(nullptr, (size_t)size + 1, 1);
	if (!source) { fclose(input); return 1; }
	const size_t read = fread(source, 1, (size_t)size, input);
	fclose(input);
	source[read] = '\0';

	ls_module* module = ls_module_create(&host);
	int result = 1;
	if (!module || !ls_module_compile(module, stringView(source), stringView(input_path), nullptr, nullptr)) {
		fputs("Compile error\n", stderr);
	}
	else if (!c_compile(*module, discardC, nullptr)) {
		fputs("Error: This script uses a feature not supported by the C backend\n", stderr);
	}
	else {
		FILE* output = argc == 3 ? fopen(argv[2], "wb") : stdout;
		if (!output) fprintf(stderr, "Error: Cannot write C output '%s'\n", argv[2]);
		else {
			const bool emitted = c_compile(*module, writeC, output);
			if (output != stdout) fclose(output);
			result = emitted ? 0 : 1;
		}
	}

	if (module) ls_module_destroy(module);
	deallocate(nullptr, source);
	return result;
}
