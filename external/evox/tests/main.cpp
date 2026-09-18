#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "../compiler.h"
#include <chrono>
#include <vector>
#include <unordered_map>

#include "../arena.h"
#include "../utils.h"
#include "../bytecode.h"
#include "../capi.h"
#include "../ir.h"

void print(const char* val) { printf("%s", val); }
void print(int val) { printf("%d", val); }

#define EXPECT_EQ(expected, actual) \
	do { \
		auto expected_value = (expected); \
		auto actual_value = (actual); \
		if (expected_value != actual_value) { \
			if constexpr (sizeof(expected_value) == 8) { \
				printf("TEST FAILED at %s:%d: Expected: %lld, Actual: %lld\n", __FILE__, __LINE__, (long long)expected_value, (long long)actual_value); \
			} else { \
				printf("TEST FAILED at %s:%d: Expected: %d, Actual: %d\n", __FILE__, __LINE__, (int)expected_value, (int)actual_value); \
			} \
			return false; \
		} \
	} while (false)

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
		ex_module* module = ex_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		bool compiled = ex_module_compile(module, toLs(src), makeStringView(__func__), nullptr, nullptr); \
		ex_bytecode* bytecode = compiled ? ex_bytecode_compile(module, &context.host, nullptr) : nullptr; \
		if (bytecode) ex_bytecode_destroy(bytecode); \
		ex_module_destroy(module); \
		EXPECT_TRUE(compiled); \
		EXPECT_TRUE(bytecode != nullptr); \
	} while(false)

#define EXPECT_COMPILE_FAIL(src) \
	do { \
		TestContext context; \
		ex_module* module = ex_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		context.diagnostics.output_enabled = false; \
		EXPECT_TRUE(!ex_module_compile(module, toLs(src), makeStringView(__func__), nullptr, nullptr)); \
		ex_module_destroy(module); \
	} while(false)

#define EXPECT_COMPILE_WITH_IMPORTS(src, files) \
	do { \
		TestContext context; \
		ex_module* module = ex_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		bool compiled = ex_module_compile(module, toLs(src), makeStringView(__func__), &resolveEvoxImportC, &(files)); \
		ex_bytecode* bytecode = compiled ? ex_bytecode_compile(module, &context.host, nullptr) : nullptr; \
		if (bytecode) ex_bytecode_destroy(bytecode); \
		ex_module_destroy(module); \
		EXPECT_TRUE(compiled); \
		EXPECT_TRUE(bytecode != nullptr); \
	} while(false)

#define EXPECT_COMPILE_FAIL_WITH_IMPORTS(src, files) \
	do { \
		TestContext context; \
		ex_module* module = ex_module_create(&context.host); \
		EXPECT_TRUE(module != nullptr); \
		context.diagnostics.output_enabled = false; \
		EXPECT_TRUE(!ex_module_compile(module, toLs(src), makeStringView(__func__), &resolveEvoxImportC, &(files))); \
		ex_module_destroy(module); \
	} while(false)

#define EXPECT_RUNTIME_WITH_IMPORTS(src, files, runtime_name, body) \
	do { \
		CAPI_BEGIN(module, diagnostics); \
		EXPECT_TRUE(ex_module_compile(module, toLs(src), makeStringView(__func__), &resolveEvoxImportC, &(files))); \
		CAPI_RUNTIME(module, runtime_name); \
		body; \
		CAPI_END(module); \
	} while(false)

struct EvoxImportFile {
	ex_string_view path;
	ex_string_view source;
};

struct EvoxImportFiles {
	const EvoxImportFile* files = nullptr;
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
	bool name()

static int test_count = 0;
static int passed_count = 0;

static void testPrint(void* userdata, ex_string_view msg);

static ex_string_view toLs(ex_string_view value) { return value; }
static ex_string_view toLs(const char* value) { return makeStringView(value); }

struct TestContext {
	TestContext() {
		ex_default_arena_create(&host.arena);
		host.diagnostics_userdata = &diagnostics;
		host.print = &testPrint;
	}

	~TestContext() { ex_default_arena_destroy(&host.arena); }

	struct Diagnostics {
		bool output_enabled = true;
		u32 size = 0;
	} diagnostics;

	ex_host host = {};
};

static std::unordered_map<ex_runtime*, ex_task*>& test_tasks() {
	static std::unordered_map<ex_runtime*, ex_task*> value;
	return value;
}
static std::unordered_map<ex_task*, std::vector<u8>>& test_task_args() {
	static std::unordered_map<ex_task*, std::vector<u8>> value;
	return value;
}

struct RuntimeGuard {
	explicit RuntimeGuard(ex_module* module, ex_host* host, bool optimize = false)
		: bytecode(compile(module, host, optimize))
		, runtime(bytecode ? ex_runtime_create(bytecode, nullptr) : nullptr)
		, task(runtime ? ex_task_create(runtime) : nullptr) {
		if (runtime && task) test_tasks()[runtime] = task;
	}


	~RuntimeGuard() {
		if (runtime) {
			auto it = test_tasks().find(runtime);
			if (it != test_tasks().end() && it->second == task) test_tasks().erase(it);
		}
		if (task) {
			test_task_args().erase(task);
			ex_task_destroy(task);
		}
		if (runtime) ex_runtime_destroy(runtime);
		if (bytecode) ex_bytecode_destroy(bytecode);
	}

	static ex_bytecode* compile(ex_module* module, ex_host* host, bool optimize) {
		ex_bytecode_compile_options options = { optimize };
		return ex_bytecode_compile(module, host, optimize ? &options : nullptr);
	}

	RuntimeGuard(const RuntimeGuard&) = delete;
	RuntimeGuard& operator=(const RuntimeGuard&) = delete;

	RuntimeGuard(RuntimeGuard&& rhs) noexcept
		: bytecode(rhs.bytecode), runtime(rhs.runtime), task(rhs.task) {
		rhs.bytecode = nullptr;
		rhs.runtime = nullptr;
		rhs.task = nullptr;
	}

	RuntimeGuard& operator=(RuntimeGuard&& rhs) noexcept {
		if (this == &rhs) return *this;
		if (runtime) {
			auto it = test_tasks().find(runtime);
			if (it != test_tasks().end() && it->second == task) test_tasks().erase(it);
		}
		if (task) {
			test_task_args().erase(task);
			ex_task_destroy(task);
		}
		if (runtime) ex_runtime_destroy(runtime);
		if (bytecode) ex_bytecode_destroy(bytecode);
		bytecode = rhs.bytecode;
		runtime = rhs.runtime;
		task = rhs.task;
		rhs.bytecode = nullptr;
		rhs.runtime = nullptr;
		rhs.task = nullptr;
		return *this;
	}

	void reset_task() {
		test_tasks().erase(runtime);
		test_task_args().erase(task);
		ex_task_destroy(task);
		task = ex_task_create(runtime);
		test_tasks()[runtime] = task;
	}

	operator bool() const { return task != nullptr; }
	operator ex_task*() const { return task; }
	ex_task* get() const { return task; }

	template <typename T>
	void push(T value) {
		const u8* bytes = reinterpret_cast<const u8*>(&value);
		args.insert(args.end(), bytes, bytes + sizeof(value));
	}

	void pushString(ex_string_view value) {
		struct Slice { const void* data; i64 length; } slice = { value.begin, value.length };
		push(slice);
	}

	ex_call_result call(ex_string_view name) {
		ex_call_result result = ex_call(task, name, args.empty() ? nullptr : args.data(), (u32)args.size());
		args.clear();
		return result;
	}

	ex_bytecode* bytecode = nullptr;
	ex_runtime* runtime = nullptr;
	ex_task* task = nullptr;
	std::vector<u8> args;
};

static ex_task* test_task_for_runtime(ex_runtime* runtime) {
	auto found = test_tasks().find(runtime);
	if (found != test_tasks().end()) return found->second;
	ex_task* task = ex_task_create(runtime);
	test_tasks()[runtime] = task;
	return task;
}

static ex_call_result ex_task_resume(ex_runtime* runtime, const ex_type* type, const void* data, u32 size) {
	return ex_task_resume(test_task_for_runtime(runtime), type, data, size);
}
static void test_abort(RuntimeGuard& runtime) { runtime.reset_task(); }
static ex_call_result test_call(RuntimeGuard& runtime, ex_string_view name) { return runtime.call(name); }
static ex_call_result test_call(ex_runtime* runtime, ex_string_view name) {
	ex_task* task = test_task_for_runtime(runtime);
	std::vector<u8>& args = test_task_args()[task];
	ex_call_result result = ex_call(task, name, args.empty() ? nullptr : args.data(), (u32)args.size());
	args.clear();
	return result;
}
static void test_push_bytes(ex_task* task, const void* data, size_t size) {
	const u8* bytes = reinterpret_cast<const u8*>(data);
	test_task_args()[task].insert(test_task_args()[task].end(), bytes, bytes + size);
}
static void test_push_bool(RuntimeGuard& runtime, int value) { runtime.push((u8)(value ? 1 : 0)); }
static void test_push_i32(RuntimeGuard& runtime, i32 value) { runtime.push(value); }
static void test_push_u32(RuntimeGuard& runtime, u32 value) { runtime.push(value); }
static void test_push_i64(RuntimeGuard& runtime, i64 value) { runtime.push(value); }
static void test_push_u64(RuntimeGuard& runtime, u64 value) { runtime.push(value); }
static void test_push_f32(RuntimeGuard& runtime, float value) { runtime.push(value); }
static void test_push_f64(RuntimeGuard& runtime, double value) { runtime.push(value); }
static void test_push_string(RuntimeGuard& runtime, ex_string_view value) { runtime.pushString(value); }
static void test_push_bool(ex_runtime* runtime, int value) { u8 v = value ? 1 : 0; test_push_bytes(test_task_for_runtime(runtime), &v, sizeof(v)); }
static void test_push_i32(ex_runtime* runtime, i32 value) { test_push_bytes(test_task_for_runtime(runtime), &value, sizeof(value)); }
static void test_push_u32(ex_runtime* runtime, u32 value) { test_push_bytes(test_task_for_runtime(runtime), &value, sizeof(value)); }
static void test_push_i64(ex_runtime* runtime, i64 value) { test_push_bytes(test_task_for_runtime(runtime), &value, sizeof(value)); }
static void test_push_u64(ex_runtime* runtime, u64 value) { test_push_bytes(test_task_for_runtime(runtime), &value, sizeof(value)); }
static void test_push_f32(ex_runtime* runtime, float value) { test_push_bytes(test_task_for_runtime(runtime), &value, sizeof(value)); }
static void test_push_f64(ex_runtime* runtime, double value) { test_push_bytes(test_task_for_runtime(runtime), &value, sizeof(value)); }
static void test_push_string(ex_runtime* runtime, ex_string_view value) {
	struct Slice { const void* data; i64 length; } slice = { value.begin, value.length };
	test_push_bytes(test_task_for_runtime(runtime), &slice, sizeof(slice));
}
static ex_runtime* test_vm(RuntimeGuard& runtime) { return runtime.runtime; }
static ex_runtime* test_vm(ex_runtime* runtime) { return runtime; }
static i32 test_global_i32(ex_runtime* runtime, u32 index) {
	u32 size = 0u;
	const void* value = ex_debug_global_value(runtime, index, &size);
	i32 result = 0;
	if (value && size == sizeof(result)) memcpy(&result, value, sizeof(result));
	return result;
}

static const ex_type* ex_type_from_any(RuntimeGuard& runtime, const void* value) { return ex_type_from_any(runtime.runtime, value); }

static void test_runtime_destroy(ex_runtime* runtime) {
	auto it = test_tasks().find(runtime);
	if (it != test_tasks().end()) {
		test_task_args().erase(it->second);
		ex_task_destroy(it->second);
		test_tasks().erase(it);
	}
	ex_runtime_destroy(runtime);
}

#define TEST_TASK_ACCESSOR(name, type) \
	static type name(ex_runtime* runtime, i32 index) { return name(test_task_for_runtime(runtime), index); }
TEST_TASK_ACCESSOR(ex_task_to_bool, i32)
TEST_TASK_ACCESSOR(ex_task_to_i8, i8)
TEST_TASK_ACCESSOR(ex_task_to_u8, u8)
TEST_TASK_ACCESSOR(ex_task_to_i16, i16)
TEST_TASK_ACCESSOR(ex_task_to_u16, u16)
TEST_TASK_ACCESSOR(ex_task_to_i32, i32)
TEST_TASK_ACCESSOR(ex_task_to_u32, u32)
TEST_TASK_ACCESSOR(ex_task_to_i64, i64)
TEST_TASK_ACCESSOR(ex_task_to_u64, u64)
TEST_TASK_ACCESSOR(ex_task_to_f32, float)
TEST_TASK_ACCESSOR(ex_task_to_f64, double)
TEST_TASK_ACCESSOR(ex_task_to_string, ex_string_view)
TEST_TASK_ACCESSOR(ex_task_to_ptr, void*)
#undef TEST_TASK_ACCESSOR
static const void* ex_task_result(ex_runtime* runtime, u32* size) { return ex_task_result(test_task_for_runtime(runtime), size); }

static ex_task_state ex_task_get_state(ex_runtime* runtime) { return ex_task_get_state(test_task_for_runtime(runtime)); }
static ex_result ex_debug_pause_event(ex_runtime* runtime, ex_debug_event* event) { return ex_debug_pause_event(test_task_for_runtime(runtime), event); }
static ex_call_result ex_debug_resume(ex_runtime* runtime, ex_debug_action action) { return ex_debug_resume(test_task_for_runtime(runtime), action); }
static u32 ex_debug_stack_depth(ex_runtime* runtime) { return ex_debug_stack_depth(test_task_for_runtime(runtime)); }
static ex_string_view ex_debug_frame_function_name(ex_runtime* runtime, u32 index) { return ex_debug_frame_function_name(test_task_for_runtime(runtime), index); }
static ex_result ex_debug_frame_location(ex_runtime* runtime, u32 index, ex_debug_location* location) { return ex_debug_frame_location(test_task_for_runtime(runtime), index, location); }
static u32 ex_debug_frame_local_count(ex_runtime* runtime, u32 frame) { return ex_debug_frame_local_count(test_task_for_runtime(runtime), frame); }
static ex_string_view ex_debug_local_name(ex_runtime* runtime, u32 frame, u32 local) { return ex_debug_local_name(test_task_for_runtime(runtime), frame, local); }
static void* ex_debug_local_value(ex_runtime* runtime, u32 frame, u32 local, u32* size) { return ex_debug_local_value(test_task_for_runtime(runtime), frame, local, size); }
static const ex_type* ex_debug_local_type(ex_runtime* runtime, u32 frame, u32 local) { return ex_debug_local_type(test_task_for_runtime(runtime), frame, local); }

static void testPrint(void* userdata, ex_string_view msg) {
	TestContext* context = (TestContext*)userdata;
	context->diagnostics.size += (u32)(msg.length);
	if (!context->diagnostics.output_enabled) return;
	for (u64 i = 0; i < msg.length; ++i) putchar(msg.begin[i]);
}

static int resolveEvoxImportC(void* userdata, ex_string_view path, ex_string_view, ex_string_view* source) {
	const EvoxImportFiles* imports = (const EvoxImportFiles*)userdata;
	if (!imports) return 0;
	span<const EvoxImportFile> files(imports->files, imports->count);
	for (const EvoxImportFile& file : files) {
		if (equalStrings(file.path, path)) {
			*source = toLs(file.source);
			return 1;
		}
	}
	return 0;
}

static void nativeAddC(ex_runtime* runtime, ex_call_frame frame) {
	EX_ARG(frame, i32, a);
	EX_ARG(frame, i32, b);
	EX_RESULT(frame, a + b);
}

static ex_call_result g_reentrant_call_result = EX_CALL_RESULT_RUNTIME_ERROR;

static void nativeReenter(ex_runtime* runtime, ex_call_frame) {
	g_reentrant_call_result = ex_call(test_task_for_runtime(runtime), makeStringView("main"), nullptr, 0);
}

#include "bytecode_tests.inl"
#include "ir_tests.inl"
#include "debugger_tests.inl"
#include "capi_tests.inl"
#include "casts_tests.inl"
#include "types_tests.inl"
#include "operator_tests.inl"
#include "loop_tests.inl"
#include "import_tests.inl"
#include "array_tests.inl"
#include "tuple_tests.inl"
#include "slices_tests.inl"
#include "string_tests.inl"
#include "function_tests.inl"
#include "variadic_tests.inl"
#include "shadowing_tests.inl"
#include "declaration_tests.inl"
#include "comptime_tests.inl"
#include "introspection_tests.inl"
#include "attributes_tests.inl"
#include "control_flow_tests.inl"
#include "enum_tests.inl"
#include "nullable_tests.inl"
#include "else_guard_tests.inl"
#include "union_tests.inl"
#include "pointer_tests.inl"
#include "match_tests.inl"
#include "template_tests.inl"
#include "memory_tests.inl"
#include "temporaries_tests.inl"
#include "any_tests.inl"
#include "interpolation_tests.inl"
#include "yield_tests.inl"

int main(int argc, char** argv) {
	const char* test_name = nullptr;
	if (argc >= 2) {
		if (strcmp(argv[1], "--test") == 0) {
			if (argc < 3) {
				printf("Usage: %s [--test <name>]\n", argv[0]);
				return -1;
			}
			test_name = argv[2];
		} else if (argv[1][0] != '-') {
			test_name = argv[1];
		} else {
			printf("Usage: %s [--test <name>]\n", argv[0]);
			return -1;
		}
	}
	printf("Running Evox tests...\n"); fflush(stdout);
	if (test_name) printf("Filtering to test: %s\n", test_name);
	const auto start_time = std::chrono::steady_clock::now();
	bool found = false;
	for (TestList* test = TestList::first; test; test = test->next) {
		if (test_name && strcmp(test->name, test_name) != 0) continue;
		found = true;
		++test_count;
		if (test->fn()) ++passed_count;
		else printf("FAILED: %s\n\n", test->name);
	}
	if (test_name && !found) {
		printf("No test named '%s' found.\n", test_name);
		return -1;
	}
	const auto end_time = std::chrono::steady_clock::now();
	const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
	printf("%d/%d tests passed (%d failed) in %lld ms\n", passed_count, test_count, test_count - passed_count, (long long)elapsed_ms);
	return passed_count == test_count ? 0 : -1;
}
