TEST(ComptimeBasic) {
	const char* source = R"(
		comptime N = 32;
		comptime enabled = true;
		comptime scale = 1.5;
		comptime Name = "Lumix";
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeFunction) {
	const char* source = R"(
		comptime foo = fn() : void {};
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeBinaryExpression) {
	const char* source = R"(
		comptime N = (32 - 2) * 3 / 5 + 1;
		comptime Remainder = 32 % 7;
		comptime enabled = 2 > 1 and 4 <= 4;
		comptime scale = 1.5 * 2.0 + 0.5;

		fn main() : i32 {
			if (enabled == false or N != 19 or Remainder != 4 or scale != 3.5) {
				return 1;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeBinaryExpressionEdgeCases) {
	const char* source = R"(
		comptime integer_division = 5 / 2;
		comptime remainder = 5 % 2;
		comptime unsigned_wrap = (255 as u8) + (1 as u8);
		comptime signed_wrap = (127 as i8) + (1 as i8);
		comptime f32_value = (1.25 as f32) * (2.0 as f32) / (0.5 as f32);
		comptime comparisons = 2 < 3 and 7 >= 7 and 3.5 > 3.0;

		fn main() : i32 {
			if (integer_division != 2
				or remainder != 1
				or unsigned_wrap as i32 != 0
				or signed_wrap as i32 != -128
				or f32_value != 5.0
				or comparisons == false)
			{
				return 1;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeBinaryExpressionU64EdgeCases) {
	const char* source = R"(
		comptime max = 18446744073709551615 as u64;
		comptime wrapped = max + (1 as u64);
		comptime half = max / (2 as u64);

		fn wrap() : u64 { return wrapped; }
		fn divide() : u64 { return half; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("wrap")));
	EXPECT_TRUE(ls_to_u64(runtime, -1) == 0);
	EXPECT_TRUE(ls_call(runtime, toLs("divide")));
	EXPECT_TRUE(ls_to_u64(runtime, -1) == 9223372036854775807ull);
	CAPI_END(module);
	return true;
}

TEST(ComptimeBinaryExpressionDivisionByZeroFails) {
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1 / 0;
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1 % 0;
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1.0 / 0.0;
	)");
	return true;
}



TEST(ComptimeTernary) {
	const char* source = R"(
		comptime X = true ? 1 : 4;

		fn main() : i32 {
			return X;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimePrimitiveValueTypechecks) {
	const char* source = R"(
		comptime N = 32;
		comptime enabled = true;
		comptime scale = 1.5;

		fn main() : i32 {
			return N;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(32, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeStringValueTypechecks) {
	const char* source = R"(
		comptime Name = "Lumix";

		fn main() : string {
			return Name;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(equalStrings(ls_to_string(runtime, -1), toLs("Lumix")));
	CAPI_END(module);
	return true;
}

TEST(ComptimeBoolValueTypechecks) {
	const char* source = R"(
		comptime Enabled = true;

		fn main() : bool {
			return Enabled;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeFloatValueTypechecks) {
	const char* source = R"(
		comptime Scale = 1.5;

		fn main() : f64 {
			return Scale;
		}
	)";
	{
		CAPI_BEGIN(module, diagnostics);
		EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
		CAPI_RUNTIME(module, runtime);
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_FLOAT_EQ(1.5, ls_to_f64(runtime, -1));
		CAPI_END(module);
	}

	const char* does_not_implicitly_convert = R"(
		comptime Scale = 1.5;

		fn main() : f32 {
			return Scale;
		}
	)";
	EXPECT_COMPILE_FAIL(does_not_implicitly_convert);
	return true;
}

TEST(ComptimeAnnotatedPrimitiveValueTypechecks) {
	const char* source = R"(
		comptime N : i32 = 32;

		fn main() : i32 {
			return N;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(32, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeAnnotatedFloatValueTypechecks) {
	const char* source = R"(
		comptime N : f32 = 1.5;

		fn main() : f32 {
			return N;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(1.5, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeAnnotatedPrimitiveTypeMismatchFails) {
	const char* source = R"(
		comptime N : i32 = 1.5;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimePrimitiveValueCanBeStaticArraySize) {
	const char* source = R"(
		comptime N = 4;

		fn main() : void {
			var values : [N]i32 = undefined;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}

TEST(ComptimeTypeBindingTypechecks) {
	const char* source = R"(
		comptime Int = i32;

		fn main() : Int {
			return 42;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeCompositeTypeBindingsTypecheck) {
	const char* source = R"(
		comptime Slice = []i32;
		comptime Array = [4]i32;
		comptime Optional = ?i32;
		comptime Callback = fn() : void;

		fn callback() : void {}

		fn main(slice : Slice, array : Array, optional : Optional, callback_value : Callback) : void {
			var slice_value : []i32 = slice;
			var array_value : [4]i32 = array;
			var optional_value : ?i32 = optional;
			callback_value();
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeVariableInitielizedWithFunctionReturningNumericValue) {
	const char* source = R"(
		fn foo() : i32 { return 2; }
		comptime N = foo();
		fn main() : [N]i32 { return [1, 2]; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeArray) {
	const char* source = R"(
		comptime A = [i32, f32];
		fn foo() : A[1] { return 3.14; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("foo")));
	EXPECT_FLOAT_EQ(3.14f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeArray2) {
	const char* source = R"(
		fn foo() : [2]i32 { 
			var a : [2]i32 = [40, 2];
			return a;
		}
		comptime A = foo();
		fn main() : i32 {
			var a : [2]i32 = A;
			return a[0] + a[1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeArray3) {
	const char* source = R"(
		fn bar() : [2]i32 {
			var a : [2]i32 = [15, 25];
			return a;
		}
		comptime B = bar();
		fn foo() : [2]i32 {
			var a : [2]i32 = [B[0] + B[1], 2];
			return a;
		}
		comptime A = foo();
		fn main() : i32 {
			var a : [2]i32 = A;
			return a[0] + a[1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}


TEST(ComptimeArrayUnrollFor) {
	const char* source = R"(
		comptime A = [1, 2, 3]
		
		fn foo() : i32 {
			var sum : i32 = 0;
			unroll for i in A {
				sum += i;
			}
			return sum;
		}

		comptime B = foo();

		fn main() : i32 {
			return B;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(6, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// KNOWN BUG: evalComptimeStmt has no Statement::FOR case, so folding foo()
// fails and B falls back to calling foo() at each use instead of being a
// folded constant. A breakpoint in foo() should never fire; it fires twice.
TEST(ComptimeArrayUnrollForFoldsWithoutRunningFooAtRuntime) {
	const char* source = R"(
		comptime A = [1, 2, 3]

		fn foo() : i32 {
			var sum : i32 = 0;
			unroll for i in A {
				sum += i;
			}
			return sum;
		}

		comptime B = foo();

		fn main() : i32 {
			var first : i32 = B;
			var second : i32 = B;
			return first + second;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));
	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_call(runtime, toLs("main")));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// "Comptime evaluation cannot depend on runtime storage" (reference.md) applies
// to writes too, not just reads - mutating a global should be rejected here.
TEST(ComptimeCallMutatingGlobalFails) {
	const char* source = R"(
		var call_count : i32 = 0;

		fn foo() : i32 {
			call_count += 1;
			return call_count;
		}

		comptime B = foo();

		fn main() : i32 {
			return B;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeCallReadingGlobalFails) {
	const char* source = R"(
		var runtime_value : i32 = 16;

		fn foo() : i32 {
			return runtime_value;
		}

		comptime N = foo();

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeCallToExternTransitivelyFails) {
	const char* source = R"(
		extern fn native_value() : i32;

		fn foo() : i32 {
			return native_value();
		}

		comptime N = foo();

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(ComptimeStruct) {
	const char* source = R"(
		struct S {
			a : [2]i32;
			b : i32;
		}
		comptime S_value = S { [19, 21], 2 };

		fn main() : i32 {
			var s : S = S_value;
			return s.a[0] + s.a[1] + s.b;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeFunctionReturningTypeBindingTypechecks) {
	const char* source = R"(
		fn foo() : type { return i32; }
		comptime T = foo();
		fn main() : T { return 42; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeCompositeTypeExpressions) {
	const char* source = R"(
		comptime Slice = []i32;
		comptime Array = [4]i32;
		comptime Optional = ?i32;
		comptime Callback = fn() : void;

		fn main(slice : Slice, array : Array, optional : Optional, callback : Callback) : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeEnumBindingTypechecks) {
	const char* source = R"(
		comptime State = enum {
			Idle,
			Running
		}

		fn main() : State {
			return State.Idle;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeStructBindingTypechecks) {
	const char* source = R"(
		comptime Vec2 = struct {
			x : f32;
			y : f32;
		}

		fn main() : Vec2 {
			return Vec2 { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeFunctionReturningTypeFails) {
	const char* source = R"(
		comptime make_vec2 = fn(T : type) : type {
			return struct {
				x : T;
				y : T;
			};
		}

		comptime Vec2 = make_vec2(f32);

		fn main() : Vec2 {
			return Vec2 { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeFunctionBindingTypechecks) {
	const char* source = R"(
		comptime add = fn(a : i32, b : i32) : i32 {
			return a + b;
		}

		fn main() : i32 {
			return add(20, 22);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeNestedExpressionTypechecks) {
	const char* source = R"(
		comptime A = 20;
		comptime B = A + 22;

		fn main() : i32 {
			return B;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeInitializerCanCallTopLevelFunction) {
	const char* source = R"(
		fn double(v : i32) : i32 {
			return v * 2;
		}

		comptime N = double(16);

		fn main() : i32 {
			return N;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(32, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeInitializerCanCallComptimeFunctionBinding) {
	const char* source = R"(
		comptime double = fn(v : i32) : i32 {
			return v * 2;
		}

		comptime N = double(16);

		fn main() : i32 {
			return N;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(32, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeFunctionCanCallOtherComptimeFunction) {
	const char* source = R"(
		fn double(v : i32) : i32 {
			return v * 2;
		}

		fn quadruple(v : i32) : i32 {
			return double(double(v));
		}

		comptime N = quadruple(8);

		fn main() : i32 {
			return N;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(32, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeInitializerCanNotCallTypeProducingFunction) {
	const char* source = R"(
		fn make_vec2(T : type) : type {
			return struct {
				x : T;
				y : T;
			};
		}

		comptime Vec2 = make_vec2(f32);

		fn main() : Vec2 {
			return Vec2 { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeCallWithRuntimeArgumentFails) {
	const char* source = R"(
		fn double(v : i32) : i32 {
			return v * 2;
		}

		var runtime_value : i32 = 16;
		comptime N = double(runtime_value);

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeCallCanNotCallExternFunctionFails) {
	const char* source = R"(
		extern fn native_value() : i32;

		comptime N = native_value();

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeDuplicateNameFails) {
	const char* source = R"(
		comptime N = 1;
		comptime N = 2;

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeCanNotBeReassignedFails) {
	const char* source = R"(
		comptime N = 1;

		fn main() : void {
			N = 2;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeInitializerMustBeComptimeFails) {
	const char* source = R"(
		var runtime_value : i32 = 1;
		comptime N = runtime_value;

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeLocalDeclaration) {
	const char* source = R"(
		fn main() : void {
			comptime N = 32;
		}

		fn foo() : void {
			comptime N = 32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}

TEST(ComptimeLocalValueMaterializesAtRuntime) {
	const char* source = R"(
		fn main() : i32 {
			comptime value = 42;
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}


TEST(ComptimeTypeLiteralInRuntimeContext) {
	const char* source = R"(
		fn main() : type {
			return i32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeRuntimeValueAsTypeFails) {
	const char* source = R"(
		fn main() : void {
			var T = i32;
			var value : T = 42;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeUnresolvedNameFails) {
	const char* source = R"(
		comptime N = Missing;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeImportedValueVisibleWithAlias) {
	const char* main_source = R"(
		import "constants" as constants;

		fn main() : i32 {
			return constants.N;
		}
	)";

	const LumScriptImportFile files[] = {
		{ toLs("constants"), toLs("comptime N = 32;") }
	};
	LumScriptImportFiles imports = { files, lengthOf(files) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, imports, runtime, {
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(32, ls_to_i32(runtime, -1));
	});
	return true;
}

TEST(ComptimeImportedTypeVisibleWithAlias) {
	const char* main_source = R"(
		import "types" as types

		fn main() : types.Int {
			return 42;
		}
	)";

	const LumScriptImportFile files[] = {
		{ toLs("types"), toLs("comptime Int = i32;") }
	};
	LumScriptImportFiles imports = { files, lengthOf(files) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, imports, runtime, {
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	});
	return true;
}

TEST(ComptimeImportedValueNotVisibleWithoutImportFails) {
	const char* source = R"(
		fn main() : i32 {
			return constants.N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeIfPrunesUnselectedArm) {
	const char* source = R"(
		fn main() : void {
			if false {
				var impossible : MissingType = undefined;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}

TEST(ComptimeIfTemplateSpecializationPrunesUnselectedArm) {
	const char* source = R"(
		fn write(value : $T) : void {
			if T == i32 {
				var number : i32 = value;
			} else {
				var text : string = value;
			}
		}

		fn main() : void {
			write(42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}

TEST(ComptimeIfElseIfAndElsePruneUnselectedArms) {
	const char* source = R"(
		comptime enabled = false;
		fn main() : i32 {
			if enabled {
				var bad : MissingType = undefined;
			} else if true {
				return 7;
			} else {
				var also_bad : MissingType = undefined;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeMatchPrunesUnselectedCase) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime state = State.Idle;

		fn main() : i32 {
			match state {
				case .Idle:
					return 1;
				case .Running:
					var bad : MissingType = undefined;
				case:
					var also_bad : MissingType = undefined;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(RuntimeIfChecksBothArms) {
	const char* source = R"(
		fn main(flag : bool) : void {
			if flag {
				var ok : i32 = 1;
			} else {
				var bad : MissingType = undefined;
			}
		}
	)";
	// A runtime condition must check both arms even when a call site passes a constant.
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeIfSelectedArmExecutes) {
	const char* source = R"(
		comptime enabled = true;
		fn main() : i32 {
			var result : i32 = 0;
			if enabled {
				result = 7;
			} else {
				var bad : MissingType = undefined;
			}
			return result;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateRuntimeIfDoesNotBecomeComptime) {
	const char* source = R"(
		fn choose(T : comptime type, flag : bool) : void {
			if flag {
				var ok : T = undefined;
			} else {
				var bad : MissingType = undefined;
			}
		}

		fn main() : void {
			choose(i32, true);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeIfFunctionCallConditionPrunesArm) {
	const char* source = R"(
		fn enabled() : bool { return true; }
		comptime flag = enabled();

		fn main() : i32 {
			if flag {
				return 3;
			} else {
				var bad : MissingType = undefined;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeValueParameterIfPrunesArm) {
	const char* source = R"(
		fn choose(flag : comptime bool) : i32 {
			if flag {
				return 5;
			} else {
				var bad : MissingType = undefined;
			}
		}

		fn main() : i32 {
			return choose(true);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeIfRejectsSyntacticallyInvalidUnselectedArm) {
	const char* source = R"(
		fn main() : void {
			if false {
				var broken = ;
			}
		}
	)";
	// Unselected arms are not type-checked, but they must still be syntactically valid.
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeIfPrunesReturnInUnselectedArm) {
	const char* source = R"(
		fn main() : i32 {
			if false {
				return "wrong type";
			}
			return 9;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(9, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeIfNestedPruning) {
	const char* source = R"(
		comptime outer = true;
		comptime inner = false;
		fn main() : i32 {
			if outer {
				if inner {
					var bad : MissingType = undefined;
				} else {
					return 11;
				}
			} else {
				var also_bad : MissingType = undefined;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeMatchCommaAlternativesAndFallback) {
	const char* source = R"(
		enum State { Idle, Running, Paused }
		comptime state = State.Running;

		fn main() : i32 {
			match state {
				case .Idle, .Running:
					return 1;
				case:
					var bad : MissingType = undefined;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeMatchRequiresExhaustiveCases) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime state = State.Idle;
		fn main() : void {
			match state {
				case .Idle:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeMatchRejectsDuplicateCases) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime state = State.Idle;
		fn main() : void {
			match state {
				case .Idle:
					return;
				case .Idle:
					return;
				case .Running:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeMatchSelectedArmExecutes) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime state = State.Running;
		fn main() : i32 {
			match state {
				case .Idle:
					var bad : MissingType = undefined;
				case .Running:
					return 8;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(8, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeMatchFunctionCallScrutinee) {
	const char* source = R"(
		enum State { Idle, Running }
		fn current() : State { return State.Running; }
		comptime state = current();
		fn main() : i32 {
			match state {
				case .Idle:
					var bad : MissingType = undefined;
				case .Running:
					return 9;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(9, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeMatchSelectedFallbackExecutes) {
	const char* source = R"(
		enum State { Idle, Running, Paused }
		comptime state = State.Paused;
		fn main() : i32 {
			match state {
				case .Idle:
					return 1;
				case .Running:
					return 2;
				case:
					return 3;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeMatchPrunesInvalidAlternativeArm) {
	const char* source = R"(
		enum State { Idle, Running, Paused }
		comptime state = State.Idle;
		fn main() : i32 {
			match state {
				case .Idle, .Running:
					return 4;
				case .Paused:
					var bad : MissingType = undefined;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(4, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnrollForRange) {
	const char* source = R"(
		fn main() : i32 {
			var total : i32 = 0;
			unroll for i in 0..3 { total += i; }
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnrollForComptimeSequenceWithIndex) {
	const char* source = R"(
		comptime values : []i32 = [1, 2, 3];
		fn main() : i32 {
			var total : i32 = 0;
			unroll for i, value in values { total += i as i32 + value; }
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(9, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnrollForBreakContinue) {
	const char* source = R"(
		fn main() : i32 {
			var total : i32 = 0;
			unroll for i in 0..4 {
				if i == 1 { continue; }
				if i == 3 { break; }
				total += i;
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnrollForArrayBreakRuntime) {
	const char* source = R"(
		comptime values : []i32 = [10, 20, 30, 40];
		fn main() : i32 {
			var total : i32 = 0;
			unroll for value in values {
				if value == 30 { break; }
				total += value;
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnrollForLabeledBreakContinue) {
	const char* source = R"(
		fn main() : i32 {
			var total : i32 = 0;
			outer:
			unroll for i in 0..3 {
				unroll for j in 0..3 {
					if j == 1 { continue outer; }
					if i == 2 { break outer; }
					total += 1;
				}
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnrollForPerCopyCompileTimeBranch) {
	const char* source = R"(
		fn main() : i32 {
			var total : i32 = 0;
			unroll for i in 0..3 { if i > 0 { total += i; } }
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeCallInitAggregateFolds) {
	// A comptime global initialized by a function returning an aggregate must be
	// folded to a constant and materialized inline (no runtime global slot / call).
	const char* source = R"(
		fn foo() : [2]i32 {
			var a : [2]i32 = [40, 2];
			return a;
		}
		comptime A = foo();
		fn main() : i32 {
			var a : [2]i32 = A;
			return a[0] + a[1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeCallInitStructFolds) {
	const char* source = R"(
		struct Vec { x : i32; y : i32; }
		fn make() : Vec {
			var v : Vec = Vec { 30, 12 };
			return v;
		}
		comptime V = make();
		fn main() : i32 {
			var v : Vec = V;
			return v.x + v.y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeCallInitLocalReassignFolds) {
	// Body uses a local variable, an assignment, then returns an aggregate built
	// from the local: exercises the byte-buffer interpreter's frame and ASSIGN.
	const char* source = R"(
		fn build() : [3]i32 {
			var n : i32 = 10;
			n = 20;
			var a : [3]i32 = [n, 21, 1];
			return a;
		}
		comptime A = build();
		fn main() : i32 {
			var a : [3]i32 = A;
			return a[0] + a[1] + a[2];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeNestedAggregateFolds) {
	const char* source = R"(
		struct Inner { a : [2]i32; }
		struct Outer { inner : Inner; b : i32; }
		comptime O = Outer { Inner { [19, 21] }, 2 };
		fn main() : i32 {
			var o : Outer = O;
			return o.inner.a[0] + o.inner.a[1] + o.b;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeCallInitLoopFolds) {
	// A comptime initializer whose body has a while loop and compound assignment
	// must be interpreted at compile time and folded to a constant.
	const char* source = R"(
		fn sum_to(n : i32) : i32 {
			var total : i32 = 0;
			var i : i32 = 0;
			while i < n {
				total += i;
				i = i + 1;
			}
			return total;
		}
		fn pick() : i32 { return sum_to(5); }
		comptime C = pick();
		fn main() : i32 {
			return C + 32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1)); // 0+1+2+3+4 = 10, +32 = 42
	CAPI_END(module);
	return true;
}

TEST(ComptimeCallInitIfFolds) {
	const char* source = R"(
		fn choose() : i32 {
			var x : i32 = 3;
			if x > 2 {
				x = 42;
			} else {
				x = 0;
			}
			return x;
		}
		comptime C = choose();
		fn main() : i32 {
			return C;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeCallInitLoopBuildsArray) {
	// Loop that fills an aggregate local, returned as the comptime value.
	const char* source = R"(
		fn build() : [3]i32 {
			var a : [3]i32 = [0, 0, 0];
			a[0] = 20;
			a[1] = 21;
			a[2] = 1;
			return a;
		}
		comptime A = build();
		fn main() : i32 {
			var a : [3]i32 = A;
			return a[0] + a[1] + a[2];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeParametricRecursionFolds) {
	// A parametric, recursive comptime function must be interpreted at compile time.
	// Using the result as an array length forces compile-time evaluation, and the
	// runtime use must observe the same folded constant.
	const char* source = R"(
		fn fib(n : i32) : i32 {
			if n < 2 { return n; }
			return fib(n - 1) + fib(n - 2);
		}
		comptime C = fib(9);
		fn main() : [C]i32 {
			var a : [C]i32 = undefined;
			return a;
		}
	)";
	EXPECT_COMPILE(source); // [fib(9)]i32 == [34]i32 must type-check
	return true;
}

TEST(ComptimeParametricCallFoldsToValue) {
	const char* source = R"(
		fn square(n : i32) : i32 { return n * n; }
		comptime C = square(6) + 6;
		fn main() : i32 { return C; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1)); // 36 + 6
	CAPI_END(module);
	return true;
}

TEST(ComptimeCallInitReadsLocalAggregateRvalue) {
	// The interpreter must fold a body that reads local aggregate elements/fields as
	// rvalues (a[i] with a runtime-style index, p.x) inside a loop.
	const char* source = R"(
		struct P { x : i32; y : i32; }
		fn compute() : i32 {
			var a : [3]i32 = [10, 11, 12];
			var p : P = P { 4, 5 };
			var sum : i32 = 0;
			var i : i32 = 0;
			while i < 3 {
				sum = sum + a[i];
				i = i + 1;
			}
			return sum + p.x + p.y;
		}
		comptime C = compute();
		fn main() : i32 { return C; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1)); // 10+11+12 + 4 + 5
	CAPI_END(module);
	return true;
}
