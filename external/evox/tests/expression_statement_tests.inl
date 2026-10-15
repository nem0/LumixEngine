TEST(IgnoredCallResultIsCompileError) {
	EXPECT_COMPILE_FAIL(R"(
		fn compute() : i32 { return 1; }
		fn main() : void {
			compute();
		}
	)");

	EXPECT_COMPILE_FAIL(R"(
		struct Pair { x : i32; y : i32; }
		fn make() : Pair { return Pair { 1, 2 }; }
		fn main() : void {
			make();
		}
	)");

	EXPECT_COMPILE_FAIL(R"(
		fn maybe() : ?i32 { return 3; }
		fn main() : void {
			maybe();
		}
	)");

	EXPECT_COMPILE_FAIL(R"(
		fn values() : []const u8 { return "abc"; }
		fn main() : void {
			values();
		}
	)");
	return true;
}

TEST(IgnoredCallResultInNestedBlockIsCompileError) {
	EXPECT_COMPILE_FAIL(R"(
		fn compute() : i32 { return 1; }
		fn main() : void {
			if true {
				compute();
			}
		}
	)");

	EXPECT_COMPILE_FAIL(R"(
		fn compute() : i32 { return 1; }
		fn main() : void {
			var i = 0;
			while i < 3 {
				compute();
				i += 1;
			}
		}
	)");
	return true;
}

TEST(IgnoredCallResultFromNativeStyleCallIsCompileError) {
	EXPECT_COMPILE_FAIL(R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn main() : void {
			add(1, 2);
		}
	)");
	return true;
}

TEST(ExplicitDiscardOfCallResultCompiles) {
	const char* source = R"(
		fn compute() : i32 { return 1; }
		fn main() : i32 {
			_ = compute();
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(0, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ExplicitDiscardStillExecutesCall) {
	const char* source = R"(
		var calls : i32 = 0;
		fn compute() : i32 {
			calls += 1;
			return calls;
		}
		fn main() : i32 {
			_ = compute();
			_ = compute();
			return calls;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(2, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ExplicitDiscardInNestedBlockCompiles) {
	const char* source = R"(
		fn compute() : i32 { return 7; }
		fn main() : i32 {
			var total = 0;
			if true {
				_ = compute();
				total = 1;
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(1, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VoidCallStatementStillCompiles) {
	const char* source = R"(
		var value : i32 = 0;
		fn set(v : i32) : void { value = v; }
		fn main() : i32 {
			set(5);
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(5, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UsedCallResultStillCompiles) {
	EXPECT_COMPILE(R"(
		fn compute() : i32 { return 1; }
		fn use(v : i32) : void {}
		fn main() : i32 {
			const a = compute();
			use(compute());
			return a + compute();
		}
	)");
	return true;
}

TEST(DiscardOfVoidCallIsCompileError) {
	EXPECT_COMPILE_FAIL(R"(
		fn nothing() : void {}
		fn main() : void {
			_ = nothing();
		}
	)");
	return true;
}

TEST(DiscardTargetRejectsCompoundAssignment) {
	EXPECT_COMPILE_FAIL(R"(
		fn compute() : i32 { return 1; }
		fn main() : void {
			_ += compute();
		}
	)");
	return true;
}
