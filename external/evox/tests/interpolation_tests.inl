
TEST(InterpolationBinaryExpression) {
	EXPECT_COMPILE(R"(
		fn sink(a : []const u8, value : i32, b : []const u8) : i32 { return value; }
		fn main() : i32 { var value : i32 = 4; return sink("sum {value + 3}"); }
	)");
	return true;
}

TEST(InterpolationMixedCallArguments) {
	EXPECT_COMPILE(R"(
		fn foo(a : i32, prefix : []const u8, value : i32, suffix : []const u8, c : f64) : i32 { return value; }
		fn main() : i32 { var value : i32 = 42; return foo(42, "some {value} abc", 69.0); }
	)");
	return true;
}

TEST(InterpolationFunctionCall) {
	EXPECT_COMPILE(R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn sink(a : []const u8, value : i32, b : []const u8) : i32 { return value; }
		fn main() : i32 { return sink("call {add(10, 32)} done"); }
	)");
	return true;
}

TEST(InterpolationMemberAccess) {
	EXPECT_COMPILE(R"(
		struct Pair { value : i32; }
		fn sink(a : []const u8, value : i32, b : []const u8) : i32 { return value; }
		fn main() : i32 { var pair : Pair = Pair { 19 }; return sink("member {pair.value}"); }
	)");
	return true;
}

TEST(InterpolationIndexExpression) {
	EXPECT_COMPILE(R"(
		fn sink(a : []const u8, value : i32, b : []const u8) : i32 { return value; }
		fn main() : i32 { var values : [2]i32 = [ 7, 8 ]; return sink("index {values[1]}"); }
	)");
	return true;
}

TEST(InterpolationNestedExpressions) {
	EXPECT_COMPILE(R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn sink(a : []const u8, value : i32, b : []const u8) : i32 { return value; }
		fn main() : i32 { var value : i32 = 5; return sink("nested {add(value * 2, 1)} end"); }
	)");
	return true;
}

TEST(InterpolationComplexExpressionTypeChecked) {
	EXPECT_COMPILE_FAIL(R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn sink(a : []const u8, value : f32, b : []const u8) : void {}
		fn main() : void { var value : i32 = 5; sink("bad {add(value, true)}"); }
	)");
	return true;
}
