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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}



TEST(StructExtern) {
	const char* source = R"(
		struct Entity {
			index : i32;
			world : cptr;
		}
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	
	return true;
}

TEST(BytecodeStringLiteralArgumentShouldCompile) {
	const char* source = R"(
		extern fn findByName(name : string) : void;

		fn main() : void {
			findByName("testor");
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(3.75f, (float)ls_to_f64(runtime, -1));

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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(21, ls_to_i32(runtime, -1));

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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	EXPECT_EQ(1, ls_to_i32(runtime, 0));
	EXPECT_EQ(0, ls_to_i32(runtime, 1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeDeferRunsOnBreak) {
	const char* source = R"(
		var g : i32 = 0;

		fn main() : i32 {
			while true {
				defer g = g + 1;
				break;
			}
			return g;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(BytecodeDeferRunsOnContinue) {
	const char* source = R"(
		var g : i32 = 0;

		fn main() : i32 {
			var i : i32 = 0;
			while i < 2 {
				i += 1;
				defer g = g + 1;
				continue;
			}
			return g;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(BytecodeFunctionTypedLocalCanBeCalled) {
	const char* source = R"(
		fn foo(v : bool) : i32 {
			return 7;
		}

		fn bar(v : i32) : i32 {
			return v + 1;
		}

		fn main() : i32 {
			const foo : fn(i32) : i32 = bar;
			return foo(41);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("is_gt")));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("is_lt")));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("is_eq")));
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
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
		}
		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	const char* geom_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}
		fn sum(v : Vec2) : i32 {
			return v.x + v.y + 1;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ toLs("math"), toLs(math_source) },
		{ toLs("geom"), toLs(geom_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime, {
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(85, ls_to_i32(runtime, -1));
	});
	return true;
}

TEST(BytecodeStructsBasic) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
		}
		struct Outer {
			pos : Vec2;
			z : i32;
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeChainedFieldAccess) {
	const char* source = R"(
		struct C {
			d : i32;
		}
		struct B {
			c : C;
		}
		struct A {
			b : B;
		}
		fn main() : i32 {
			const a : A = A { B { C { 42 } } };
			return a.b.c.d;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeChainedFieldAssignment) {
	const char* source = R"(
		struct C {
			d : i32;
		}
		struct B {
			c : C;
		}
		struct A {
			b : B;
		}
		fn main() : i32 {
			var a : A = A { B { C { 1 } } };
			a.b.c.d = 42;
			return a.b.c.d;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

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

	EXPECT_TRUE(ls_call(runtime, toLs("add_i8_wrap")));
	EXPECT_EQ(-128, ls_to_i8(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("add_u8_wrap")));
	EXPECT_EQ(0, ls_to_u8(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("sub_i8_wrap")));
	EXPECT_EQ(-1, ls_to_i8(runtime, -1));

	ls_runtime_destroy(runtime);
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));

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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("false_and_touch")));
	EXPECT_TRUE(!ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("get_hits")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	EXPECT_TRUE(ls_call(runtime, toLs("true_or_touch")));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("get_hits")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("false_and_nested")));
	EXPECT_TRUE(!ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("get_hits")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	EXPECT_TRUE(ls_call(runtime, toLs("true_or_nested")));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("get_hits")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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

	EXPECT_TRUE(ls_call(runtime, toLs("ret_i8")));
	EXPECT_EQ(10, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_u8")));
	EXPECT_EQ(20, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_i16")));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_u16")));
	EXPECT_EQ(40, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_i64")));
	EXPECT_EQ(50, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("ret_u64")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("choose")));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));

	ls_push_bool(runtime, 0);
	EXPECT_TRUE(ls_call(runtime, toLs("choose")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("classify")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	ls_push_i32(runtime, 4);
	EXPECT_TRUE(ls_call(runtime, toLs("classify")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));

	ls_push_i32(runtime, 11);
	EXPECT_TRUE(ls_call(runtime, toLs("classify")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

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
			return 6.0 / 2.0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("q_pos")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("q_neg")));
	EXPECT_EQ(-2, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("r_neg_left")));
	EXPECT_EQ(-1, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("r_neg_right")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("float_div")));
	EXPECT_FLOAT_EQ(3, ls_to_f32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
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
	EXPECT_TRUE(ls_call(runtime, toLs("left_false")));
	EXPECT_TRUE(!ls_to_bool(runtime, -1));

	TestContext diagnostics2;
	RuntimeGuard runtime2(module, &diagnostics2.host);
	EXPECT_TRUE(runtime2);
	EXPECT_TRUE(ls_call(runtime2, toLs("left_true")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("classify")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));

	ls_push_i32(runtime, 11);
	EXPECT_TRUE(ls_call(runtime, toLs("classify")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

	ls_push_i32(runtime, -1);
	EXPECT_TRUE(ls_call(runtime, toLs("classify")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("read_counter")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("increment")));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("increment")));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("read_counter")));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("shadow_counter")));
	EXPECT_EQ(101, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("read_counter")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("scoped")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ExtendedScalarTypesRuntime) {
	const char* source = R"(
		fn main() : i32 {
			const a : i8 = 10;
			const b : u8 = 20;
			const c : i16 = 30;
			const d : u16 = 40;
			const e : i64 = 50;
			const f : u64 = 60;
			const g : f64 = 1;
			return a as i32 + b as i32 + c as i32 + d as i32 + e as i32 + f as i32 + g as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("u8_add_wrap")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("i8_add_wrap")));
	EXPECT_EQ(-128, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("u8_add_assign_wrap")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("cast_i8_wrap")));
	EXPECT_EQ(-1, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("cast_u8_wrap")));
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

		fn divide_constant_zero(v : i32) : i32 {
			return v / 0;
		}

		fn modulo_constant_zero(v : i32) : i32 {
			return v % 0;
		}

		fn divide_assign_constant_zero() : i32 {
			var x : i32 = 8;
			x /= 0;
			return x;
		}

		fn divide_float_constant_zero() : f32 {
			return 1.0 / 0.0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;
	ls_push_i32(runtime, 10);
	ls_push_i32(runtime, 0);
	EXPECT_TRUE(!ls_call(runtime, toLs("divide")));

	TestContext diagnostics2;
	RuntimeGuard runtime2(module, &diagnostics2.host);
	EXPECT_TRUE(runtime2);
	diagnostics2.diagnostics.output_enabled = false;
	ls_push_i32(runtime2, 10);
	ls_push_i32(runtime2, 0);
	EXPECT_TRUE(!ls_call(runtime2, toLs("modulo")));

	TestContext diagnostics3;
	RuntimeGuard runtime3(module, &diagnostics3.host);
	EXPECT_TRUE(runtime3);
	diagnostics3.diagnostics.output_enabled = false;
	ls_push_i32(runtime3, 0);
	EXPECT_TRUE(!ls_call(runtime3, toLs("divide_assign")));

	TestContext diagnostics4;
	RuntimeGuard runtime4(module, &diagnostics4.host);
	EXPECT_TRUE(runtime4);
	diagnostics4.diagnostics.output_enabled = false;
	ls_push_i32(runtime4, 10);
	EXPECT_TRUE(!ls_call(runtime4, toLs("divide_constant_zero")));

	TestContext diagnostics5;
	RuntimeGuard runtime5(module, &diagnostics5.host);
	EXPECT_TRUE(runtime5);
	diagnostics5.diagnostics.output_enabled = false;
	ls_push_i32(runtime5, 10);
	EXPECT_TRUE(!ls_call(runtime5, toLs("modulo_constant_zero")));

	TestContext diagnostics6;
	RuntimeGuard runtime6(module, &diagnostics6.host);
	EXPECT_TRUE(runtime6);
	diagnostics6.diagnostics.output_enabled = false;
	EXPECT_TRUE(!ls_call(runtime6, toLs("divide_assign_constant_zero")));

	TestContext diagnostics7;
	RuntimeGuard runtime7(module, &diagnostics7.host);
	EXPECT_TRUE(runtime7);
	diagnostics7.diagnostics.output_enabled = false;
	EXPECT_TRUE(!ls_call(runtime7, toLs("divide_float_constant_zero")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("vec3_sum")));
	EXPECT_FLOAT_EQ(6, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("integer_widths")));
	EXPECT_EQ(467, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("return_f64")));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(64, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FirstClassFunctionsNoArgsRuntime) {
	const char* source = R"(
		fn make_one() : i32 {
			return 1;
		}

		fn make_two() : i32 {
			return 2;
		}

		fn choose(use_two : bool) : fn() : i32 {
			if use_two {
				return make_two;
			}
			return make_one;
		}

		fn call(f : fn() : i32) : i32 {
			return f();
		}

		fn main() : i32 {
			const one_fn = choose(false);
			const two_fn = choose(true);
			return call(one_fn) * 10 + two_fn();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FirstClassFunctionsImmediateCallRuntime) {
	const char* source = R"(
		fn inner() : i32 {
			return 42;
		}

		fn make_fn() : fn() : i32 {
			return inner;
		}

		fn main() : i32 {
			return make_fn()();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FirstClassFunctionLiteralImmediateCallRuntime) {
	const char* source = R"(
		fn inner() : i32 {
			return 42;
		}

		const make_fn = fn() : fn() : i32 {
			return inner;
		};

		fn main() : i32 {
			return make_fn()();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FirstClassFunctionsStructFieldImmediateCallRuntime) {
	const char* source = R"(
		struct Holder {
			bar : fn() : i32;
		}

		fn inner() : i32 {
			return 42;
		}

		fn make_holder() : Holder {
			return Holder { inner };
		}

		fn main() : i32 {
			return make_holder().bar();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(!ls_call(runtime, toLs("main")));
	return true;
}





