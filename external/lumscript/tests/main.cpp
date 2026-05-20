#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/string.h"
#include "../capi.h"

using namespace Lumix;

void print(const char* val) { printf(val); }
void print(int val) { printf("%d", val); }

#define EXPECT_EQ(expected, actual) \
	if ((expected) != (actual)) { \
		printf("TEST FAILED at %s:%d: Expected: %d, Actual: %d\n", __FILE__, __LINE__, expected, actual); \
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
		bool compiled = ls_module_compile(module, toLs(src), {}, &context.host, nullptr, nullptr); \
		ls_module_destroy(module); \
		EXPECT_TRUE(compiled); \
	} while(false)

#define EXPECT_COMPILE_FAIL(src) \
	do { \
		TestContext context; \
		ls_module* module = ls_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		context.diagnostics.output_enabled = false; \
		EXPECT_TRUE(!ls_module_compile(module, toLs(src), {}, &context.host, nullptr, nullptr)); \
		ls_module_destroy(module); \
		EXPECT_TRUE(context.diagnostics.has_error); \
	} while(false)

struct LumScriptImportFile {
	StringView path;
	StringView source;
};

struct LumScriptImportFiles {
	const LumScriptImportFile* files = nullptr;
	u32 count = 0;
};

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

static ls_string_view toLs(StringView value) {
	return { value.begin, value.end };
}

struct TestContext {
	TestContext() {
		host.diagnostics_userdata = &diagnostics;
		host.print = &testPrint;
		host.has_error = 0;
	}

	struct Diagnostics {
		bool output_enabled = true;
		bool has_error = false;
	} diagnostics;

	ls_host host = {};
};

struct RuntimeGuard {
	explicit RuntimeGuard(ls_module* module)
		: runtime(ls_runtime_create(module))
	{}

	~RuntimeGuard() {
		if (runtime) ls_runtime_destroy(runtime);
	}

	RuntimeGuard(const RuntimeGuard&) = delete;
	RuntimeGuard& operator=(const RuntimeGuard&) = delete;

	RuntimeGuard(RuntimeGuard&& rhs) noexcept
		: runtime(rhs.runtime)
	{
		rhs.runtime = nullptr;
	}

	RuntimeGuard& operator=(RuntimeGuard&& rhs) noexcept {
		if (this == &rhs) return *this;
		if (runtime) ls_runtime_destroy(runtime);
		runtime = rhs.runtime;
		rhs.runtime = nullptr;
		return *this;
	}

	operator bool() const { return runtime != nullptr; }
	operator ls_runtime*() const { return runtime; }
	ls_runtime* get() const { return runtime; }

	ls_runtime* runtime = nullptr;
};

static void testPrint(void* userdata, ls_string_view msg) {
	TestContext* context = (TestContext*)userdata;
	context->diagnostics.has_error = true;
	context->host.has_error = 1;
	if (!context->diagnostics.output_enabled) return;
	for (const char* c = msg.begin; c != msg.end; ++c) {
		putchar(*c);
	}
}

static int resolveLumScriptImportC(void* userdata, ls_string_view path, ls_string_view, ls_string_view* source) {
	const LumScriptImportFiles* imports = (const LumScriptImportFiles*)userdata;
	if (!imports) return 0;
	Span<const LumScriptImportFile> files(imports->files, imports->count);
	for (const LumScriptImportFile& file : files) {
		if (equalStrings(file.path, StringView(path.begin, path.end))) {
			*source = toLs(file.source);
			return 1;
		}
	}
	return 0;
}

static int nativeAddC(const ls_value* args, size_t arg_count, ls_value* result, void*) {
	if (arg_count < 2) return 0;
	if (result) *result = ls_value_make_i32(args[0].i + args[1].i);
	return 1;
}

#include "compiler_tests.inl"
#include "runtime_tests.inl"

int main() {
    printf("Running LumScript tests...\n");
    for (TestList* test = TestList::first; test; test = test->next) {
        ++test_count;

		if (test->fn()) {
			++passed_count;
		}
		else {
			printf("FAILED: %s\n", test->name);
		}
	}
    printf("%d/%d tests passed\n", passed_count, test_count);
    return passed_count == test_count ? 0 : -1;
}
