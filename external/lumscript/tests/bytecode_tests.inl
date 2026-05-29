#define CAPI_BEGIN(module_name, diagnostics_name) \
	TestContext diagnostics_name; \
	ls_module* module_name = ls_module_create(&diagnostics_name.host); \
	EXPECT_TRUE(module_name != nullptr); \
	auto& module_host = diagnostics_name.host; \
	auto& test_diagnostics = diagnostics_name.diagnostics

#define CAPI_END(module_name) \
	do {} while (false)

#define CAPI_RUNTIME(module_name, runtime_name) \
	RuntimeGuard runtime_name(module_name, &module_host); \
	EXPECT_TRUE(runtime_name)

TEST(BytecodeCompileAndRunMain) {
	const char* source = R"(
		fn main() : i32 {
			return 42;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(ExternImport) {
	const char* main_source = R"(
		import "math" as m

		fn main() : i32 {
			const v1 : m.Vec2 = m.Vec2 { 10, 11 };
			const s1 : i32 = m.sum(v1); // with namespace
			const v2 : m.Vec2 = m.Vec2 { 9, 12 };
			const s2 : i32 = sum(v2); // inferred namespaced
			return s1 + s2;
		}
	)";
	
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		extern fn sum(v : Vec2) : i32;
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &resolveLumScriptImportC, &files));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &diagnostics.host);
	EXPECT_TRUE(bytecode != nullptr);

	const i32 sum_fn_idx = ls_module_get_native_function_index(module, toLs("math.sum"));

	auto sumfn = [](ls_runtime* runtime) -> void {
		const i32 s = ls_to_i32(runtime, -1) + ls_to_i32(runtime, -2);
		ls_push_i32(runtime, s);
	};

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	ls_runtime_set_native_function_callback(runtime, sum_fn_idx, sumfn);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(StructExtern) {
	const char* source = R"(
		struct Entity {
			index : i32;
			world : cptr;
		};

		extern fn create() : Entity;

		fn main() : Entity {
			var v : Entity = create();
			return v;
		}
	)";
	CAPI_BEGIN(module, diagnostics);

	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	static int ptr;

	auto create_fn = [](ls_runtime* runtime) -> void {
		ls_push_i32(runtime, 42);
		ls_push_ptr(runtime, &ptr);
	};

	CAPI_RUNTIME(module, runtime);
	const i32 fn_idx = ls_module_get_native_function_index(module, toLs("create"));
	
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, fn_idx, create_fn) == LS_RESULT_OK);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -2));
	EXPECT_TRUE(&ptr == ls_to_ptr(runtime, -1));
	CAPI_END(module);
	
	return true;
}

TEST(Extern) {
	const char* source = R"(
		extern fn nativefn() : i32;
		extern fn nativefn2(v : i32) : i32;

		fn main() : i32 {
			var v : i32 = nativefn();
			v = nativefn2(v);
			return v;
		}
	)";
	CAPI_BEGIN(module, diagnostics);

	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	auto nativefn = [](ls_runtime* runtime) -> void {
		ls_push_i32(runtime, 41);
	};

	auto nativefn2 = [](ls_runtime* runtime) -> void {
		i32 v = ls_to_i32(runtime, -1);
		ls_push_i32(runtime, v + 1);
	};

	CAPI_RUNTIME(module, runtime);
	const i32 fn_idx = ls_module_get_native_function_index(module, toLs("nativefn"));
	const i32 fn2_idx = ls_module_get_native_function_index(module, toLs("nativefn2"));
	
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, fn_idx, nativefn) == LS_RESULT_OK);
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, fn2_idx, nativefn2) == LS_RESULT_OK);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	
	return true;
}

TEST(NativePtr) {
	const char* source = R"(
		fn main() : void {
			var x = create();
			test(x);
		}
	)";

	CAPI_BEGIN(module, diagnostics);

	const int idx = ls_module_add_native_type(module, toLs("Entity"), toLs("engine:entity/Entity"));
	const ls_type entity_type = ls_type_make_native(toLs("Entity"), idx, 0);
	
	const i32 create_fn_idx = ls_module_add_native_function(module, ls_make_qualified_name(module, toLs(""), toLs("create")), entity_type, nullptr, 0);
	const ls_type params[] = { entity_type };
	const i32 test_fn_idx = ls_module_add_native_function(module, ls_make_qualified_name(module, toLs(""), toLs("test")), ls_type_make(LS_TYPE_VOID), params, 1);
	
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	static int ptr;
	static bool matches = false;

	auto createfn = [](ls_runtime* runtime) -> void {
		ls_push_ptr(runtime, &ptr);
	};

	auto testfn = [](ls_runtime* runtime) -> void {
		void* t = ls_to_ptr(runtime, -1);
		matches = t == &ptr;
	};

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, create_fn_idx, createfn) == LS_RESULT_OK);
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, test_fn_idx, testfn) == LS_RESULT_OK);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_TRUE(matches);
	CAPI_END(module);
	
	return true;
}

TEST(testNativeFunctionCall) {
	const char* source = R"(
		fn main() : i32 {
			return native_add(20, 22);
		}
	)";

	CAPI_BEGIN(module, diagnostics);

	ls_type params[] = {ls_type_make(LS_TYPE_I32), ls_type_make(LS_TYPE_I32)};
	const int native_add = ls_module_add_native_function(module, toLs("native_add"), ls_type_make(LS_TYPE_I32), params, 2);
	EXPECT_TRUE(native_add >= 0);

	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, native_add, &nativeAddC) == LS_RESULT_OK);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(CoreMathImportRuntime) {
	const char* source = R"(
		import "core:math" as math

		fn sin32() : f32 {
			return math.sin(0.0);
		}

		fn cos32() : f32 {
			return math.cos(0.0);
		}

		fn sin64() : f64 {
			return math.sin_f64(0.0);
		}

		fn cos64() : f64 {
			return math.cos_f64(0.0);
		}

		fn sqrt32() : f32 {
			return math.sqrt(9.0);
		}

		fn sqrt64() : f64 {
			return math.sqrt_f64(16.0);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("sin32"), 0, 1));
	EXPECT_FLOAT_EQ(0.0f, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("cos32"), 0, 1));
	EXPECT_FLOAT_EQ(1.0f, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("sin64"), 0, 1));
	EXPECT_FLOAT_EQ(0.0f, (float)ls_to_f64(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("cos64"), 0, 1));
	EXPECT_FLOAT_EQ(1.0f, (float)ls_to_f64(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("sqrt32"), 0, 1));
	EXPECT_FLOAT_EQ(3.0f, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("sqrt64"), 0, 1));
	EXPECT_FLOAT_EQ(4.0f, (float)ls_to_f64(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(BytecodeAddTwoConstants) {
	const char* source = R"(
		fn main() : i32 {
			return 1 + 2;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeFloatArithmetic) {
	const char* source = R"(
		fn main() : f32 {
			return 1.25 + 2.5;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_FLOAT_EQ(3.75f, ls_to_f32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeF64Arithmetic) {
	const char* source = R"(
		fn main() : f64 {
			return 1.5 + 2.25;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_FLOAT_EQ(3.75f, (float)ls_to_f64(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeExplicitCastNumeric) {
	const char* source = R"(
		fn main() : f32 {
			return 1 as f32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_FLOAT_EQ(1.0f, ls_to_f32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeExplicitCastEnumToInteger) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn main() : i32 {
			const s : State = .Running;
			return s as i32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeFunctionValueLocal) {
	const char* source = R"(
		fn add_one(v : i32) : i32 {
			return v + 1;
		}

		fn main() : i32 {
			const f : fn(i32) : i32 = add_one;
			return f(41);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);

	CAPI_END(module);
	return true;
}

TEST(BytecodeIndirectFunctionCall) {
	const char* source = R"(
		fn add_one(v : i32) : i32 {
			return v + 1;
		}

		fn apply(f : fn(i32) : i32, value : i32) : i32 {
			return f(value);
		}

		fn main() : i32 {
			return apply(add_one, 41);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);

	CAPI_END(module);
	return true;
}

TEST(BytecodeMultiplyExpression) {
	const char* source = R"(
		fn main() : i32 {
			return 6 * 7;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeDivideExpression) {
	const char* source = R"(
		fn main() : i32 {
			return 42 / 2;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(21, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeModuloExpression) {
	const char* source = R"(
		fn main() : i32 {
			return 42 % 5;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeMultiplyAssignment) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 6;
			value *= 7;
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeDivideAssignment) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 42;
			value /= 2;
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(21, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeStaticSizedArrayLocal) {
	const char* source = R"(
		fn main() : i32 {
			var values : i32[3] = undefined;
			return 42;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	CAPI_END(module);
	return true;
}

TEST(BytecodeStaticSizedArrayIndexing) {
	const char* source = R"(
		fn main() : i32 {
			var values : i32[3] = undefined;
			values[0] = 20;
			values[1] = 22;
			return values[0] + values[1];
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	CAPI_END(module);
	return true;
}

TEST(BytecodeCompoundAssignArrayIndexEvaluatedOnce) {
	const char* source = R"(
		var hits : i32 = 0;

		fn idx() : i32 {
			hits += 1;
			return 1;
		}

		fn main() : i32 {
			var values : i32[3] = undefined;
			values[1] = 41;
			values[idx()] += 1;
			return hits * 100 + values[1];
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(142, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeDeferRunsOnReturn) {
	const char* source = R"(
		var g : i32 = 1;

		fn main() : i32 {
			defer g = 2;
			return 7;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	EXPECT_EQ(2, ls_to_i32(runtime, 0));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeDeferLifoAcrossScopes) {
	const char* source = R"(
		var a : i32 = 0;
		var b : i32 = 0;

		fn main() : i32 {
			{
				defer a = 1;
				{
					defer b = a;
					return 0;
				}
			}
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	EXPECT_EQ(1, ls_to_i32(runtime, 0));
	EXPECT_EQ(0, ls_to_i32(runtime, 1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNullableLocalNullCheck) {
	const char* source = R"(
		fn main() : i32 {
			var value : ?i32 = null;
			if value == null {
				return 42;
			}
			return 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNullableStructComparison) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn main() : i32 {
			var v : ?Vec2 = null;
			if v != null {
				return v.x + v.y;
			}
			return 42;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNullableReturnNull) {
	const char* source = R"(
		fn main() : ?i32 {
			return null;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(0, ls_to_bool(runtime, -2));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	ls_runtime_destroy(runtime);

	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNullableReturnValue) {
	const char* source = R"(
		fn main() : ?i32 {
			return 7;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(1, ls_to_bool(runtime, -2));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	ls_runtime_destroy(runtime);

	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeRefParameterCall) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : i32 {
			var x : i32 = 41;
			increment(ref x);
			return x;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeRefParameterNestedFieldCall) {
	const char* source = R"(
		struct Stats {
			hp : i32;
		};

		struct Player {
			stats : Stats;
		};

		fn bump(v : ref i32) : void {
			v += 1;
		}

		fn main() : i32 {
			var p = Player { Stats { 10 } };
			bump(ref p.stats.hp);
			return p.stats.hp;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeRefParameterArrayCall) {
	const char* source = R"(
		fn bump(value : ref i32) : void {
			value += 2;
		}

		fn main() : i32 {
			var values : i32[3] = undefined;
			values[0] = 20;
			values[1] = 20;
			bump(ref values[1]);
			return values[0] + values[1];
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeRunFunctionWithF64ParameterFromStack) {
	const char* source = R"(
		fn main(x : f64) : f64 {
			return x + 0.5;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	ls_push_f64(runtime, 41.5);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 1, 1));
	EXPECT_FLOAT_EQ(42.0f, (float)ls_to_f64(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeF64Comparisons) {
	const char* source = R"(
		fn is_gt() : bool {
			return 2.25 > 1.5;
		}

		fn is_lt() : bool {
			return 1.5 < 2.25;
		}

		fn is_eq() : bool {
			return 3.75 == 3.75;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("is_gt"), 0, 1));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("is_lt"), 0, 1));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("is_eq"), 0, 1));
	EXPECT_TRUE(ls_to_bool(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNamespaceResolutionByFirstParameter) {
	const char* main_source = R"(
		import "math" as math

		fn main() : i32 {
			const v : math.Vec2 = math.Vec2 { 20, 22 };
			return sum(v);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &resolveLumScriptImportC, &files));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &diagnostics.host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(BytecodeNamespaceResolutionByFirstParameterChoosesNamespace) {
	const char* main_source = R"(
		import "math" as math
		import "geom" as geom

		fn main() : i32 {
			const a : math.Vec2 = math.Vec2 { 20, 22 };
			const b : geom.Vec2 = geom.Vec2 { 10, 32 };
			return sum(a) + sum(b);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	const char* geom_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y + 1;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) },
		{ toLs("geom"), toLs(geom_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &resolveLumScriptImportC, &files));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &diagnostics.host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(85, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	ls_module_destroy(module);
	return true;
}

TEST(BytecodeStructsBasic) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn main() : i32 {
			const v : Vec2 = Vec2 { 20, 22 };
			return v.x + v.y;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNestedStructs) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		struct Outer {
			pos : Vec2;
			z : i32;
		};

		fn main() : i32 {
			const v : Outer = Outer { Vec2 { 20, 22 }, 7 };
			return v.pos.x + v.pos.y + v.z;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(49, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeStructParameterPassing) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}

		fn main() : i32 {
			const v : Vec2 = Vec2 { 20, 22 };
			return sum(v);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeStructFieldAssignment) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn main() : i32 {
			var v : Vec2 = Vec2 { 1, 2 };
			v.x = 20;
			return v.x + v.y;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(22, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeStructFieldCompoundAssignment) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn main() : i32 {
			var v : Vec2 = Vec2 { 1, 2 };
			v.x += 5;
			return v.x + v.y;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(8, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeStructFieldAssignmentGlobal) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		var g : Vec2 = Vec2 { 1, 2 };

		fn main() : i32 {
			g.x = 20;
			return g.x + g.y;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(22, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeStructFieldAssignmentParameterLocalCopy) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn bump(v : Vec2) : i32 {
			var tmp : Vec2 = v;
			tmp.x = 20;
			return tmp.x + tmp.y;
		}

		fn main() : i32 {
			const v : Vec2 = Vec2 { 1, 2 };
			return bump(v);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(22, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeEnumBasicUsage) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn main() : i32 {
			const s : State = .Running;
			if s == .Running {
				return 42;
			}
			return 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeEnumMatch) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn main() : i32 {
			const s : State = .Running;
			match s {
				case .Idle:
					return 1;
				case .Running:
					return 2;
			}
			return 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeIntegerOverflowWraps) {
	const char* source = R"(
		fn add_i8_wrap() : i8 {
			var x : i8 = 127;
			x += 1;
			return x;
		}

		fn add_u8_wrap() : u8 {
			var x : u8 = 255;
			x += 1;
			return x;
		}

		fn sub_i8_wrap() : i8 {
			var x : i8 = 0;
			x -= 1;
			return x;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("add_i8_wrap"), 0, 1));
	EXPECT_EQ(-128, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("add_u8_wrap"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("sub_i8_wrap"), 0, 1));
	EXPECT_EQ(-1, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeCompileFunctionWithParameter) {
	const char* source = R"(
		fn main(x : i32) : i32 {
			return x + 1;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeRunFunctionWithParameterFromStack) {
	const char* source = R"(
		fn main(x : i32) : i32 {
			return x + 1;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	ls_push_i32(runtime, 41);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 1, 1));

	i32 result = ls_to_i32(runtime, -1);
	EXPECT_EQ(42, result);

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeFunctionCallWorks) {
	const char* source = R"(
		fn add(a : i32, b : i32) : i32 {
			return a + b;
		}

		fn main() : i32 {
			return add(20, 22);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);

	CAPI_END(module);
	return true;
}

TEST(BytecodeWhileBreakContinue) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			var sum : i32 = 0;

			while i < 10 {
				i += 1;
				if i == 3 {
					continue;
				}
				if i == 7 {
					break;
				}
				sum += i;
			}

			return sum;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(18, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNamedLabelBreakContinue) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			var hits : i32 = 0;
			outer: while i < 5 {
				i += 1;
				var j : i32 = 0;
				while j < 5 {
					j += 1;
					if i < 5 {
						if j == 2 {
							continue outer;
						}
					}
					if i == 5 {
						if j == 4 {
							break outer;
						}
					}
					hits += 1;
				}
			}
			return i + hits;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeLocalVariable) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 41;
			return x + 1;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	i32 result = ls_to_i32(runtime, -1);
	EXPECT_EQ(42, result);

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeGlobalVariable) {
	const char* source = R"(
		var counter : i32 = 41;

		fn main() : i32 {
			counter += 1;
			return counter;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(43, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeGlobalInitializationOrder) {
	const char* source = R"(
		var a : i32 = 1;
		var b : i32 = a + 2;

		fn main() : i32 {
			return a + b;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(4, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeShortCircuitingWithGlobals) {
	const char* source = R"(
		var hits : i32 = 0;

		fn touch() : bool {
			hits += 1;
			return true;
		}

		fn false_and_touch() : bool {
			return false and touch();
		}

		fn true_or_touch() : bool {
			return true or touch();
		}

		fn get_hits() : i32 {
			return hits;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("false_and_touch"), 0, 1));
	EXPECT_TRUE(!ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("get_hits"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	EXPECT_TRUE(ls_call(runtime, toLs("true_or_touch"), 0, 1));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("get_hits"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNestedShortCircuitingWithGlobals) {
	const char* source = R"(
		var hits : i32 = 0;

		fn touch() : bool {
			hits += 1;
			return true;
		}

		fn false_and_nested() : bool {
			return false and (touch() or touch());
		}

		fn true_or_nested() : bool {
			return true or (touch() and touch());
		}

		fn get_hits() : i32 {
			return hits;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("false_and_nested"), 0, 1));
	EXPECT_TRUE(!ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("get_hits"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	EXPECT_TRUE(ls_call(runtime, toLs("true_or_nested"), 0, 1));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("get_hits"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeAssignLocalVariable) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 41;
			x = x + 1;
			return x;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	i32 result = ls_to_i32(runtime, -1);
	EXPECT_EQ(42, result);

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeCompoundAssignLocalPlusEqual) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 41;
			x += 1;
			return x;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	i32 result = ls_to_i32(runtime, -1);
	EXPECT_EQ(42, result);

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeCompoundAssignLocalMinusEqual) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 41;
			x -= 1;
			return x;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	i32 result = ls_to_i32(runtime, -1);
	EXPECT_EQ(40, result);

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeExtendedIntegerReturnWidths) {
	const char* source = R"(
		fn ret_i8() : i8 {
			return 10;
		}

		fn ret_u8() : u8 {
			return 20;
		}

		fn ret_i16() : i16 {
			return 30;
		}

		fn ret_u16() : u16 {
			return 40;
		}

		fn ret_i64() : i64 {
			return 50;
		}

		fn ret_u64() : u64 {
			return 60;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("ret_i8"), 0, 1));
	EXPECT_EQ(10, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_u8"), 0, 1));
	EXPECT_EQ(20, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_i16"), 0, 1));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_u16"), 0, 1));
	EXPECT_EQ(40, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_i64"), 0, 1));
	EXPECT_EQ(50, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_u64"), 0, 1));
	EXPECT_EQ(60, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeIfElse) {
	const char* source = R"(
		fn choose(flag : bool) : i32 {
			if flag {
				return 11;
			} else {
				return 22;
			}
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	ls_push_bool(runtime, 1);
	EXPECT_TRUE(ls_call(runtime, toLs("choose"), 1, 1));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));

	ls_push_bool(runtime, 0);
	EXPECT_TRUE(ls_call(runtime, toLs("choose"), 1, 1));
	EXPECT_EQ(22, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeIfElseIf) {
	const char* source = R"(
		fn classify(v : i32) : i32 {
			if v > 10 {
				return 2;
			} else if v > 0 {
				return 1;
			} else {
				return 0;
			}
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	ls_push_i32(runtime, -1);
	EXPECT_TRUE(ls_call(runtime, toLs("classify"), 1, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	ls_push_i32(runtime, 4);
	EXPECT_TRUE(ls_call(runtime, toLs("classify"), 1, 1));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));

	ls_push_i32(runtime, 11);
	EXPECT_TRUE(ls_call(runtime, toLs("classify"), 1, 1));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(NestedFunctionsRuntime) {
	const char* source = R"(
		fn apply(f : fn(i32, i32) : i32, a : i32, b : i32) : i32 {
			return f(a, b);
		}

		fn main() : i32 {
			fn add(a : i32, b : i32) : i32 {
				return a + b;
			}

			fn mul(a : i32, b : i32) : i32 {
				return a * b;
			}

			const f = add;
			return apply(f, 20, 2) + mul(5, 4);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(DivisionAndModuloSemanticsRuntime) {
	const char* source = R"(
		fn q_pos() : i32 {
			return 5 / 2;
		}

		fn q_neg() : i32 {
			return -5 / 2;
		}

		fn r_neg_left() : i32 {
			return -5 % 2;
		}

		fn r_neg_right() : i32 {
			return 5 % -2;
		}

		fn float_div() : f32 {
			return 1.0 / 0.0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("q_pos"), 0, 1));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("q_neg"), 0, 1));
	EXPECT_EQ(-2, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("r_neg_left"), 0, 1));
	EXPECT_EQ(-1, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("r_neg_right"), 0, 1));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("float_div"), 0, 1));
	EXPECT_TRUE(isinf((double)ls_to_f32(runtime, -1)));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(RuntimeCasts) {
	const char* source = R"(
		fn to_f32() : f32 {
			const x : i32 = 10;
			return x as f32;
		}

		fn to_i32() : i32 {
			const x : f32 = 12.75;
			return x as i32;
		}

	fn to_bool() : bool {
			const x : i32 = 1;
			return x as bool;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("to_f32"), 0, 1));
	EXPECT_FLOAT_EQ(10, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("to_i32"), 0, 1));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("to_bool"), 0, 1));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(IntegerToEnumCastAllowsAnyIntegerRuntime) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn to_state(v : i32) : State {
			return v as State;
		}

		fn to_i32(v : i32) : i32 {
			const s : State = v as State;
			return s as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_push_i32(runtime, 123);
	EXPECT_TRUE(ls_call(runtime, toLs("to_state"), 1, 1));
	EXPECT_TRUE(ls_call(runtime, toLs("to_i32"), 1, 1));
	EXPECT_EQ(123, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(MatchRuntime) {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		};

		fn enum_match(state : State) : i32 {
			match state {
				case .Idle:
					return 1;
				case .Running, .Paused:
					return 2;
			}
			return 0;
		}

		fn range_match(score : i32) : i32 {
			match score {
				case 0:
					return 0;
				case 1..9, 99:
					return 1;
				case _:
					return 2;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_push_i32(runtime, 0);
	EXPECT_TRUE(ls_call(runtime, toLs("enum_match"), 1, 1));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	ls_push_i32(runtime, 2);
	EXPECT_TRUE(ls_call(runtime, toLs("enum_match"), 1, 1));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	ls_push_i32(runtime, 5);
	EXPECT_TRUE(ls_call(runtime, toLs("range_match"), 1, 1));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	ls_push_i32(runtime, 42);
	EXPECT_TRUE(ls_call(runtime, toLs("range_match"), 1, 1));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(AliasedImportRuntime) {
	const char* main_source = R"(
		import "math" as math
		import "state" as state

		fn main() : i32 {
			const v : math.Vec2 = math.Vec2 { 20, 22 };
			if state.is_running(state.State.Running) {
				return math.sum(v);
			}
			return 0;
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	const char* state_source = R"(
		enum State {
			Idle,
			Running
		};

		fn is_running(state : State) : bool {
			return state == .Running;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) },
		{ toLs("state"), toLs(state_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &resolveLumScriptImportC, &files));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FirstParameterNamespaceResolutionPrecedenceRuntime) {
	const char* main_source = R"(
		import "entity_mod" as entity
		import "helper_mod" as e

		fn destroy(x : entity.Entity) : i32 {
			return 3;
		}

		fn main() : i32 {
			const x : entity.Entity = entity.Entity { 1 };
			return e.destroy() * 100 + x.destroy() * 10 + destroy(x);
		}
	)";

	const char* entity_source = R"(
		struct Entity {
			id : i32;
		};

		fn destroy(x : Entity) : i32 {
			return 1;
		}
	)";

	const char* helper_source = R"(
		fn destroy() : i32 {
			return 2;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("entity_mod"), toLs(entity_source) },
		{ toLs("helper_mod"), toLs(helper_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &resolveLumScriptImportC, &files));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(213, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ShortCircuiting) {
	const char* source = R"(
		fn spin() : bool {
			while true {
			}
			return true;
		}

		fn left_false() : bool {
			return false and spin();
		}

		fn left_true() : bool {
			return true or spin();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("left_false"), 0, 1));
	EXPECT_TRUE(!ls_to_bool(runtime, -1));

	TestContext diagnostics2;
	RuntimeGuard runtime2(module, &diagnostics2.host);
	EXPECT_TRUE(runtime2);
	EXPECT_TRUE(ls_call(runtime2, toLs("left_true"), 0, 1));
	EXPECT_TRUE(ls_to_bool(runtime2, -1));
	CAPI_END(module);
	return true;
}

TEST(IfElse) {
	const char* source = R"(
		fn classify(v : i32) : i32 {
			if v > 10 {
				return 2;
			} else if v > 0 {
				return 1;
			} else {
				return 0;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_push_i32(runtime, 4);
	EXPECT_TRUE(ls_call(runtime, toLs("classify"), 1, 1));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));

	ls_push_i32(runtime, 11);
	EXPECT_TRUE(ls_call(runtime, toLs("classify"), 1, 1));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

	ls_push_i32(runtime, -1);
	EXPECT_TRUE(ls_call(runtime, toLs("classify"), 1, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(GlobalVariablesRuntime) {
	const char* source = R"(
		var counter : i32 = 1;
		const step = 2;

		fn increment() : i32 {
			counter += step;
			return counter;
		}

		fn read_counter() : i32 {
			return counter;
		}

		fn shadow_counter() : i32 {
			var counter : i32 = 100;
			counter += 1;
			return counter;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("read_counter"), 0, 1));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("increment"), 0, 1));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("increment"), 0, 1));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("read_counter"), 0, 1));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("shadow_counter"), 0, 1));
	EXPECT_EQ(101, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("read_counter"), 0, 1));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(RuntimeBlockScope) {
	const char* source = R"(
		fn scoped() : i32 {
			var a = 1;
			{
				var a = 5;
				a = a + 1;
			}
			return a;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("scoped"), 0, 1));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(RefParameterMutatesCaller) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : i32 {
			var x : i32 = 10;
			increment(ref x);
			return x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DeferRunsAtScopeExit) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 1;
			{
				defer x += 3;
				x += 1;
			}
			return x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DeferRunsInLifoOrder) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 1;
			{
				defer x += 1;
				defer x *= 2;
			}
			return x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DeferRunsOnEarlyReturn) {
	const char* source = R"(
		fn apply(v : ref i32) : void {
			defer v += 1;
			return;
		}

		fn main() : i32 {
			var x : i32 = 10;
			apply(ref x);
			return x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(NullableNullBranchRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var x : ?i32 = null;
			if x != null {
				return x;
			}
			return 42;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(NullableNonNullBranchRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var x : ?i32 = 7;
			if x != null {
				return x;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ExtendedScalarTypesRuntime) {
	const char* source = R"(
		fn main() : i32 {
			const a : i8 = 10 as i8;
			const b : u8 = 20 as u8;
			const c : i16 = 30 as i16;
			const d : u16 = 40 as u16;
			const e : i64 = 50 as i64;
			const f : u64 = 60 as u64;
			const g : f64 = 1 as f64;
			return a as i32 + b as i32 + c as i32 + d as i32 + e as i32 + f as i32 + g as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(211, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(IntegerOverflowWraparoundRuntime) {
	const char* source = R"(
		fn u8_add_wrap() : i32 {
			const a : u8 = 255 as u8;
			const b : u8 = (a + 1 as u8) as u8;
			return b as i32;
		}

		fn i8_add_wrap() : i32 {
			const a : i8 = 127 as i8;
			const b : i8 = (a + 1 as i8) as i8;
			return b as i32;
		}

		fn u8_add_assign_wrap() : i32 {
			var x : u8 = 255 as u8;
			x += 1 as u8;
			return x as i32;
		}

		fn cast_i8_wrap() : i32 {
			const x : i32 = 255;
			return (x as i8) as i32;
		}

		fn cast_u8_wrap() : i32 {
			const x : i32 = 256;
			return (x as u8) as i32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("u8_add_wrap"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("i8_add_wrap"), 0, 1));
	EXPECT_EQ(-128, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("u8_add_assign_wrap"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("cast_i8_wrap"), 0, 1));
	EXPECT_EQ(-1, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("cast_u8_wrap"), 0, 1));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DivisionByZeroRuntimeError) {
	const char* source = R"(
		fn divide(v : i32, d : i32) : i32 {
			return v / d;
		}

		fn modulo(v : i32, d : i32) : i32 {
			return v % d;
		}

		fn divide_assign(d : i32) : i32 {
			var x : i32 = 8;
			x /= d;
			return x;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;
	ls_push_i32(runtime, 10);
	ls_push_i32(runtime, 0);
	EXPECT_TRUE(!ls_call(runtime, toLs("divide"), 2, 1));

	TestContext diagnostics2;
	RuntimeGuard runtime2(module, &diagnostics2.host);
	EXPECT_TRUE(runtime2);
	diagnostics2.diagnostics.output_enabled = false;
	ls_push_i32(runtime2, 10);
	ls_push_i32(runtime2, 0);
	EXPECT_TRUE(!ls_call(runtime2, toLs("modulo"), 2, 1));

	TestContext diagnostics3;
	RuntimeGuard runtime3(module, &diagnostics3.host);
	EXPECT_TRUE(runtime3);
	diagnostics3.diagnostics.output_enabled = false;
	ls_push_i32(runtime3, 0);
	EXPECT_TRUE(!ls_call(runtime3, toLs("divide_assign"), 1, 1));
	CAPI_END(module);
	return true;
}

TEST(UntypedLiteralsRuntime) {
	const char* source = R"(
		struct Vec3 {
            x : f32; y : f32; z : f32;
        }

		struct Pair {
			x : u8;
			y : f64;
		}

		fn vec3_sum() : f32 {
			const v : Vec3 = { 1, 2, 3 };
			return v.x + v.y + v.z;
		}

		fn integer_widths() : i32 {
			const a : i8 = 10;
			const b : u8 = 20;
			const c : i16 = 30;
			const d : u16 = 40;
			const e : i64 = 50;
			const f : u64 = 60;
			const pair = Pair { 255, 2.5 };
			return a as i32 + b as i32 + c as i32 + d as i32 + e as i32 + f as i32 + pair.x as i32 + pair.y as i32;
		}

		fn return_f64() : f64 {
			return 1.5;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	bool compiled = ls_module_compile(module, toLs(source), {}, nullptr, nullptr);
	EXPECT_TRUE(compiled);

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("vec3_sum"), 0, 1));
	EXPECT_FLOAT_EQ(6, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("integer_widths"), 0, 1));
	EXPECT_EQ(467, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("return_f64"), 0, 1));
	EXPECT_FLOAT_EQ(1.5f, (float)ls_to_f64(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FirstClassFunctionsRuntime) {
	const char* source = R"(
		fn add(a : i32, b : i32) : i32 {
			return a + b;
		}

		fn mul(a : i32, b : i32) : i32 {
			return a * b;
		}

		fn apply(f : fn(i32, i32) : i32, a : i32, b : i32) : i32 {
			return f(a, b);
		}

		fn choose(use_mul : bool) : fn(i32, i32) : i32 {
			if use_mul {
				return mul;
			}
			return add;
		}

		fn main() : i32 {
			const add_fn = choose(false);
			const mul_fn = choose(true);
			return apply(add_fn, 20, 2) + mul_fn(6, 7);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	const bool ok = ls_module_compile(module, toLs(source), {}, nullptr, nullptr);
	EXPECT_TRUE(ok);

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(64, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(StaticArrayRuntimeIndexing) {
	const char* source = R"(
		fn main() : i32 {
			var d : i32[4] = undefined;
			var i : i32 = 2;
			d[i] = 42;
			return d[2];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(StaticArrayRuntimeOutOfBoundsFails) {
	const char* source = R"(
		fn main(i : i32) : i32 {
			var d : i32[2] = undefined;
			d[0] = 7;
			return d[i];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;
	ls_push_i32(runtime, 5);
	EXPECT_TRUE(!ls_call(runtime, toLs("main"), 1, 1));
	return true;
}

TEST(BreakContinueRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			var sum : i32 = 0;
			while i < 10 {
				i += 1;
				if i == 3 {
					continue;
				}
				if i == 8 {
					break;
				}
				sum += i;
			}
			return sum;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(25, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ForLoopRuntime) {
	const char* source = R"(
		var bound_hits : i32 = 0;

		fn lower() : i32 {
			bound_hits += 1;
			return 0;
		}

		fn upper() : i32 {
			bound_hits += 1;
			return 3;
		}

		fn main() : i32 {
			var sum : i32 = 0;
			for i = lower()..upper() {
				sum += i;
			}
			return bound_hits * 100 + sum;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(206, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ForLoopRangeEvaluatedOnce) {
	const char* source = R"(
		var range_hits : i32 = 0;

		fn lower() : i32 {
			range_hits += 1;
			return 0;
		}

		fn upper() : i32 {
			range_hits += 1;
			return 2;
		}

		fn main() : i32 {
			var sum : i32 = 0;
			for i = lower()..upper() {
				sum += i;
			}
			return range_hits * 100 + sum;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(203, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(NamedLabelBreakContinueRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			var hits : i32 = 0;
			outer: while i < 5 {
				i += 1;
				var j : i32 = 0;
				while j < 5 {
					j += 1;
					if i < 5 and j == 2 {
						continue outer;
					}
					if i == 5 and j == 4 {
						break outer;
					}
					hits += 1;
				}
			}
			return i * 10 + hits;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 0, 1));
	EXPECT_EQ(57, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(MatchArmMultipleStatementsRuntime) {
	const char* source = R"(
		fn main(v : i32) : i32 {
			var result : i32 = 0;
			match v {
				case 0:
					result = 1;
					result += 2;
				case _:
					result = 10;
					result += 20;
			}
			return result;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	ls_push_i32(runtime, 0);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 1, 1));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	ls_push_i32(runtime, 7);
	EXPECT_TRUE(ls_call(runtime, toLs("main"), 1, 1));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
