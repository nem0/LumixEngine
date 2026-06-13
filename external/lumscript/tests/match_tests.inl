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
				case 1..9, 99:
					return 1;
				else:
					return 2;
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
				else:
					result = 10;
					result += 20;
			}
			return result;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MatchRejectsPatternTypeMismatch) {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case "bad":
					return;
				else:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRangeRequiresNumericTypeFails) {
	const char* source = R"(
		fn main(v : string) : void {
			match v {
				case "a".."z":
					return;
				else:
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
				else:
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
				else:
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
				else:
					return;
				case 1:
					return;
				else:
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
				case 1..9, 99:
					return 1;
				else:
					return 2;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_push_i32(runtime, 0);
	EXPECT_TRUE(ls_call(runtime, toLs("enum_match")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	ls_push_i32(runtime, 2);
	EXPECT_TRUE(ls_call(runtime, toLs("enum_match")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	ls_push_i32(runtime, 5);
	EXPECT_TRUE(ls_call(runtime, toLs("range_match")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	ls_push_i32(runtime, 42);
	EXPECT_TRUE(ls_call(runtime, toLs("range_match")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
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
				else:
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
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	ls_push_i32(runtime, 7);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
