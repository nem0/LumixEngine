TEST(BreakContinueTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			while i < 10 {
				i += 1;
				if i == 3 {
					continue;
				}
				if i == 8 {
					break;
				}
			}
			return i;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ForLoopCompiles) {
	const char* source = R"(
		fn main() : void {
			for i = 0..9 {
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// The i32 passed to bound checking is a hint for untyped literals, not a
// requirement: matching concrete integer bounds of any width are valid.
TEST(ForLoopI64Bounds) {
	const char* source = R"(
		fn main() : void {
			var from : i64 = 0;
			var to : i64 = 9;
			for i = from..to {
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ForLoopRangeBoundsMustMatchTypeFails) {
	const char* source = R"(
		fn main() : void {
			var from : i32 = 0;
			var to : f32 = 1.5;
			for i = from..to {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ForLoopRangeRequiresNumericBoundsFails) {
	const char* source = R"(
		fn main() : void {
			for i = 0.."9" {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ForLoopVariableIsImmutable) {
	const char* source = R"(
		fn main() : void {
			for i = 0..9 {
				i = 1;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BreakContinueOutsideLoopFail) {
	const char* source = R"(
		fn main() : void {
			break;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(WhileConditionMustBeBoolFails) {
	const char* source = R"(
		fn main() : void {
			while 1 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NamedLabelTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			outer: while i < 10 {
				i += 1;
				var j : i32 = 0;
				while j < 10 {
					j += 1;
					if j == 2 {
						continue outer;
					}
				}
			}
			return i;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NamedLabelUnknownFails) {
	const char* source = R"(
		fn main() : void {
			while true {
				break missing;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DuplicateNamedLabelFails) {
	const char* source = R"(
		fn main() : void {
			outer: while true {
				outer: while true {
					break;
				}
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SequentialNamedLabelReuseCompiles) {
	const char* source = R"(
		fn main() : void {
			outer: while true {
				break;
			}
			outer: while true {
				break;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(DuplicateLoopLabelFails) {
	const char* source = R"(
		fn main() : void {
			outer: while true {
				break;
			}
			outer: for i = 0..1 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(203, ls_to_i32(runtime, -1));
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(201, ls_to_i32(runtime, -1));
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
			return i + hits;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
