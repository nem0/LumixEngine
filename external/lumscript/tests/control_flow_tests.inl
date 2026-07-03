TEST(DeferTypechecks) {
	const char* source = R"(
		fn cleanup(v : ref i32) : void {
			v += 1;
		}

		fn main() : i32 {
			var x : i32 = 0;
			defer cleanup(ref x);
			x += 2;
			return x;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}


TEST(DeferCanNotWrapReturnInBlock) {
	const char* source = R"(
		fn main() : i32 {
			defer { return 1; }
			return 2;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(DeferCanNotWrapReturn) {
	const char* source = R"(
		fn main() : i32 {
			defer return 1;
			return 2;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NestedDeferCanNotWrapReturn) {
	const char* source = R"(
		fn main() : i32 {
			defer {
				defer {}
				return 1;
			}
			return 2;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IfConditionMustBeBoolFails) {
	const char* source = R"(
		fn main() : void {
			if 1 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FirstClassFunctionsTypecheck) {
	const char* source = R"(
		fn add(a : i32, b : i32) : i32 {
			return a + b;
		}

		fn apply(f : fn(i32, i32) : i32, a : i32, b : i32) : i32 {
			return f(a, b);
		}

		fn main() : i32 {
			const f = add;
			const g : fn(i32, i32) : i32 = add;
			return apply(f, 20, 2) + g(10, 5);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NestedFunctionsTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			fn add(a : i32, b : i32) : i32 {
				return a + b;
			}
			return add(20, 22);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NestedFunctionCanNotCaptureOuterLocal) {
	const char* source = R"(
		fn main() : i32 {
			const x = 10;
			fn get() : i32 {
				return x;
			}
			return get();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// A non-void function whose `if` branch does not cover the `else` case must
// be rejected at compile time, otherwise it would fall off the end of its
// bytecode at runtime and silently return zeroed/garbage data.
TEST(NonVoidFunctionMissingReturnOnAllPathsFails) {
	const char* source = R"(
		fn maybeOne(cond : bool) : i32 {
			if cond {
				return 1;
			}
		}

		fn main() : i32 {
			return maybeOne(true);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// if/else where both branches return is exhaustive and must compile.
TEST(NonVoidFunctionReturnsOnAllPathsViaIfElseCompiles) {
	const char* source = R"(
		fn maybeOne(cond : bool) : i32 {
			if cond {
				return 1;
			}
			else {
				return 0;
			}
		}

		fn main() : i32 {
			return maybeOne(true);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// An else-if chain only counts as exhaustive if the final `else` also returns.
TEST(NonVoidFunctionElseIfChainMissingFinalElseFails) {
	const char* source = R"(
		fn classify(v : i32) : i32 {
			if v < 0 {
				return -1;
			}
			else if v == 0 {
				return 0;
			}
		}

		fn main() : i32 {
			return classify(1);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NonVoidFunctionElseIfChainWithFinalElseCompiles) {
	const char* source = R"(
		fn classify(v : i32) : i32 {
			if v < 0 {
				return -1;
			}
			else if v == 0 {
				return 0;
			}
			else {
				return 1;
			}
		}

		fn main() : i32 {
			return classify(1);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// A trailing statement after a guaranteed return is unreachable but still
// legal; the function as a whole is still guaranteed to return.
TEST(NonVoidFunctionReturnFollowedByUnreachableCodeCompiles) {
	const char* source = R"(
		fn always() : i32 {
			if true {
				return 1;
			}
			else {
				return 2;
			}
			var unreachable : i32 = 3;
		}

		fn main() : i32 {
			return always();
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// A `while` loop body is never guaranteed to run, so a return only inside the
// loop body does not make the function exhaustive.
TEST(NonVoidFunctionReturnOnlyInsideWhileLoopFails) {
	const char* source = R"(
		fn first(cond : bool) : i32 {
			while cond {
				return 1;
			}
		}

		fn main() : i32 {
			return first(true);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Same reasoning applies to `for` loops.
TEST(NonVoidFunctionReturnOnlyInsideForLoopFails) {
	const char* source = R"(
		fn first() : i32 {
			for i in 0..10 {
				return i;
			}
		}

		fn main() : i32 {
			return first();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// A match statement without a fallback (`else`) arm is not treated as
// exhaustive by this check, even though the caller passes only covered values.
TEST(NonVoidFunctionMatchWithoutFallbackFails) {
	const char* source = R"(
		fn classify(v : i32) : i32 {
			match v {
				case 0:
					return 0;
				case 1:
					return 1;
			}
		}

		fn main() : i32 {
			return classify(0);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// A match statement with a fallback arm, where every arm returns, is exhaustive.
TEST(NonVoidFunctionMatchWithFallbackAllArmsReturnCompiles) {
	const char* source = R"(
		fn classify(v : i32) : i32 {
			match v {
				case 0:
					return 0;
				else:
					return 1;
			}
		}

		fn main() : i32 {
			return classify(0);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// A match with a fallback arm still fails if any single arm doesn't return.
TEST(NonVoidFunctionMatchWithFallbackOneArmMissingReturnFails) {
	const char* source = R"(
		fn classify(v : i32) : i32 {
			match v {
				case 0:
					var x : i32 = 0;
				else:
					return 1;
			}
		}

		fn main() : i32 {
			return classify(0);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// A labeled block wrapping a returning if/else should still be recognized.
TEST(NonVoidFunctionLabeledIfElseCompiles) {
	const char* source = R"(
		fn maybeOne(cond : bool) : i32 {
			outer: if cond {
				return 1;
			}
			else {
				return 0;
			}
		}

		fn main() : i32 {
			return maybeOne(true);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// Void functions are unaffected by the exhaustiveness check: falling off the
// end without an explicit return is fine.
TEST(VoidFunctionWithoutExplicitReturnCompiles) {
	const char* source = R"(
		fn log(v : i32) : void {
			if v > 0 {
				return;
			}
		}

		fn main() : void {
			log(1);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NestedFunctionNotGloballyVisible) {
	const char* source = R"(
		fn main() : i32 {
			fn local() : i32 {
				return 1;
			}
			return local();
		}

		fn other() : i32 {
			return local();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
