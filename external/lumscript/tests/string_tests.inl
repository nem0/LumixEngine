TEST(StringConcatenationTypechecks) {
	const char* source = R"(
		fn join(a : string, b : string) : string {
			return a + " " + b;
		}

		fn main() : string {
			const hello : string = "Hello";
			const target = "Lumix";
			return join(hello, target);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}


TEST(StringConcatenationRejectsNonString) {
	const char* source = R"(
		fn main() : string {
			return "count: " + 42;
		}
	)";
	EXPECT_COMPILE_FAIL(source);

	const char* global_source = R"(
		const a = 1;
		fn main() : void {
			a = 2;
		}
	)";
	EXPECT_COMPILE_FAIL(global_source);
	return true;
}

TEST(StringIsReservedKeyword) {
	const char* source = R"(
		fn string() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StringEqualityAndInequalityRuntime) {
	const char* source = R"(
		fn main() : i32 {
			if "abc" == "abc" {
				if "abc" != "abd" {
					return 42;
				}
			}
			return 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(CStrIsBuiltinTypeAndImplicitlyAcceptsStringLiterals) {
	const char* source = R"(
		extern fn native_print(text : cstr) : void;

		fn main() : void {
			const text : cstr = "hello";
			native_print(text);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_END(module);
	return true;
}

TEST(StringImplicitlyConvertsToCStr) {
	const char* source = R"(
		fn main() : void {
			var text : string = "hello";
			var native_text : cstr = text;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_END(module);
	return true;
}

TEST(CStrCanExplicitlyConvertToString) {
	const char* source = R"(
		extern fn getNativeMessage() : cstr;

		fn main() : void {
			var native_text : cstr = getNativeMessage();
			var text : string = native_text as string;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_END(module);
	return true;
}

TEST(CStrStringRoundTripRuntime) {
	const char* source = R"(
		fn makeMessage() : string {
			return "runtime message";
		}

		fn main() : i32 {
			var text : string = makeMessage();
			var native_text : cstr = text;
			var copied : string = native_text as string;
			if copied == "runtime message" {
				return 42;
			}
			return 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(CStrNativeArgumentRuntime) {
	const char* source = R"(
		extern fn inspect(text : cstr) : i32;

		fn main() : i32 {
			return inspect("native cstr");
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	auto inspect = [](ls_runtime* runtime, ls_call_frame frame) -> void {
		(void)runtime;
		LS_ARG(frame, const char*, text);
		LS_RESULT(frame, i32(text && strcmp(text, "native cstr") == 0 ? 42 : 0));
	};
	CAPI_RUNTIME(module, runtime);
	const i32 fn_idx = ls_module_get_native_function_index(module, toLs("inspect"));
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, fn_idx, inspect) == LS_RESULT_OK);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(CStrNativeResultRuntime) {
	const char* source = R"(
		extern fn getNativeText() : cstr;

		fn main() : i32 {
			var text : string = getNativeText() as string;
			if text == "native result" {
				return 42;
			}
			return 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	auto get_native_text = [](ls_runtime* runtime, ls_call_frame frame) -> void {
		(void)runtime;
		const char* text = "native result";
		LS_RESULT(frame, text);
	};
	CAPI_RUNTIME(module, runtime);
	const i32 fn_idx = ls_module_get_native_function_index(module, toLs("getNativeText"));
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, fn_idx, get_native_text) == LS_RESULT_OK);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
