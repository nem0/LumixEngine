#define CAPI_BEGIN(module_name, diagnostics_name) \
	TestContext diagnostics_name; \
	ls_module* module_name = ls_module_create(&diagnostics_name.host); \
	EXPECT_TRUE(module_name != nullptr); \
	auto& module_host = diagnostics_name.host; \
	auto& test_diagnostics = diagnostics_name.diagnostics

#define CAPI_END(module_name) \
	do {} while (false)

#define CAPI_RUNTIME(module_name, runtime_name) \
	RuntimeGuard runtime_name(module_name); \
	EXPECT_TRUE(runtime_name)

TEST(testNativeFunctionCall) {
	const char* source = R"(
		fn main() : i32 {
			return native_add(20, 22);
		}
	)";

	CAPI_BEGIN(module, diagnostics);

	ls_type params[] = {ls_type_make(LS_TYPE_I32), ls_type_make(LS_TYPE_I32)};
	EXPECT_TRUE(ls_module_add_native_function(module, toLs("native_add"), ls_type_make(LS_TYPE_I32), params, 2, &nativeAddC, nullptr) >= 0);

	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(42, result.i);
	CAPI_END(module);
	return true;
}

TEST(testStringConcatenationRuntime) {
	const char* source = R"(
		fn greet(name : string) : string {
			const hello = "Hello";
			return hello + ", " + name;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value arg = ls_value_make_string(toLs("Lumix"));
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("greet"), &arg, 1, &result, &module_host));
	EXPECT_TRUE(equalStrings(StringView(result.string.begin, result.string.end), "Hello, Lumix"));
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("to_f32"), nullptr, 0, &result, &module_host));
	EXPECT_FLOAT_EQ(10, result.f);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("to_i32"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(12, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("to_bool"), nullptr, 0, &result, &module_host));
	EXPECT_TRUE(result.b);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value arg = ls_value_make_i32(123);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("to_state"), &arg, 1, &result, &module_host));
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("to_i32"), &arg, 1, &result, &module_host));
	EXPECT_EQ(123, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	ls_value arg = ls_value_make_i32(0);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("enum_match"), &arg, 1, &result, &module_host));
	EXPECT_EQ(1, result.i);
	arg = ls_value_make_i32(2);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("enum_match"), &arg, 1, &result, &module_host));
	EXPECT_EQ(2, result.i);
	arg = ls_value_make_i32(5);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("range_match"), &arg, 1, &result, &module_host));
	EXPECT_EQ(1, result.i);
	arg = ls_value_make_i32(42);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("range_match"), &arg, 1, &result, &module_host));
	EXPECT_EQ(2, result.i);
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
		{ "math", math_source },
		{ "state", state_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &module_host, &resolveLumScriptImportC, &files));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(42, result.i);
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
		{ "entity_mod", entity_source },
		{ "helper_mod", helper_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &module_host, &resolveLumScriptImportC, &files));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(213, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("left_false"), nullptr, 0, &result, &module_host));
	EXPECT_TRUE(!result.b);

	TestContext diagnostics2;
	RuntimeGuard runtime2(module);
	EXPECT_TRUE(runtime2);
	EXPECT_TRUE(ls_runtime_call(runtime2, toLs("left_true"), nullptr, 0, &result, &diagnostics2.host));
	EXPECT_TRUE(result.b);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value arg = ls_value_make_i32(4);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("classify"), &arg, 1, &result, &module_host));
	EXPECT_EQ(1, result.i);

	ls_value arg2 = ls_value_make_i32(11);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("classify"), &arg2, 1, &result, &module_host));
	EXPECT_EQ(2, result.i);

	ls_value arg3 = ls_value_make_i32(-1);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("classify"), &arg3, 1, &result, &module_host));
	EXPECT_EQ(0, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("read_counter"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(1, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("increment"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(3, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("increment"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(5, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("read_counter"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(5, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("shadow_counter"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(101, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("read_counter"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(5, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("scoped"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(1, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(11, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(5, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(3, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(11, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(42, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(7, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(211, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("u8_add_wrap"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(0, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("i8_add_wrap"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(-128, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("u8_add_assign_wrap"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(0, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("cast_i8_wrap"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(-1, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("cast_u8_wrap"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(0, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("q_pos"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(2, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("q_neg"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(-2, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("r_neg_left"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(-1, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("r_neg_right"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(1, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("float_div"), nullptr, 0, &result, &module_host));
	EXPECT_TRUE(isinf((double)result.f));
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value args[] = { ls_value_make_i32(10), ls_value_make_i32(0) };
	ls_value result = {};
	test_diagnostics.output_enabled = false;
	EXPECT_TRUE(!ls_runtime_call(runtime, toLs("divide"), args, 2, &result, &module_host));
	EXPECT_TRUE(test_diagnostics.has_error);

	TestContext diagnostics2;
	RuntimeGuard runtime2(module);
	EXPECT_TRUE(runtime2);
	diagnostics2.diagnostics.output_enabled = false;
	EXPECT_TRUE(!ls_runtime_call(runtime2, toLs("modulo"), args, 2, &result, &diagnostics2.host));
	EXPECT_TRUE(diagnostics2.diagnostics.has_error);

	TestContext diagnostics3;
	RuntimeGuard runtime3(module);
	EXPECT_TRUE(runtime3);
	ls_value assign_arg = ls_value_make_i32(0);
	diagnostics3.diagnostics.output_enabled = false;
	EXPECT_TRUE(!ls_runtime_call(runtime3, toLs("divide_assign"), &assign_arg, 1, &result, &diagnostics3.host));
	EXPECT_TRUE(diagnostics3.diagnostics.has_error);
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
	bool compiled = ls_module_compile(module, toLs(source), {}, &module_host, &resolveLumScriptImportC, nullptr);
	EXPECT_TRUE(compiled);

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("vec3_sum"), nullptr, 0, &result, &module_host));
	EXPECT_FLOAT_EQ(6, result.f);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("integer_widths"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(467, result.i);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("return_f64"), nullptr, 0, &result, &module_host));
	EXPECT_FLOAT_EQ(1.5f, (float)result.d);
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
	const bool ok = ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr);
	EXPECT_TRUE(ok);

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(64, result.i);
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
	const bool ok = ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr);
	EXPECT_TRUE(ok);

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(42, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(42, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_value arg = ls_value_make_i32(5);
	ls_value result = {};
	test_diagnostics.output_enabled = false;
	EXPECT_TRUE(!ls_runtime_call(runtime, toLs("main"), &arg, 1, &result, &module_host));
	EXPECT_TRUE(test_diagnostics.has_error);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(25, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), nullptr, 0, &result, &module_host));
	EXPECT_EQ(57, result.i);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	ls_value arg = ls_value_make_i32(0);
	ls_value result = {};
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), &arg, 1, &result, &module_host));
	EXPECT_EQ(3, result.i);
	arg = ls_value_make_i32(7);
	EXPECT_TRUE(ls_runtime_call(runtime, toLs("main"), &arg, 1, &result, &module_host));
	EXPECT_EQ(30, result.i);
	CAPI_END(module);
	return true;
}
