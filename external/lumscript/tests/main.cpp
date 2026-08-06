#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../compiler.h"
#include "../mir_builder.h"
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
		ls_bytecode* bytecode = compiled ? ls_bytecode_compile_mir(module, &context.host) : nullptr; \
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
		ls_bytecode* bytecode = compiled ? ls_bytecode_compile_mir(module, &context.host) : nullptr; \
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
		u32 size = 0;
	} diagnostics;

	ls_host host = {};
};

struct RuntimeGuard {
	// The tests run through the bytecode runtime, so this guard owns both the
	// compiled bytecode and the runtime bound to it.
	explicit RuntimeGuard(ls_module* module, ls_host* host)
		: bytecode(ls_bytecode_compile_mir(module, host))
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
	context->diagnostics.size += (u32)(msg.end - msg.begin);
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

TEST(MIRBuildScalarReturn) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : i32 { return 2 + 3; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_test"), nullptr, nullptr));

	FunctionExpression* main_function = nullptr;
	for (Unit& unit : module->units) {
		for (Symbol& symbol : unit.symbols) {
			if (symbol.expression && symbol.expression->kind == Expression::FUNCTION && equalStrings(symbol.name, makeStringView("main"))) {
				main_function = static_cast<FunctionExpression*>(symbol.expression);
				break;
			}
		}
	}
	EXPECT_TRUE(main_function != nullptr);
	MirFunction* mir = mirBuildFunction(context.host.arena, main_function);
	EXPECT_TRUE(mir != nullptr);
	EXPECT_TRUE(mir->blocks.size() == 1);
	MirBlock& block = mir->blocks[0];
	EXPECT_TRUE(block.instructions.size() == 3);
	EXPECT_TRUE(block.instructions[0]->opcode == MIR_OP_CONST);
	EXPECT_TRUE(block.instructions[1]->opcode == MIR_OP_CONST);
	EXPECT_TRUE(block.instructions[2]->opcode == MIR_OP_ADD);
	EXPECT_TRUE(block.has_terminator);
	EXPECT_TRUE(block.terminator.kind == MIR_TERM_RETURN_VALUE);
	EXPECT_TRUE(block.terminator.value == block.instructions[2]->result);

	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeF32Cast) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn to_f32() : f32 { const x : i32 = 10; return x as f32; } fn to_i32() : i32 { const x : f32 = 12.75; return x as i32; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_f32_cast_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("to_f32")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_f32(runtime, -1) == 10);
	EXPECT_TRUE(ls_call(runtime, makeStringView("to_i32")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 12);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeScalarReturn) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : i32 { return 20 + 22; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_runtime_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 42);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeIntegerExpressions) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : i32 { return -(20 - 2 * 3); }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_integer_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == -14);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeLocalAndParameter) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main(v : i32) : i32 { var x : i32 = v + 2; return x * 3; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_local_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	ls_push_i32(runtime, 4);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 18);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeIf) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main(v : bool) : i32 { if v { return 1; } return 2; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_if_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	ls_push_bool(runtime, true);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 1);
	ls_push_bool(runtime, false);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 2);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeWhile) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : i32 { while false {} return 7; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_while_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 7);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeCompare) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main(v : i32) : bool { return v >= 10; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_compare_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	ls_push_i32(runtime, 10);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	ls_push_i32(runtime, 9);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(!ls_to_bool(runtime, -1));
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeDefer) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : i32 { var x : i32 = 1; defer x += 2; return x; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_defer_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 1);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeCast) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : i64 { return 3 as i64; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_cast_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i64(runtime, -1) == 3);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeDirectCall) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn add(a : i32, b : i32) : i32 { return a + b; } fn main() : i32 { return add(20, 22); }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_call_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 42);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeFloatExpressions) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : f64 { return 1.5 + 2.25; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_float_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_f64(runtime, -1) == 3.75);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeFloatCall) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn add(a : f64, b : f64) : f64 { return a + b; } fn main() : f64 { return add(1.5, 2.25); }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_float_call_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_f64(runtime, -1) == 3.75);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeLoopState) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : i32 { var i : i32 = 0; var sum : i32 = 0; while i < 3 { sum += i; i += 1; } return sum; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_loop_state_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 3);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeGlobalRead) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "var g : i32 = 5; fn main() : i32 { return g; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_global_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 5);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeShortCircuit) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : bool { return false and (1 / 0 == 0); }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_short_circuit_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(!ls_to_bool(runtime, -1));
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeTernary) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main(v : bool) : i32 { return v ? 11 : 22; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_ternary_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	ls_push_bool(runtime, true);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 11);
	ls_push_bool(runtime, false);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 22);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(MIRRuntimeBreakContinue) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	const char* source = "fn main() : i32 { var i : i32 = 0; var sum : i32 = 0; while true { i += 1; if i == 2 { continue; } if i == 4 { break; } sum += i; } return sum; }";
	EXPECT_TRUE(ls_module_compile(module, makeStringView(source), makeStringView("mir_break_continue_test"), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile_mir(module, &context.host);
	EXPECT_TRUE(bytecode != nullptr);
	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, makeStringView("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_to_i32(runtime, -1) == 4);
	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

#include "bytecode_tests.inl"
#include "debugger_tests.inl"
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
#include "introspection_tests.inl"
#include "control_flow_tests.inl"
#include "enum_tests.inl"
#include "nullable_tests.inl"
#include "union_tests.inl"
#include "pointer_tests.inl"
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
			printf("FAILED: %s\n\n", test->name);
		}
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
