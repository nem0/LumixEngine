// Variadic arguments are syntax sugar for a trailing slice parameter.

TEST(VariadicNoArguments) {
	const char* source = R"(
		fn count(values : ...i32) : i32 { return values.length as i32; }
		fn main() : i32 { return count(); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(0, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicArgumentsArePackedAsSlice) {
	const char* source = R"(
		fn sum(values : ...i32) : i32 {
			var result : i32 = 0;
			for value in values { result += value; }
			return result;
		}
		fn main() : i32 { return sum(1, 2, 3, 4); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(10, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicParameterMayFollowFixedParameters) {
	const char* source = R"(
		fn sum_from(start : i32, values : ...i32) : i32 {
			var result : i32 = start;
			for value in values { result += value; }
			return result;
		}
		fn main() : i32 { return sum_from(10, 1, 2, 3); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(16, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicAnyAcceptsHeterogeneousArguments) {
	const char* source = R"(
		fn count(values : ...any) : i32 { return values.length as i32; }
		fn main() : i32 { return count(1, true, "text"); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(3, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicAnyMatchesInterpolatedString) {
	const char* source = R"(
		fn logErrorString(value : []const u8) : i32 { return 1; }
		fn logError(args : ...any) : i32 {
			for arg in args {
				match arg {
					case []const u8: return logErrorString(arg);
					case: return 0;
				}
			}
			return 0;
		}
		fn main() : i32 {
			var name = "LumixEngine";
			return logError("Hello {name}");
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(1, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicFunctionValueUsesSliceFunctionType) {
	const char* source = R"(
		fn sum(values : ...i32) : i32 {
			var result : i32 = 0;
			for value in values { result += value; }
			return result;
		}
		fn apply(f : fn([]i32) : i32, values : []i32) : i32 { return f(values); }
		fn main() : i32 {
			var values : [3]i32 = [1, 2, 3];
			return apply(sum, values[:]);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(6, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeVariadicParameterIsRejected) {
	EXPECT_COMPILE_FAIL(R"(
		fn count(values : comptime ...i32) : i32 { return 0; }
	)");
	return true;
}

TEST(UnnamedVariadicFunctionType) {
	const char* source = R"(
		fn count(values : []i32) : i32 { return values.length as i32; }
		fn apply(f : fn(...i32) : i32) : i32 { return f(1, 2, 3); }
		fn main() : i32 { return apply(count); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(3, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicFunctionTypeMustBeFinal) {
	EXPECT_COMPILE_FAIL(R"(
		fn apply(f : fn(values : ...i32, suffix : i32) : void) : void {}
	)");
	return true;
}

TEST(VariadicParameterMustBeFinal) {
	EXPECT_COMPILE_FAIL(R"(
		fn invalid(values : ...i32, suffix : i32) : void {}
	)");
	return true;
}

TEST(VariadicParameterCannotBeRepeated) {
	EXPECT_COMPILE_FAIL(R"(
		fn invalid(a : ...i32, b : ...i32) : void {}
	)");
	return true;
}

TEST(VariadicFixedArgumentsWithoutVariadicArguments) {
	const char* source = R"(
		fn add_count(base : i32, values : ...i32) : i32 {
			return base + values.length as i32;
		}
		fn main() : i32 { return add_count(42); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicTemplateArgumentsArePackedOnce) {
	const char* source = R"(
		fn sum(T : comptime type, values : ...T) : T {
			var result : T = 0;
			for value in values { result += value; }
			return result;
		}
		fn main() : i32 { return sum(i32, 1, 2, 3); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(6, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicUFCSArgumentsArePacked) {
	const char* source = R"(
		struct Counter { base : i32; }
		fn add(counter : Counter, values : ...i32) : i32 {
			var result = counter.base;
			for value in values { result += value; }
			return result;
		}
		fn main() : i32 {
			const counter = Counter { 10 };
			return counter.add(1, 2, 3);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(16, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(VariadicArgumentTypeMismatchFails) {
	EXPECT_COMPILE_FAIL(R"(
		fn count(values : ...i32) : i32 { return values.length as i32; }
		fn main() : i32 { return count(1, true, 3); }
	)");
	return true;
}

TEST(VariadicParameterCannotBeUFCSReceiver) {
	EXPECT_COMPILE_FAIL(R"(
		struct Value { n : i32; }
		fn consume(values : ...Value) : void {}
		fn main() : void {
			const value = Value { 1 };
			value.consume();
		}
	)");
	return true;
}

TEST(EmptyArrayLiteralRemainsInvalid) {
	EXPECT_COMPILE_FAIL(R"(
		fn count(values : []i32) : i32 { return values.length as i32; }
		fn main() : i32 { return count([]); }
	)");
	return true;
}
