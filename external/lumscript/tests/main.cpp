#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>

#include "../arena.h"
#include "../utils.h"
#include "../utils.h"
#include "../bytecode.h"
#include "../capi.h"

void print(const char* val) { printf(val); }
void print(int val) { printf("%d", val); }

#define EXPECT_EQ(expected, actual) \
	if ((expected) != (actual)) { \
		if constexpr (sizeof(expected) == 8) { \
			printf("TEST FAILED at %s:%d: Expected: %lld, Actual: %lld\n", __FILE__, __LINE__, (long long)(expected), (long long)(actual)); \
		} else { \
			printf("TEST FAILED at %s:%d: Expected: %d, Actual: %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); \
		} \
		return false; \
	}

#define EXPECT_FLOAT_EQ(expected, actual) \
	do { \
		float diff = (expected) - (actual); \
		if (diff < 0) diff = -diff; \
		if (diff >= 0.01f) { \
			printf("TEST FAILED at %s:%d: Expected: %f, Actual: %f\n", __FILE__, __LINE__, (double)(expected), (double)(actual)); \
			return false; \
		} \
	} while(false)

#define EXPECT_TRUE(condition) \
	if (!(condition)) { \
		printf("TEST FAILED at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return false; \
	}
    
#define EXPECT_COMPILE(src) \
	do { \
		TestContext context; \
		ls_module* module = ls_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		bool compiled = ls_module_compile(module, toLs(src), makeStringView(__func__), nullptr, nullptr); \
		ls_bytecode* bytecode = compiled ? ls_bytecode_compile(module, &context.host) : nullptr; \
		if (bytecode) ls_bytecode_destroy(bytecode); \
		ls_module_destroy(module); \
		EXPECT_TRUE(compiled); \
		EXPECT_TRUE(bytecode != nullptr); \
	} while(false)

#define EXPECT_COMPILE_FAIL(src) \
	do { \
		TestContext context; \
		ls_module* module = ls_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		context.diagnostics.output_enabled = false; \
		EXPECT_TRUE(!ls_module_compile(module, toLs(src), makeStringView(__func__), nullptr, nullptr)); \
		ls_module_destroy(module); \
	} while(false)

#define EXPECT_COMPILE_WITH_IMPORTS(src, files) \
	do { \
		TestContext context; \
		ls_module* module = ls_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		bool compiled = ls_module_compile(module, toLs(src), makeStringView(__func__), &resolveLumScriptImportC, &(files)); \
		ls_bytecode* bytecode = compiled ? ls_bytecode_compile(module, &context.host) : nullptr; \
		if (bytecode) ls_bytecode_destroy(bytecode); \
		ls_module_destroy(module); \
		EXPECT_TRUE(compiled); \
		EXPECT_TRUE(bytecode != nullptr); \
	} while(false)

#define EXPECT_COMPILE_FAIL_WITH_IMPORTS(src, files) \
	do { \
		TestContext context; \
		ls_module* module = ls_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		context.diagnostics.output_enabled = false; \
		EXPECT_TRUE(!ls_module_compile(module, toLs(src), makeStringView(__func__), &resolveLumScriptImportC, &(files))); \
		ls_module_destroy(module); \
	} while(false)

#define EXPECT_RUNTIME_WITH_IMPORTS(src, files, runtime_name, body) \
	do { \
		CAPI_BEGIN(module, diagnostics); \
		EXPECT_TRUE(ls_module_compile(module, toLs(src), makeStringView(__func__), &resolveLumScriptImportC, &(files))); \
		CAPI_RUNTIME(module, runtime_name); \
		body; \
		CAPI_END(module); \
	} while(false)

struct LumScriptImportFile {
	ls_string_view path;
	ls_string_view source;
};

struct LumScriptImportFiles {
	const LumScriptImportFile* files = nullptr;
	u32 count = 0;
};

template <typename T, u32 L>
u32 lengthOf(T (&)[L]) { return L; }

struct TestList {
    using test_fn = bool (*)();

    TestList(test_fn fn, const char* name) : fn(fn), name(name) {
        next = first;
        first = this;
    }

    static inline TestList* first = nullptr;
    TestList* next = nullptr;
    test_fn fn;
    const char* name;
};

#define TEST(name) \
    bool name(); \
    TestList name ## name (&name, #name); \
    bool name() \

static int test_count = 0;
static int passed_count = 0;

static void testPrint(void* userdata, ls_string_view msg);

static ls_string_view toLs(ls_string_view value) {
	return value;
}

static ls_string_view toLs(const char* value) {
	return makeStringView(value);
}

struct TestContext {
	TestContext() {
		ls_default_arena_create(&host.arena);
		host.diagnostics_userdata = &diagnostics;
		host.print = &testPrint;
	}

	~TestContext() { ls_default_arena_destroy(&host.arena); }

	struct Diagnostics {
		bool output_enabled = true;
	} diagnostics;

	ls_host host = {};
};

struct RuntimeGuard {
	// The tests run through the bytecode runtime, so this guard owns both the
	// compiled bytecode and the runtime bound to it.
	explicit RuntimeGuard(ls_module* module, ls_host* host)
		: bytecode(ls_bytecode_compile(module, host))
		, runtime(bytecode ? ls_runtime_create(bytecode, nullptr) : nullptr)
	{}

	~RuntimeGuard() {
		if (runtime) ls_runtime_destroy(runtime);
		if (bytecode) ls_bytecode_destroy(bytecode);
	}

	RuntimeGuard(const RuntimeGuard&) = delete;
	RuntimeGuard& operator=(const RuntimeGuard&) = delete;

	RuntimeGuard(RuntimeGuard&& rhs) noexcept
		: bytecode(rhs.bytecode)
		, runtime(rhs.runtime)
	{
		rhs.bytecode = nullptr;
		rhs.runtime = nullptr;
	}

	RuntimeGuard& operator=(RuntimeGuard&& rhs) noexcept {
		if (this == &rhs) return *this;
		if (runtime) ls_runtime_destroy(runtime);
		if (bytecode) ls_bytecode_destroy(bytecode);
		bytecode = rhs.bytecode;
		runtime = rhs.runtime;
		rhs.bytecode = nullptr;
		rhs.runtime = nullptr;
		return *this;
	}

	operator bool() const { return runtime != nullptr; }
	operator ls_runtime*() const { return runtime; }
	ls_runtime* get() const { return runtime; }

	ls_bytecode* bytecode = nullptr;
	ls_runtime* runtime = nullptr;
};

static void testPrint(void* userdata, ls_string_view msg) {
	TestContext* context = (TestContext*)userdata;
	if (!context->diagnostics.output_enabled) return;
	for (const char* c = msg.begin; c != msg.end; ++c) {
		putchar(*c);
	}
}

static int resolveLumScriptImportC(void* userdata, ls_string_view path, ls_string_view, ls_string_view* source) {
	const LumScriptImportFiles* imports = (const LumScriptImportFiles*)userdata;
	if (!imports) return 0;
	span<const LumScriptImportFile> files(imports->files, imports->count);
	for (const LumScriptImportFile& file : files) {
		if (equalStrings(file.path, path)) {
			*source = toLs(file.source);
			return 1;
		}
	}
	return 0;
}

static ls_result setNativeFunctionCallback(ls_runtime* runtime, ls_module* module, ls_string_view name, ls_native_fn callback) {
	for (int unit_index = 0, unit_count = ls_module_get_unit_count(module); unit_index < unit_count; ++unit_index) {
		ls_unit* unit = ls_module_get_unit(module, unit_index);
		const ls_string_view path = ls_unit_get_path(unit);
		for (int function_index = 0, function_count = ls_unit_get_native_function_count(unit); function_index < function_count; ++function_index) {
			const ls_string_view function_name = ls_unit_get_native_function_name(unit, function_index);
			if (equalStrings(name, function_name)) return ls_runtime_set_native_function_callback(runtime, unit, function_index, callback);
			if (size(name) != size(path) + 1u + size(function_name)) continue;
			if (compareMemory(data(name), data(path), size(path)) != 0) continue;
			if (data(name)[size(path)] != '.') continue;
			if (compareMemory(data(name) + size(path) + 1u, data(function_name), size(function_name)) != 0) continue;
			return ls_runtime_set_native_function_callback(runtime, unit, function_index, callback);
		}
	}
	return LS_RESULT_FAILURE;
}

static void nativeAddC(ls_runtime* runtime, ls_call_frame frame) {
	LS_ARG(frame, i32, a);
	LS_ARG(frame, i32, b);
	LS_RESULT(frame, a + b);
}

#include "bytecode_tests.inl"
#include "casts_tests.inl"
#include "types_tests.inl"
#include "operator_tests.inl"
#include "loop_tests.inl"
#include "import_tests.inl"
#include "array_tests.inl"
#include "slices_tests.inl"
#include "string_tests.inl"
#include "function_tests.inl"
#include "shadowing_tests.inl"
#include "declaration_tests.inl"
#include "comptime_tests.inl"
#include "control_flow_tests.inl"
#include "enum_tests.inl"
#include "nullable_tests.inl"
#include "ref_tests.inl"
#include "match_tests.inl"
#include "template_tests.inl"
#include "memory_tests.inl"
#include "temporaries_tests.inl"

int main(int argc, char** argv) {
    const char* test_name = nullptr;
    if (argc >= 2) {
        if (strcmp(argv[1], "--test") == 0) {
            if (argc < 3) {
                printf("Usage: %s [--test <name>]\n", argv[0]);
                return -1;
            }
            test_name = argv[2];
        }
        else if (argv[1][0] != '-') {
            test_name = argv[1];
        }
        else {
            printf("Usage: %s [--test <name>]\n", argv[0]);
            return -1;
        }
    }

    printf("Running LumScript tests...\n");
    if (test_name) {
        printf("Filtering to test: %s\n", test_name);
    }

    const auto start_time = std::chrono::steady_clock::now();
    bool found = false;
    for (TestList* test = TestList::first; test; test = test->next) {
        if (test_name && strcmp(test->name, test_name) != 0) continue;
        found = true;
        ++test_count;

		if (test->fn()) {
			++passed_count;
		}
		else {
			printf("FAILED: %s\n", test->name);
		}
	}

    if (test_name && !found) {
        printf("No test named '%s' found.\n", test_name);
        return -1;
    }

    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    printf("%d/%d tests passed in %lld ms\n", passed_count, test_count, (long long)elapsed_ms);
    return passed_count == test_count ? 0 : -1;
}
