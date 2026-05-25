TEST(BytecodeCompileAndRunMain) {
	const char* source = R"(
		fn main() : i32 {
			return 42;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &module_host, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_bytecode_runtime* runtime = ls_bytecode_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_bytecode_runtime_call(runtime, toLs("main"), &module_host));
	i32 result = ls_to_i32(runtime, -1);
	EXPECT_EQ(42, result);

	ls_bytecode_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}
