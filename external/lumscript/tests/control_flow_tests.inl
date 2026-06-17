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
