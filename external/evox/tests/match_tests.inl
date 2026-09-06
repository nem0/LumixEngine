TEST(MatchTypechecks) {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		}
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
				case 1..=9, 99:
					return 1;
				case:
					return 2;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MatchStringTypechecks) {
	const char* source = R"(
		fn classify(value : []const u8) : i32 {
			match value {
				case "start", "run":
					return 1;
				case "stop":
					return 2;
				case:
					return 0;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MatchArmAllowsMultipleStatements) {
	const char* source = R"(
		fn main(v : i32) : i32 {
			var result : i32 = 0;
			match v {
				case 0:
					result = 1;
					result += 2;
				case:
					result = 10;
					result += 20;
			}
			return result;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MatchFallbackAfterIf) {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case 0:
					if false {}
				case:
					return;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MatchTwoFallbacksFail) {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case 0:
					if false {}
				case:
				case:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
TEST(MatchRejectsElseFallback) {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case 0:
					return;
				else:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRejectsPatternTypeMismatch) {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case "bad":
					return;
				case:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRangeRequiresNumericTypeFails) {
	const char* source = R"(
		fn main(v : []const u8) : void {
			match v {
				case "a".."z":
					return;
				case:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRangeTypeMismatchFails) {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case 1.."bad":
					return;
				case:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRequiresScalarEnumOrStringFails) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn main() : void {
			var v : Vec2 = Vec2 { 1, 2 };
			match v {
				case:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchDuplicateFallbackFails) {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case:
					return;
				case 1:
					return;
				case:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRequiresExhaustiveEnumOrFallback) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
		fn main(state : State) : void {
			match state {
				case .Idle:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRejectsDuplicateEnumCase) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
		fn main(state : State) : void {
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

TEST(MatchExhaustiveEnumWithMoreThan32Members) {
	const char* source = R"(
		enum Value {
			V00, V01, V02, V03, V04, V05, V06, V07,
			V08, V09, V10, V11, V12, V13, V14, V15,
			V16, V17, V18, V19, V20, V21, V22, V23,
			V24, V25, V26, V27, V28, V29, V30, V31,
			V32
		}

		fn main(value : Value) : void {
			match value {
				case .V00, .V01, .V02, .V03, .V04, .V05, .V06, .V07:
					return;
				case .V08, .V09, .V10, .V11, .V12, .V13, .V14, .V15:
					return;
				case .V16, .V17, .V18, .V19, .V20, .V21, .V22, .V23:
					return;
				case .V24, .V25, .V26, .V27, .V28, .V29, .V30, .V31:
					return;
				case .V32:
					return;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(BytecodeEnumMatch) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(2, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(MatchRuntime) {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		}
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
				case 1..=9, 99:
					return 1;
				case:
					return 2;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ex_push_i32(runtime, 0);
	EXPECT_TRUE(ex_call(runtime, toLs("enum_match")));
	EXPECT_EQ(1, ex_to_i32(runtime, -1));
	ex_push_i32(runtime, 2);
	EXPECT_TRUE(ex_call(runtime, toLs("enum_match")));
	EXPECT_EQ(2, ex_to_i32(runtime, -1));
	ex_push_i32(runtime, 5);
	EXPECT_TRUE(ex_call(runtime, toLs("range_match")));
	EXPECT_EQ(1, ex_to_i32(runtime, -1));
	ex_push_i32(runtime, 42);
	EXPECT_TRUE(ex_call(runtime, toLs("range_match")));
	EXPECT_EQ(2, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(MatchStringRuntimeUsesContentEquality) {
	const char* source = R"(
		fn classify(value : []const u8) : i32 {
			match value {
				case "start", "run":
					return 1;
				case "stop":
					return 2;
				case:
					return 0;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	ex_push_string(runtime, {"run", 3});
	EXPECT_TRUE(ex_call(runtime, toLs("classify")));
	EXPECT_EQ(1, ex_to_i32(runtime, -1));

	// The input is not the pooled literal used by the match arm.
	const char input[] = {'s', 't', 'o', 'p'};
	ex_push_string(runtime, {input, 4});
	EXPECT_TRUE(ex_call(runtime, toLs("classify")));
	EXPECT_EQ(2, ex_to_i32(runtime, -1));

	ex_push_string(runtime, {"unknown", 7});
	EXPECT_TRUE(ex_call(runtime, toLs("classify")));
	EXPECT_EQ(0, ex_to_i32(runtime, -1));
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
				case:
					result = 10;
					result += 20;
			}
			return result;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	ex_push_i32(runtime, 0);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(3, ex_to_i32(runtime, -1));
	ex_push_i32(runtime, 7);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(30, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(MatchFallbackFirstRuntime) {
	const char* source = R"(
		fn main(v : i32) : i32 {
			match v {
				case:
					return 10;
				case 7:
					return 20;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	ex_push_i32(runtime, 7);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(20, ex_to_i32(runtime, -1));
	ex_push_i32(runtime, 3);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(10, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
