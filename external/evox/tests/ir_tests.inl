// Regression: inlined by-value aggregate parameters have no local alloca.
// Field/index lowering requests an address, which used to assert in IDENTIFIER.
TEST(ir_inline_aggregate_parameter_address) {
	const char* source = R"(
		struct Vec { x : i32; y : i32; }
		struct Nested { value : Vec; }
		var input : i32 = 3;
		fn squared(v : Vec) : i32 { return v.x * v.x + v.y * v.y; }
		fn nested(v : Nested) : i32 { return v.value.x + v.value.y; }
		fn indexed(v : [2]i32) : i32 { return v[0] + v[1]; }
		fn main() : i32 {
			var vector_arg : Vec = {input, 4};
			var nested_arg : Nested = {vector_arg};
			var array_arg : [2]i32 = [input, 4];
			return squared(vector_arg) + nested(nested_arg) + indexed(array_arg);
		}
	)";
	for (bool optimize : {false, true}) {
		CAPI_BEGIN(module, diagnostics);
		EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
		ex_bytecode_compile_options options = {};
		options.optimize = optimize;
		ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, &options);
		EXPECT_TRUE(bytecode != nullptr);
		ex_runtime* runtime = ex_runtime_create(bytecode, &module_host);
		EXPECT_TRUE(runtime != nullptr);
		EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
		EXPECT_EQ(ex_task_to_i32(runtime, -1), 39);
		test_runtime_destroy(runtime);
		ex_bytecode_destroy(bytecode);
		CAPI_END(module);
	}
	return true;
}

TEST(ir_to_bytecode_basic) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { return 2 + 3; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_compare_and_if) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { if 2 < 3 { return 7; } else { return 9; } }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 7);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_locals) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var x : i32 = 2; x += 3; return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_module_call) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn helper(x : i32) : i32 { return x + 1; } fn main() : i32 { return helper(2); }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 3);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_globals) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("var g : i32 = 4; fn main() : i32 { g += 2; return g; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

static void irNativeAdd(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, i32, value);
	EX_RESULT(frame, value + 5);
}

TEST(ir_to_bytecode_while_loop) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var x : i32 = 0; while x < 3 { x += 1; } return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 3);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_loop_control) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var x : i32 = 0; while x < 10 { x += 1; if x == 3 { continue; } if x == 5 { break; } } return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_for_range) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var sum : i32 = 0; for i in 0 .. 4 { sum += i; } return sum; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_access) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; values[1] = 8; return values[1]; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 8);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_bounds_check) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		fn valid() : i32 { var values : [3]i32 = undefined; var index : i64 = 1; values[index] = 8; return values[index]; }
		fn negative() : i32 { var values : [3]i32 = undefined; var index : i64 = -1; return values[index]; }
		fn past_end() : i32 { var values : [3]i32 = undefined; var index : u64 = 3; values[index] = 8; return 0; }
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;
	EXPECT_EQ(test_call(runtime, toLs("valid")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 8);

	// Runtime errors suspend with EX_DEBUG_PAUSE_ERROR instead of failing the
	// call (see DebugErrorSuspendsWhenEnabled); the host aborts to unwind.
	ex_debug_event event;
	EXPECT_EQ(test_call(runtime, toLs("negative")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);

	EXPECT_EQ(test_call(runtime, toLs("past_end")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);
	CAPI_END(module);
	return true;
}

// Exercises every index width through LOAD_INDEXED/STORE_INDEXED: unsigned
// kinds on the success path (previously only ever seen failing), and narrow
// signed kinds wrapping through i64 to u64 when rejected.
TEST(ir_to_bytecode_array_index_kinds) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		fn load_u8() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var index : u8 = 2; return values[index]; }
		fn load_u16() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var index : u16 = 1; return values[index]; }
		fn load_u32() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var index : u32 = 2; return values[index]; }
		fn store_u8() : i32 { var values : [3]i32 = undefined; var index : u8 = 2; values[index] = 7; return values[index]; }
		fn negative_i8() : i32 { var values : [3]i32 = undefined; var index : i8 = -1; return values[index]; }
		fn negative_i16() : i32 { var values : [3]i32 = undefined; var index : i16 = -1; return values[index]; }
		fn negative_i32() : i32 { var values : [3]i32 = undefined; var index : i32 = -1; return values[index]; }
		fn past_end_u8() : i32 { var values : [3]i32 = undefined; var index : u8 = 3; return values[index]; }
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_EQ(test_call(runtime, toLs("load_u8")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	EXPECT_EQ(test_call(runtime, toLs("load_u16")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 5);
	EXPECT_EQ(test_call(runtime, toLs("load_u32")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	EXPECT_EQ(test_call(runtime, toLs("store_u8")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 7);

	test_diagnostics.output_enabled = false;
	ex_debug_event event;
	EXPECT_EQ(test_call(runtime, toLs("negative_i8")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);

	EXPECT_EQ(test_call(runtime, toLs("negative_i16")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);

	EXPECT_EQ(test_call(runtime, toLs("negative_i32")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);

	EXPECT_EQ(test_call(runtime, toLs("past_end_u8")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_undefined_array_and_greater_than) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = undefined; for i in 0..3 { values[i] = i; } if values[2] > 1 { return values[2]; } return 0; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 2);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_for) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 1, 2, 3 ]; var sum : i32 = 0; for value in values { sum += value; } return sum; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_slice_access) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var view : []i32 = values[0:3]; view[1] = 8; return view[1]; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 8);
	CAPI_END(module);
	return true;
}
