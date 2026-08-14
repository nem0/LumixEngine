TEST(ir_to_bytecode_basic) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { return 2 + 3; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_compare_and_if) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { if 2 < 3 { return 7; } else { return 9; } }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 7);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_locals) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { var x : i32 = 2; x += 3; return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_module_call) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn helper(x : i32) : i32 { return x + 1; } fn main() : i32 { return helper(2); }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 3);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_globals) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("var g : i32 = 4; fn main() : i32 { g += 2; return g; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

static void irNativeAdd(ls_runtime*, ls_call_frame frame) {
	LS_ARG(frame, i32, value);
	LS_RESULT(frame, value + 5);
}

TEST(ir_to_bytecode_while_loop) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { var x : i32 = 0; while x < 3 { x += 1; } return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 3);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_loop_control) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { var x : i32 = 0; while x < 10 { x += 1; if x == 3 { continue; } if x == 5 { break; } } return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_for_range) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { var sum : i32 = 0; for i in 0 .. 4 { sum += i; } return sum; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_access) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; values[1] = 8; return values[1]; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 8);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_bounds_check) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(R"(
		fn valid() : i32 { var values : [3]i32 = undefined; var index : i64 = 1; values[index] = 8; return values[index]; }
		fn negative() : i32 { var values : [3]i32 = undefined; var index : i64 = -1; return values[index]; }
		fn past_end() : i32 { var values : [3]i32 = undefined; var index : u64 = 3; values[index] = 8; return 0; }
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("valid")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 8);
	EXPECT_EQ(ls_call(runtime, toLs("negative")), LS_RESULT_FAILURE);
	EXPECT_EQ(ls_call(runtime, toLs("past_end")), LS_RESULT_FAILURE);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_undefined_array_and_greater_than) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = undefined; for i in 0..3 { values[i] = i; } if values[2] > 1 { return values[2]; } return 0; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 2);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_for) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 1, 2, 3 ]; var sum : i32 = 0; for value in values { sum += value; } return sum; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_slice_access) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var view : []i32 = values[0:3]; view[1] = 8; return view[1]; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(ls_call(runtime, toLs("main")), LS_RESULT_OK);
	EXPECT_EQ(ls_to_i32(runtime, -1), 8);
	CAPI_END(module);
	return true;
}
