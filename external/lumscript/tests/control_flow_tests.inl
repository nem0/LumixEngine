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

TEST(ExtendedScalarTypesTypecheck) {
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
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedLiteralsUseExpectedTypes) {
	const char* source = R"(
		struct Pair {
			x : u8;
			y : f64;
		}

		fn takes_f32(v : f32) : f32 {
			return v;
		}

		fn takes_i16(v : i16) : i16 {
			return v;
		}

		fn returns_i64() : i64 {
			return 42;
		}

		fn main() : f32 {
			const a : i8 = 10;
			const b : u16 = 20;
			const c : f64 = 1.5;
			const d = Pair { 255, 2.5 };
			return takes_f32(12) + takes_i16(3) as f32 + d.y as f32 + returns_i64() as f32 + a as f32 + b as f32 + c as f32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(DecimalLiteralDoesNotConcretizeToInteger) {
	const char* source = R"(
		fn main() : i32 {
			const a : i32 = 1.5;
			return a;
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
