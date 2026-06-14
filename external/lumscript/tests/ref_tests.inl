TEST(RefParameterTypechecks) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(ref x);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(RefParameterRequiresRefArgument) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefExpressionOnlyAllowedInArgumentsFails) {
	const char* source = R"(
		fn main() : void {
			var x : i32 = 10;
			ref x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentMustBeAssignable) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(ref (x + 1));
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentRejectsLiteral) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			increment(ref 1);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentTypeMismatchFails) {
	const char* source = R"(
		fn increment(v : ref f32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(ref x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentRejectsArrayToSliceConversion) {
	const char* source = R"(
		fn consume(values : ref i32[]) : void {
		}

		fn main() : void {
			var values : i32[4] = undefined;
			consume(ref values);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentCanNotBeConst) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			const x : i32 = 10;
			increment(ref x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentAllowsMutableGlobal) {
	const char* source = R"(
		var counter : i32 = 0;

		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			increment(ref counter);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(RefArgumentRejectsConstGlobal) {
	const char* source = R"(
		const counter : i32 = 0;

		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			increment(ref counter);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentAllowsNestedMutableField) {
	const char* source = R"(
		struct Stats {
			hp : i32;
		}
		struct Player {
			stats : Stats;
		}

		fn bump(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var p = Player { Stats { 10 } };
			bump(ref p.stats.hp);
		}
	)";
	EXPECT_COMPILE(source);
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeRefParameterRejectsLiteral) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			increment(ref 1);
		}
	)";

	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BytecodeRefParameterNestedFieldCall) {
	const char* source = R"(
		struct Stats {
			hp : i32;
		}
		struct Player {
			stats : Stats;
		}
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

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeRefParameterForwarding) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn forward(v : ref i32) : void {
			increment(ref v);
		}

		fn main() : i32 {
			var x : i32 = 40;
			forward(ref x);
			return x;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(41, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(BytecodeRefParameterAssignment) {
	const char* source = R"(
		fn assign(value : ref i32) : void {
			value = 42;
		}

		fn main() : i32 {
			var value : i32 = 0;
			assign(ref value);
			return value;
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

TEST(BytecodeRefParameterGlobal) {
	const char* source = R"(
		var value : i32 = 10;

		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : i32 {
			increment(ref value);
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(11, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

