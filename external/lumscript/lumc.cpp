// lumc.cpp - Standalone LumScript runner/compiler
// Usage: lumc <script.lum> [function_name] [args...]
//
// Compiles and runs a LumScript file. If function_name is provided,
// calls that function with the remaining arguments. Otherwise, calls main().

#define LUMIX_NO_CUSTOM_CRT
#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Undefine EOF macro from <cstdio> to avoid conflict with Token::EOF
#ifdef EOF
#undef EOF
#endif

#include "core/core.h"
#include "core/allocator.h"
#include "core/span.h"
#include "core/array.h"
#include "core/string.h"

#include "lumscript/ast.h"
#include "lumscript/diagnostics.h"
#include "lumscript/tokenizer.h"
#include "lumscript/parser.h"
#include "lumscript/checker.cpp"
#include "lumscript/runtime.h"

// Include source implementations
#include "core/string.cpp"

// Simple malloc-based allocator for standalone build (no Mutex/OS dependencies)
namespace Lumix {

struct SimpleAllocator final : IAllocator {
	void* allocate(size_t size, size_t align) override {
		void* ptr = _aligned_malloc(size, align);
		return ptr;
	}
	void deallocate(void* ptr) override {
		_aligned_free(ptr);
	}
	void* reallocate(void* ptr, size_t new_size, size_t old_size, size_t align) override {
		(void)old_size;
		(void)align;
		return _aligned_realloc(ptr, new_size, align);
	}
};

static SimpleAllocator g_allocator_instance;

IAllocator& getGlobalAllocator() {
	return g_allocator_instance;
}

} // namespace Lumix

// File I/O helpers
static char* readFile(const char* path, Lumix::IAllocator& allocator) {
	FILE* f = fopen(path, "rb");
	if (!f) return nullptr;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* data = (char*)allocator.allocate(size + 1, alignof(char));
	if (!data) {
		fclose(f);
		return nullptr;
	}
	size_t read = fread(data, 1, size, f);
	fclose(f);
	data[read] = '\0';
	return data;
}

// Native function: print
static bool nativePrint(Lumix::Span<const Lumix::LumScript::Value> args, Lumix::LumScript::Value* result, void* userdata) {
	for (Lumix::i32 i = 0; i < (Lumix::i32)args.size(); ++i) {
		if (i > 0) printf(" ");
		const Lumix::LumScript::Value& v = args[i];
		switch (v.type.kind) {
			case Lumix::LumScript::TypeRef::BOOL:
				printf("%s", v.b ? "true" : "false");
				break;
			case Lumix::LumScript::TypeRef::I8:
			case Lumix::LumScript::TypeRef::I16:
			case Lumix::LumScript::TypeRef::I32:
				printf("%d", v.i);
				break;
			case Lumix::LumScript::TypeRef::U8:
			case Lumix::LumScript::TypeRef::U16:
			case Lumix::LumScript::TypeRef::U32:
				printf("%u", v.u);
				break;
			case Lumix::LumScript::TypeRef::I64:
				printf("%lld", (long long)v.i64);
				break;
			case Lumix::LumScript::TypeRef::U64:
				printf("%llu", (unsigned long long)v.u64);
				break;
			case Lumix::LumScript::TypeRef::F32:
				printf("%f", v.f);
				break;
			case Lumix::LumScript::TypeRef::F64:
				printf("%lf", v.d);
				break;
			case Lumix::LumScript::TypeRef::STRING:
				printf("%.*s", (int)v.string.size(), v.string.begin);
				break;
			default:
				printf("<%d>", (int)v.type.kind);
				break;
		}
	}
	printf("\n");
	if (result) {
		*result = Lumix::LumScript::Runtime::makeI32(0);
	}
	return true;
}

static void printUsage() {
	fprintf(stderr, "Usage: lumc <script.lum> [function_name] [args...]\n");
	fprintf(stderr, "  script.lum     - Path to LumScript source file\n");
	fprintf(stderr, "  function_name  - Function to call (default: main)\n");
	fprintf(stderr, "  args           - Arguments passed to the function\n");
}

static void printValue(const Lumix::LumScript::Value& v) {
	switch (v.type.kind) {
		case Lumix::LumScript::TypeRef::VOID:
			break;
		case Lumix::LumScript::TypeRef::BOOL:
			printf("%s\n", v.b ? "true" : "false");
			break;
		case Lumix::LumScript::TypeRef::I8:
		case Lumix::LumScript::TypeRef::I16:
		case Lumix::LumScript::TypeRef::I32:
			printf("%d\n", v.i);
			break;
		case Lumix::LumScript::TypeRef::U8:
		case Lumix::LumScript::TypeRef::U16:
		case Lumix::LumScript::TypeRef::U32:
			printf("%u\n", v.u);
			break;
		case Lumix::LumScript::TypeRef::I64:
			printf("%lld\n", (long long)v.i64);
			break;
		case Lumix::LumScript::TypeRef::U64:
			printf("%llu\n", (unsigned long long)v.u64);
			break;
		case Lumix::LumScript::TypeRef::F32:
			printf("%f\n", v.f);
			break;
		case Lumix::LumScript::TypeRef::F64:
			printf("%lf\n", v.d);
			break;
		case Lumix::LumScript::TypeRef::STRING:
			printf("%.*s\n", (int)v.string.size(), v.string.begin);
			break;
		default:
			printf("<result type=%d>\n", (int)v.type.kind);
			break;
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		printUsage();
		return 1;
	}

	const char* script_path = argv[1];
	const char* function_name = argc > 2 ? argv[2] : "main";

	// Set up allocator
	Lumix::SimpleAllocator& allocator = static_cast<Lumix::SimpleAllocator&>(Lumix::getGlobalAllocator());

	// Read source file
	char* source = readFile(script_path, allocator);
	if (!source) {
		fprintf(stderr, "Error: Cannot read file '%s'\n", script_path);
		return 1;
	}

	// Create module and diagnostics
	Lumix::LumScript::Module module(allocator);
	Lumix::LumScript::Diagnostics diagnostics(allocator);

	// Parse
	Lumix::LumScript::Parser parser(module, diagnostics);
	parser.init(Lumix::StringView(source), Lumix::StringView(script_path));
	if (!parser.parse()) {
		fprintf(stderr, "Parse error: %s\n", diagnostics.message.c_str());
		return 1;
	}

	// Register native functions BEFORE type checking
	Lumix::LumScript::TypeRef void_type(Lumix::LumScript::TypeRef::VOID);
	Lumix::LumScript::TypeRef i32_type(Lumix::LumScript::TypeRef::I32);
	Lumix::LumScript::TypeRef string_type(Lumix::LumScript::TypeRef::STRING);

	// print(string) -> void
	{
		Lumix::LumScript::NativeFunctionDecl& fn = module.native_functions.emplace(module.allocator);
		fn.name = module.copyName("print");
		fn.return_type = void_type;
		fn.callback = nativePrint;
		fn.userdata = nullptr;
		Lumix::LumScript::Param& p = fn.params.emplace();
		p.type = string_type;
		p.name = module.copyName("msg");
	}

	// Type check
	Lumix::LumScript::Checker checker(module, diagnostics);
	if (!checker.check()) {
		fprintf(stderr, "Type error: %s\n", diagnostics.message.c_str());
		return 1;
	}

	// Parse command-line arguments into values
	Lumix::Array<Lumix::LumScript::Value> call_args(allocator);
	for (int i = 3; i < argc; ++i) {
		// Try to parse as number first
		char* end = nullptr;
		double d = strtod(argv[i], &end);
		if (end != argv[i] && *end == '\0') {
			// It's a number
			if (strchr(argv[i], '.') || strchr(argv[i], 'e') || strchr(argv[i], 'E')) {
				call_args.push(Lumix::LumScript::Runtime::makeF64(d));
			} else {
				call_args.push(Lumix::LumScript::Runtime::makeI64((Lumix::i64)d));
			}
		} else if (strcmp(argv[i], "true") == 0) {
			Lumix::LumScript::Value v;
			v.type = Lumix::LumScript::TypeRef(Lumix::LumScript::TypeRef::BOOL);
			v.b = true;
			call_args.push(v);
		} else if (strcmp(argv[i], "false") == 0) {
			Lumix::LumScript::Value v;
			v.type = Lumix::LumScript::TypeRef(Lumix::LumScript::TypeRef::BOOL);
			v.b = false;
			call_args.push(v);
		} else {
			// String argument
			Lumix::StringView sv(argv[i]);
			sv = module.copyName(sv);
			call_args.push(Lumix::LumScript::Runtime::makeString(sv));
		}
	}

	// Run
	Lumix::LumScript::Runtime runtime(module, allocator);
	Lumix::LumScript::Value result;
	Lumix::LumScript::RuntimeOptions options;
	options.max_steps = 100000;

	Lumix::Span<const Lumix::LumScript::Value> args_span(call_args.begin(), call_args.end());
	bool ok = runtime.call(Lumix::StringView(function_name), args_span, &result, diagnostics, options);

	if (!ok) {
		fprintf(stderr, "Runtime error: %s\n", diagnostics.message.c_str());
		return 1;
	}

	// Print result if not void
	if (result.type.kind != Lumix::LumScript::TypeRef::VOID) {
		printValue(result);
	}

	return 0;
}
