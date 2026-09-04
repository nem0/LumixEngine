
TEST(InterpolationBinaryExpression) {
	EXPECT_COMPILE(R"(
		fn sink(a : []const u8, value : i32) : i32 { return value; }
		fn main() : i32 { var value : i32 = 4; return sink(`sum {value + 3}`); }
	)");
	return true;
}

TEST(InterpolationMultilineString) {
	EXPECT_COMPILE(R"(
		fn sink(prefix : []const u8, value : i32, suffix : []const u8) : i32 { return value; }
		fn main() : i32 { var value : i32 = 7; return sink(`line
{value}
end`); }
	)");
	return true;
}

TEST(OrdinaryStringBracesAreLiteral) {
	const char* source = R"(
		fn length(value : []const u8) : i32 { return value.length as i32; }
		fn main() : i32 { return length("{ }"); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(3, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(InterpolationEscapedOpenBrace) {
	const char* source = R"(
		fn length(value : []const u8) : i32 { return value.length as i32; }
		fn main() : i32 { return length(`{{`) * 10 + length(`}}`); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(12, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(InterpolationRejectsNestedBraces) {
	EXPECT_COMPILE_FAIL(R"(
		fn sink(prefix : []const u8, value : i32) : void {}
		fn main() : void { sink(`nested {1 + {2}}`); }
	)");
	return true;
}

TEST(InterpolationMixedCallArguments) {
	EXPECT_COMPILE(R"(
		fn foo(a : i32, prefix : []const u8, value : i32, suffix : []const u8, c : f64) : i32 { return value; }
		fn main() : i32 { var value : i32 = 42; return foo(42, `some {value} abc`, 69.0); }
	)");
	return true;
}

TEST(InterpolationFunctionCall) {
	EXPECT_COMPILE(R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn sink(a : []const u8, value : i32, b : []const u8) : i32 { return value; }
		fn main() : i32 { return sink(`call {add(10, 32)} done`); }
	)");
	return true;
}

TEST(InterpolationMemberAccess) {
	EXPECT_COMPILE(R"(
		struct Pair { value : i32; }
		fn sink(a : []const u8, value : i32) : i32 { return value; }
		fn main() : i32 { var pair : Pair = Pair { 19 }; return sink(`member {pair.value}`); }
	)");
	return true;
}

TEST(InterpolationIndexExpression) {
	EXPECT_COMPILE(R"(
		fn sink(a : []const u8, value : i32) : i32 { return value; }
		fn main() : i32 { var values : [2]i32 = [ 7, 8 ]; return sink(`index {values[1]}`); }
	)");
	return true;
}

TEST(InterpolationNestedExpressions) {
	EXPECT_COMPILE(R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn sink(a : []const u8, value : i32, b : []const u8) : i32 { return value; }
		fn main() : i32 { var value : i32 = 5; return sink(`nested {add(value * 2, 1)} end`); }
	)");
	return true;
}

TEST(InterpolationComplexExpressionTypeChecked) {
	EXPECT_COMPILE_FAIL(R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn sink(a : []const u8, value : f32, b : []const u8) : void {}
		fn main() : void { var value : i32 = 5; sink(`bad {add(value, true)}`); }
	)");
	return true;
}

TEST(InterpolationMissingExpression) {
	EXPECT_COMPILE_FAIL(R"(
		fn sink(value : []const u8) : void {}
		fn main() : void { sink(`missing { }`); }
	)");
	return true;
}

TEST(InterpolationMissingEnd) {
	EXPECT_COMPILE_FAIL(R"(
		fn sink(value : []const u8) : void {}
		fn main() : void { sink(`wrong {1;}`); }
	)");
	return true;
}

TEST(InterpolationMissingContinuation) {
	EXPECT_COMPILE_FAIL(R"(
		fn sink(value : []const u8) : void {}
		fn main() : void { sink(`wrong {1}
`); }
	)");
	return true;
}

TEST(InterpolationRuntimeBinaryAndCall) {
	const char* source = R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn sink(a : []const u8, value : i32) : i32 { return value; }
		fn main() : i32 {
			var value : i32 = 4;
			return sink(`binary {value + 3}`) + sink(`call {add(10, 32)}`);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(49, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(InterpolationRuntimeMemberIndexAndNested) {
	const char* source = R"(
		struct Pair { value : i32; }
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn sink(a : []const u8, value : i32) : i32 { return value; }
		fn main() : i32 {
			var value : i32 = 5;
			var pair : Pair = Pair { 19 };
			var values : [2]i32 = [ 7, 8 ];
			return sink(`member {pair.value}`) + sink(`index {values[1]}`) + sink(`nested {add(value * 2, 1)}`);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(38, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(InterpolationRuntimeMixedCallArguments) {
	const char* source = R"(
		fn foo(a : i32, prefix : []const u8, value : i32, suffix : []const u8, c : f64) : i32 { return value; }
		fn main() : i32 {
			var value : i32 = 42;
			return foo(42, `some {value} abc`, 69.0);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(InterpolationRuntimeMultipleParts) {
	const char* source = R"(
		fn sink(prefix : []const u8, first : i32, middle : []const u8, second : i32, suffix : []const u8) : i32 {
			return first * 10 + second;
		}
		fn main() : i32 {
			var first : i32 = 4;
			var second : i32 = 7;
			return sink(`left {first} middle {second} right`);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(47, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(InterpolationRuntimeVariadicAny) {
	const char* source = R"(
		fn classify(value : any) : i32 {
			match value {
				case i32: return 1;
				case bool: return 10;
				case f64: return 100;
				case: return 1000;
			}
		}
		fn count(values : ...any) : i32 { return values.length as i32; }
		fn main() : i32 {
			var value : i32 = 42;
			return count(`{value}{true}{69.0}`)
				+ classify(`{value}`) + classify(`{true}`) + classify(`{69.0}`);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(114, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
