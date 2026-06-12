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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

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
