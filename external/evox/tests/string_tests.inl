TEST(StringLiteralIsConstU8Slice) {
	const char* source = R"(
		fn main() : i32 {
			const text : []const u8 = "Lumix";
			return text.length as i32 + text[0] as i32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(81, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(StringLiteralRejectsMutation) {
	const char* source = R"(
		fn main() : void {
			const text = "abc";
			text[0] = 0 as u8;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StringLiteralRejectsConcatenation) {
	const char* source = R"(
		fn main() : void {
			const text = "a" + "b";
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StringLiteralEqualityComparesContent) {
	const char* source = R"(
		fn main() : i32 {
			const a : []const u8 = "quit";
			const b : []const u8 = "quit";
			const c : []const u8 = "quiz";
			var result : i32 = 0;
			if a == b { result += 1; }
			if a == c { result += 10; }
			if a != c { result += 100; }
			if a != b { result += 1000; }
			return result;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(101, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(StringEqualityIgnoresBackingStorage) {
	// The bytes are assembled at runtime, so this cannot be satisfied by
	// comparing pointers into pooled literal storage.
	const char* source = R"(
		fn main() : i32 {
			var buffer : [3]u8 = undefined;
			buffer[0] = 'a';
			buffer[1] = 'b';
			buffer[2] = 'c';
			const built : []const u8 = buffer[:];
			if built == "abc" { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(StringEqualityComparesLengthFirst) {
	const char* source = R"(
		fn main() : i32 {
			const prefix : []const u8 = "ab";
			const longer : []const u8 = "abc";
			if prefix == longer { return 0; }
			if longer == prefix { return 0; }
			return 42;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(StringEqualityRejectsMixedSliceAndCStrOperands) {
	// `cstr` keeps address equality and does not mix with slice content equality.
	// Note that `cstr_text == "hello"` still compiles: the literal converts to
	// `cstr`, so that comparison is by address, not by content.
	const char* source = R"(
		fn main() : void {
			const text : []const u8 = "hello";
			const native : cstr = "hello";
			const equal = text == native;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StringIsOrdinaryIdentifier) {
	const char* source = R"(
		fn string() : i32 { return 42; }
		fn main() : i32 { return string(); }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(StringLiteralImplicitlyConvertsToCStr) {
	const char* source = R"(
		extern fn inspect(text : cstr) : i32;

		fn main() : i32 {
			const text : cstr = "native cstr";
			return inspect(text);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	auto inspect = [](ex_runtime* runtime, ex_call_frame frame) -> void {
		(void)runtime;
		EX_ARG(frame, const char*, text);
		EX_RESULT(frame, i32(text && strcmp(text, "native cstr") == 0 ? 42 : 0));
	};
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("inspect"), inspect) == EX_RESULT_OK);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ConstU8SliceDoesNotConvertToCStr) {
	const char* source = R"(
		extern fn inspect(text : cstr) : void;

		fn main() : void {
			const text : []const u8 = "hello";
			inspect(text);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
