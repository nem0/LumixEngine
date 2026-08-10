TEST(DebugStackDepthZeroWhenNotFailed) {
	const char* source = R"(
		fn main() : i32 {
			return 42;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0u, ls_debug_stack_depth(runtime));
	CAPI_END(module);
	return true;
}

TEST(DebugStackTraceOnDivideByZero) {
	const char* source = R"(
		fn divide(a : i32, b : i32) : i32 {
			return a / b;
		}

		fn main() : i32 {
			return divide(10, 0);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;
	EXPECT_EQ(LS_RESULT_SUSPENDED, ls_call(runtime, toLs("main")));

	EXPECT_EQ(1u, ls_debug_stack_depth(runtime));

	const ls_string_view frame_name = ls_debug_frame_function_name(runtime, 0);
	EXPECT_TRUE(equalStrings(frame_name, toLs("main")));

	ls_debug_location location;
	EXPECT_TRUE(ls_debug_frame_location(runtime, 0, &location));
	EXPECT_EQ(3u, location.line);

	CAPI_END(module);
	return true;
}

TEST(DebugGlobalsTable) {
	const char* source = R"(
		var counter : i32 = 7;
		var ratio : f64 = 2.5;

		fn main() : i32 {
			return counter;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));

	EXPECT_EQ(2u, ls_debug_global_count(runtime));

	bool found_counter = false;
	bool found_ratio = false;
	for (u32 i = 0, count = ls_debug_global_count(runtime); i < count; ++i) {
		const ls_string_view name = ls_debug_global_name(runtime, i);
		if (equalStrings(name, toLs("counter"))) {
			found_counter = true;
			EXPECT_EQ((int)LS_TYPE_I32, (int)ls_debug_global_kind(runtime, i));
			u32 size = 0;
			const void* value = ls_debug_global_value(runtime, i, &size);
			EXPECT_TRUE(value != nullptr);
			EXPECT_EQ(4u, size);
			i32 int_value = 0;
			memcpy(&int_value, value, sizeof(int_value));
			EXPECT_EQ(7, int_value);
		}
		else if (equalStrings(name, toLs("ratio"))) {
			found_ratio = true;
			EXPECT_EQ((int)LS_TYPE_F64, (int)ls_debug_global_kind(runtime, i));
			u32 size = 0;
			const void* value = ls_debug_global_value(runtime, i, &size);
			EXPECT_TRUE(value != nullptr);
			EXPECT_EQ(8u, size);
			double double_value = 0;
			memcpy(&double_value, value, sizeof(double_value));
			EXPECT_FLOAT_EQ(2.5, (float)double_value);
		}
	}
	EXPECT_TRUE(found_counter);
	EXPECT_TRUE(found_ratio);

	CAPI_END(module);
	return true;
}

TEST(DebugSetBreakpointPatchesCode) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 1;
			value = value + 1;
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	u32 resolved_line = 0;
	EXPECT_TRUE(ls_debug_set_breakpoint(bytecode, makeStringView(__func__), 4u, &resolved_line));
	EXPECT_EQ(4u, resolved_line);

	const ls_function_bc& fn = bytecode->functions[0];
	u32 patched_offset = (u32)-1;
	for (u32 i = 0; i < fn.source_map_count; ++i) {
		if (fn.source_map[i].line == 4u) {
			patched_offset = fn.source_map[i].code_offset;
			break;
		}
	}
	EXPECT_TRUE(patched_offset != (u32)-1);
	EXPECT_EQ((int)LS_OP_BREAK, (int)fn.code[patched_offset]);

	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(DebugSetBreakpointOnUnknownLineFails) {
	const char* source = R"(
		fn main() : i32 {
			return 1;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	u32 resolved_line = 0;
	EXPECT_TRUE(!ls_debug_set_breakpoint(bytecode, makeStringView(__func__), 999u, &resolved_line));
	EXPECT_TRUE(!ls_debug_set_breakpoint(bytecode, toLs("no_such_source"), 2u, &resolved_line));

	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(DebugRemoveBreakpointRestoresOriginalByte) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 1;
			value = value + 1;
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	const ls_function_bc& fn = bytecode->functions[0];
	u32 patched_offset = (u32)-1;
	for (u32 i = 0; i < fn.source_map_count; ++i) {
		if (fn.source_map[i].line == 4u) {
			patched_offset = fn.source_map[i].code_offset;
			break;
		}
	}
	EXPECT_TRUE(patched_offset != (u32)-1);
	const u8 original_byte = fn.code[patched_offset];

	EXPECT_TRUE(ls_debug_set_breakpoint(bytecode, makeStringView(__func__), 4u, nullptr));
	EXPECT_EQ((int)LS_OP_BREAK, (int)fn.code[patched_offset]);

	EXPECT_TRUE(ls_debug_remove_breakpoint(bytecode, makeStringView(__func__), 4u));
	EXPECT_EQ((int)original_byte, (int)fn.code[patched_offset]);

	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(DebugRemoveAllBreakpointsRestoresEverything) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 1;
			value = value + 1;
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	const ls_function_bc& fn = bytecode->functions[0];
	u32 offset_line3 = (u32)-1;
	u32 offset_line4 = (u32)-1;
	for (u32 i = 0; i < fn.source_map_count; ++i) {
		if (fn.source_map[i].line == 3u && offset_line3 == (u32)-1) offset_line3 = fn.source_map[i].code_offset;
		if (fn.source_map[i].line == 4u && offset_line4 == (u32)-1) offset_line4 = fn.source_map[i].code_offset;
	}
	EXPECT_TRUE(offset_line3 != (u32)-1);
	EXPECT_TRUE(offset_line4 != (u32)-1);
	const u8 original_line3 = fn.code[offset_line3];
	const u8 original_line4 = fn.code[offset_line4];

	EXPECT_TRUE(ls_debug_set_breakpoint(bytecode, makeStringView(__func__), 3u, nullptr));
	EXPECT_TRUE(ls_debug_set_breakpoint(bytecode, makeStringView(__func__), 4u, nullptr));
	EXPECT_EQ((int)LS_OP_BREAK, (int)fn.code[offset_line3]);
	EXPECT_EQ((int)LS_OP_BREAK, (int)fn.code[offset_line4]);

	ls_debug_remove_all_breakpoints(bytecode);
	EXPECT_EQ((int)original_line3, (int)fn.code[offset_line3]);
	EXPECT_EQ((int)original_line4, (int)fn.code[offset_line4]);

	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(DebugBreakpointSuspendsWhenEnabled) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 1;
			value = value + 1;
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	u32 resolved_line = 0;
	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, &resolved_line));
	EXPECT_EQ(4u, resolved_line);

	EXPECT_TRUE(!ls_debug_is_suspended(runtime));

	const ls_result call_result = ls_call(runtime, toLs("main"));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)call_result);
	EXPECT_TRUE(ls_debug_is_suspended(runtime));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)LS_DEBUG_PAUSE_BREAKPOINT, (int)event.reason);
	EXPECT_EQ(4u, event.location.line);

	EXPECT_EQ(1u, ls_debug_stack_depth(runtime));
	const ls_string_view frame_name = ls_debug_frame_function_name(runtime, 0);
	EXPECT_TRUE(equalStrings(frame_name, toLs("main")));

	const ls_result resume_result = ls_debug_resume(runtime, LS_DEBUG_CONTINUE);
	EXPECT_EQ((int)LS_RESULT_OK, (int)resume_result);
	EXPECT_TRUE(!ls_debug_is_suspended(runtime));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugBreakpointSuspendsByDefault) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 1;
			value = value + 1;
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_debug_is_suspended(runtime));
	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugBreakpointHitAgainAfterResume) {
	const char* source = R"(
		fn main() : i32 {
			var total : i32 = 0;
			var i : i32 = 0;
			while i < 3 {
				total = total + 1;
				i = i + 1;
			}
			return total;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 6u, nullptr));

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugErrorSuspendsWhenEnabled) {
	const char* source = R"(
		fn divide(a : i32, b : i32) : i32 {
			return a / b;
		}

		fn main() : i32 {
			return divide(10, 0);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_debug_is_suspended(runtime));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)LS_DEBUG_PAUSE_ERROR, (int)event.reason);

	EXPECT_EQ(1u, ls_debug_stack_depth(runtime));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 0), toLs("main")));

	const ls_result resume_result = ls_debug_resume(runtime, LS_DEBUG_ABORT);
	EXPECT_EQ((int)LS_RESULT_FAILURE, (int)resume_result);
	EXPECT_TRUE(!ls_debug_is_suspended(runtime));

	CAPI_END(module);
	return true;
}

TEST(DebugAbortingSuspendedRuntimeUnblocksCalls) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 1;
			value = value + 1;
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	EXPECT_EQ((int)LS_RESULT_FAILURE, (int)ls_debug_resume(runtime, LS_DEBUG_ABORT));
	EXPECT_TRUE(!ls_debug_is_suspended(runtime));

	ls_debug_remove_all_breakpoints(runtime.bytecode);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugFrameLocalsVisibleWhenSuspended) {
	const char* source = R"(
		fn compute(a : i32) : i32 {
			var doubled : i32 = a * 2;
			return doubled;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));
	ls_push_i32(runtime, 5);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("compute")));

	EXPECT_EQ(2u, ls_debug_frame_local_count(runtime, 0));

	bool found_a = false;
	bool found_doubled = false;
	for (u32 i = 0, count = ls_debug_frame_local_count(runtime, 0); i < count; ++i) {
		const ls_string_view name = ls_debug_local_name(runtime, 0, i);
		if (equalStrings(name, toLs("a"))) {
			found_a = true;
			EXPECT_EQ((int)LS_TYPE_I32, (int)ls_debug_local_kind(runtime, 0, i));
			u32 size = 0;
			const void* value = ls_debug_local_value(runtime, 0, i, &size);
			EXPECT_TRUE(value != nullptr);
			EXPECT_EQ(4u, size);
			i32 int_value = 0;
			memcpy(&int_value, value, sizeof(int_value));
			EXPECT_EQ(5, int_value);
		}
		else if (equalStrings(name, toLs("doubled"))) {
			found_doubled = true;
			EXPECT_EQ((int)LS_TYPE_I32, (int)ls_debug_local_kind(runtime, 0, i));
			u32 size = 0;
			const void* value = ls_debug_local_value(runtime, 0, i, &size);
			EXPECT_TRUE(value != nullptr);
			EXPECT_EQ(4u, size);
			i32 int_value = 0;
			memcpy(&int_value, value, sizeof(int_value));
			EXPECT_EQ(10, int_value);
		}
	}
	EXPECT_TRUE(found_a);
	EXPECT_TRUE(found_doubled);

	CAPI_END(module);
	return true;
}

TEST(DebugFrameLocalsExcludeNotYetDeclaredLocal) {
	const char* source = R"(
		fn compute(a : i32) : i32 {
			var first : i32 = a;
			var second : i32 = first + 1;
			return second;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	// Break at line 4 (the "second" declaration): "first" already has a
	// value, but "second" itself hasn't been declared yet at this statement,
	// so only "a" and "first" should be reported.
	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));
	ls_push_i32(runtime, 7);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("compute")));

	bool found_second = false;
	for (u32 i = 0, count = ls_debug_frame_local_count(runtime, 0); i < count; ++i) {
		if (equalStrings(ls_debug_local_name(runtime, 0, i), toLs("second"))) found_second = true;
	}
	EXPECT_TRUE(!found_second);

	CAPI_END(module);
	return true;
}

TEST(DebugStepIntoStopsAtNextLine) {
	const char* source = R"(
		fn main() : i32 {
			var a : i32 = 1;
			var b : i32 = 2;
			var c : i32 = 3;
			return a + b + c;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 3u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ(3u, event.location.line);

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_STEP_INTO));
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)LS_DEBUG_PAUSE_STEP, (int)event.reason);
	EXPECT_EQ(4u, event.location.line);

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_STEP_INTO));
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ(5u, event.location.line);

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(6, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugStepIntoEntersCalledFunction) {
	const char* source = R"(
		fn helper() : i32 {
			var x : i32 = 10;
			return x;
		}

		fn main() : i32 {
			var y : i32 = helper();
			return y;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 8u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ(8u, event.location.line);
	EXPECT_EQ(1u, ls_debug_stack_depth(runtime));

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_STEP_INTO));
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ(3u, event.location.line);
	EXPECT_EQ(2u, ls_debug_stack_depth(runtime));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 0), toLs("helper")));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 1), toLs("main")));

	CAPI_END(module);
	return true;
}

TEST(DebugStepOverSkipsCalledFunction) {
	const char* source = R"(
		fn helper() : i32 {
			var x : i32 = 10;
			return x;
		}

		fn main() : i32 {
			var y : i32 = helper();
			return y;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 8u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_STEP_OVER));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ(9u, event.location.line);
	EXPECT_EQ(1u, ls_debug_stack_depth(runtime));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 0), toLs("main")));

	CAPI_END(module);
	return true;
}

TEST(DebugStepOutReturnsToCaller) {
	const char* source = R"(
		fn helper() : i32 {
			var x : i32 = 10;
			var doubled : i32 = x * 2;
			return doubled;
		}

		fn main() : i32 {
			var y : i32 = helper();
			return y;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ(4u, event.location.line);
	EXPECT_EQ(2u, ls_debug_stack_depth(runtime));

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_STEP_OUT));
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	// Stops at the call site (line 9, "var y = helper();"), immediately after
	// helper() returns into it - standard step-out convention (matches gdb's
	// `finish`, VS Code step-out), not the following statement.
	EXPECT_EQ(9u, event.location.line);
	EXPECT_EQ(1u, ls_debug_stack_depth(runtime));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 0), toLs("main")));

	CAPI_END(module);
	return true;
}

TEST(DebugBreakpointSuspendsThroughIndirectCall) {
	const char* source = R"(
		fn add_one(v : i32) : i32 {
			var result : i32 = v + 1;
			return result;
		}

		fn apply(f : fn(i32) : i32, value : i32) : i32 {
			return f(value);
		}

		fn main() : i32 {
			const f : fn(i32) : i32 = add_one;
			return apply(f, 41);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	// add_one is only ever reached through LS_OP_CALL_INDIRECT here (apply
	// calls its parameter f, a function value): a breakpoint inside it must
	// still suspend, not just run through, once CALL_INDIRECT no longer
	// recurses through C.
	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)LS_DEBUG_PAUSE_BREAKPOINT, (int)event.reason);
	EXPECT_EQ(4u, event.location.line);

	EXPECT_EQ(3u, ls_debug_stack_depth(runtime));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 0), toLs("add_one")));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 1), toLs("apply")));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 2), toLs("main")));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(ScratchStepActionLeaksAfterCompletion) {
	const char* source = R"(
		fn main() : i32 {
			var a : i32 = 1;
			return a;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));
	// Step over "return a;" - runs to completion, never re-suspends.
	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_STEP_OVER));

	// Remove the breakpoint; a fresh call must now run to completion,
	// not spuriously suspend with a stale armed step.
	ls_debug_remove_all_breakpoints(runtime.bytecode);
	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(ScratchAbortLeaksStackTop) {
	const char* source = R"(
		fn main() : i32 {
			var a : i32 = 5;
			return a;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));

	u8* const stack_top_before = runtime.runtime->stack_top;
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));
	EXPECT_EQ((int)LS_RESULT_FAILURE, (int)ls_debug_resume(runtime, LS_DEBUG_ABORT));
	// After aborting, the runtime should be back at its pre-call stack state.
	EXPECT_TRUE(stack_top_before == runtime.runtime->stack_top);

	CAPI_END(module);
	return true;
}

TEST(DebugAbortRestoresOuterCallAfterNativeReentry) {
	const char* source = R"(
		extern fn bridge(value : i32) : i32;

		fn helper(value : i32) : i32 {
			return value + 1;
		}

		fn main(value : i32) : i32 {
			var result = bridge(value);
			return result;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	auto bridge = [](ls_runtime* runtime, ls_call_frame frame) {
		LS_ARG(frame, i32, value);
		ls_push_i32(runtime, value);
		if (ls_call(runtime, toLs("helper")) != LS_RESULT_OK) return;
		LS_RESULT(frame, ls_to_i32(runtime, -1));
	};
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("bridge"), bridge) == LS_RESULT_OK);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 9u, nullptr));
	ls_push_i32(runtime, 41);
	u8* const stack_top_before = runtime.runtime->stack_top;
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));
	EXPECT_EQ((int)LS_RESULT_FAILURE, (int)ls_debug_resume(runtime, LS_DEBUG_ABORT));
	EXPECT_TRUE(stack_top_before == runtime.runtime->stack_top);
	EXPECT_EQ(41, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugContinuingRuntimeErrorReexecutesFailingInstruction) {
	const char* source = R"(
		fn divide(a : i32, b : i32) : i32 {
			return a / b;
		}

		fn main() : i32 {
			return divide(10, 0);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)LS_DEBUG_PAUSE_ERROR, (int)event.reason);

	// Continuing an error pause must retry the failed instruction. It must not
	// skip past it and return a value from an uninitialized destination slot.
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)LS_DEBUG_PAUSE_ERROR, (int)event.reason);

	CAPI_END(module);
	return true;
}

TEST(DebugStepIntoStopsInRecursiveCallOnSameSourceLine) {
	const char* source = R"(
		fn recurse(n : i32) : i32 { if n == 0 { return 0; } return recurse(n - 1); }
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 2u, nullptr));
	ls_push_i32(runtime, 1);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("recurse")));

	// A different invocation is a different source location even when both
	// statements share a physical line; step-into must stop in that callee.
	ls_debug_remove_all_breakpoints(runtime.bytecode);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_STEP_INTO));
	EXPECT_EQ(2u, ls_debug_stack_depth(runtime));

	CAPI_END(module);
	return true;
}

TEST(DebugLiteralReturnFunctionExposesParameters) {
	const char* source = R"(
		fn always_true(value : i32) : bool {
			return true;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	const ls_result breakpoint_result = ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 3u, nullptr);
	EXPECT_EQ((int)LS_RESULT_OK, (int)breakpoint_result);
	if (breakpoint_result != LS_RESULT_OK) {
		CAPI_END(module);
		return false;
	}
	ls_push_i32(runtime, 42);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("always_true")));

	EXPECT_EQ(1u, ls_debug_frame_local_count(runtime, 0));
	EXPECT_TRUE(equalStrings(ls_debug_local_name(runtime, 0, 0), toLs("value")));
	u32 size = 0u;
	const void* value = ls_debug_local_value(runtime, 0, 0, &size);
	EXPECT_TRUE(value != nullptr);
	EXPECT_EQ(4u, size);
	i32 int_value = 0;
	memcpy(&int_value, value, sizeof(int_value));
	EXPECT_EQ(42, int_value);

	CAPI_END(module);
	return true;
}

TEST(DebugStructFieldMetadata) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}
		fn main() : i32 {
			var v : Vec2 = Vec2 { 10, 20 };
			return v.x + v.y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	// Break at the return statement so v is in scope and initialized
	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 8u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	EXPECT_EQ(1u, ls_debug_frame_local_count(runtime, 0));
	EXPECT_TRUE(equalStrings(ls_debug_local_name(runtime, 0, 0), toLs("v")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(type));
	EXPECT_EQ(8u, ls_type_get_size(type));

	EXPECT_EQ(2u, ls_type_struct_field_count(type));

	const ls_string_view f0_name = ls_type_struct_field_name(type, 0);
	EXPECT_TRUE(equalStrings(f0_name, toLs("x")));
	EXPECT_EQ(0u, ls_type_struct_field_offset(type, 0));
	const ls_type* f0_type = ls_type_struct_field_type(type, 0);
	EXPECT_TRUE(f0_type != nullptr);
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(f0_type));
	EXPECT_EQ(4u, ls_type_get_size(f0_type));

	const ls_string_view f1_name = ls_type_struct_field_name(type, 1);
	EXPECT_TRUE(equalStrings(f1_name, toLs("y")));
	EXPECT_EQ(4u, ls_type_struct_field_offset(type, 1));
	const ls_type* f1_type = ls_type_struct_field_type(type, 1);
	EXPECT_TRUE(f1_type != nullptr);
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(f1_type));
	EXPECT_EQ(4u, ls_type_get_size(f1_type));

	// Out-of-bounds field index
	EXPECT_EQ(0u, size(ls_type_struct_field_name(type, 99)));
	EXPECT_TRUE(ls_type_struct_field_type(type, 99) == nullptr);
	EXPECT_EQ(0u, ls_type_struct_field_offset(type, 99));

	// Null pointer safety
	EXPECT_EQ(0, (int)ls_type_get_kind(nullptr));
	EXPECT_EQ(0u, ls_type_get_size(nullptr));
	EXPECT_EQ(0u, ls_type_struct_field_count(nullptr));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugStructReadFieldValue) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}
		fn main() : i32 {
			var v : Vec2 = Vec2 { 10, 20 };
			return v.x + v.y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 8u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ(8u, ls_type_get_size(type));
	EXPECT_EQ(2u, ls_type_struct_field_count(type));

	u32 value_size = 0;
	const void* value = ls_debug_local_value(runtime, 0, 0, &value_size);
	EXPECT_TRUE(value != nullptr);
	EXPECT_EQ(8u, value_size);

	// Read field via offset
	const u32 x_offset = ls_type_struct_field_offset(type, 0);
	i32 x_val = 0;
	memcpy(&x_val, (const u8*)value + x_offset, sizeof(x_val));
	EXPECT_EQ(10, x_val);

	const u32 y_offset = ls_type_struct_field_offset(type, 1);
	i32 y_val = 0;
	memcpy(&y_val, (const u8*)value + y_offset, sizeof(y_val));
	EXPECT_EQ(20, y_val);

	// Verify offset and size of x and y match their types
	const ls_type* x_type = ls_type_struct_field_type(type, 0);
	EXPECT_EQ(4u, ls_type_get_size(x_type));
	const ls_type* y_type = ls_type_struct_field_type(type, 1);
	EXPECT_EQ(4u, ls_type_get_size(y_type));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugNestedStructFieldType) {
	const char* source = R"(
		struct Inner {
			a : i32;
			b : i32;
		}
		struct Outer {
			inner : Inner;
			sum : i32;
		}
		fn main() : i32 {
			var o : Outer = undefined;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 12u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(type));
	EXPECT_EQ(12u, ls_type_get_size(type));
	EXPECT_EQ(2u, ls_type_struct_field_count(type));

	// Field 0: inner
	const ls_string_view f0_name = ls_type_struct_field_name(type, 0);
	EXPECT_TRUE(equalStrings(f0_name, toLs("inner")));
	EXPECT_EQ(0u, ls_type_struct_field_offset(type, 0));

	const ls_type* inner_type = ls_type_struct_field_type(type, 0);
	EXPECT_TRUE(inner_type != nullptr);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(inner_type));
	EXPECT_EQ(8u, ls_type_get_size(inner_type));
	EXPECT_EQ(2u, ls_type_struct_field_count(inner_type));

	EXPECT_TRUE(equalStrings(ls_type_struct_field_name(inner_type, 0), toLs("a")));
	EXPECT_EQ(0u, ls_type_struct_field_offset(inner_type, 0));
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(ls_type_struct_field_type(inner_type, 0)));

	EXPECT_TRUE(equalStrings(ls_type_struct_field_name(inner_type, 1), toLs("b")));
	EXPECT_EQ(4u, ls_type_struct_field_offset(inner_type, 1));
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(ls_type_struct_field_type(inner_type, 1)));

	// Field 1: sum
	const ls_string_view f1_name = ls_type_struct_field_name(type, 1);
	EXPECT_TRUE(equalStrings(f1_name, toLs("sum")));
	EXPECT_EQ(8u, ls_type_struct_field_offset(type, 1));
	const ls_type* sum_type = ls_type_struct_field_type(type, 1);
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(sum_type));
	EXPECT_EQ(4u, ls_type_get_size(sum_type));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));

	CAPI_END(module);
	return true;
}

TEST(DebugStructWithArrayField) {
	const char* source = R"(
		struct WithArray {
			data : [3]i32;
			label : i32;
		}
		fn main() : i32 {
			var w : WithArray = undefined;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 8u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(type));
	EXPECT_EQ(16u, ls_type_get_size(type));
	EXPECT_EQ(2u, ls_type_struct_field_count(type));

	// Field 0: data
	EXPECT_TRUE(equalStrings(ls_type_struct_field_name(type, 0), toLs("data")));
	EXPECT_EQ(0u, ls_type_struct_field_offset(type, 0));

	const ls_type* arr_type = ls_type_struct_field_type(type, 0);
	EXPECT_TRUE(arr_type != nullptr);
	EXPECT_EQ((int)LS_TYPE_ARRAY, (int)ls_type_get_kind(arr_type));
	EXPECT_EQ(12u, ls_type_get_size(arr_type));
	EXPECT_EQ(3u, ls_type_array_length(arr_type));

	const ls_type* elem_type = ls_type_array_element_type(arr_type);
	EXPECT_TRUE(elem_type != nullptr);
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(elem_type));
	EXPECT_EQ(4u, ls_type_get_size(elem_type));

	// Field 1: label
	EXPECT_TRUE(equalStrings(ls_type_struct_field_name(type, 1), toLs("label")));
	EXPECT_EQ(12u, ls_type_struct_field_offset(type, 1));
	const ls_type* label_type = ls_type_struct_field_type(type, 1);
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(label_type));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));

	CAPI_END(module);
	return true;
}

TEST(DebugStructSliceField) {
	const char* source = R"(
		struct WithSlice {
			items : []i32;
			count : i32;
		}
		fn main() : i32 {
			var w : WithSlice = undefined;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 8u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(type));
	EXPECT_EQ(20u, ls_type_get_size(type));
	EXPECT_EQ(2u, ls_type_struct_field_count(type));

	// Iterate fields
	bool found_items = false;
	bool found_count = false;
	for (u32 i = 0, field_count = ls_type_struct_field_count(type); i < field_count; ++i) {
		ls_string_view fname = ls_type_struct_field_name(type, i);
		if (equalStrings(fname, toLs("items"))) {
			found_items = true;
			EXPECT_EQ(0u, ls_type_struct_field_offset(type, i));
			const ls_type* slice_type = ls_type_struct_field_type(type, i);
			EXPECT_TRUE(slice_type != nullptr);
			EXPECT_EQ((int)LS_TYPE_SLICE, (int)ls_type_get_kind(slice_type));
			EXPECT_EQ(16u, ls_type_get_size(slice_type));
			EXPECT_EQ(0u, ls_type_array_length(slice_type));

			const ls_type* sl_elem = ls_type_array_element_type(slice_type);
			EXPECT_TRUE(sl_elem != nullptr);
			EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(sl_elem));
			EXPECT_EQ(4u, ls_type_get_size(sl_elem));
		}
		if (equalStrings(fname, toLs("count"))) {
			found_count = true;
		}
	}
	EXPECT_TRUE(found_items);
	EXPECT_TRUE(found_count);

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));

	CAPI_END(module);
	return true;
}

TEST(DebugStructGlobalInspection) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}
		var g : Vec2 = Vec2 { 100, 200 };
		fn main() : i32 {
			return g.x + g.y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 8u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	EXPECT_TRUE(ls_debug_global_count(runtime) >= 1);

	bool found_g = false;
	for (u32 i = 0, count = ls_debug_global_count(runtime); i < count; ++i) {
		if (equalStrings(ls_debug_global_name(runtime, i), toLs("g"))) {
			found_g = true;

			EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_debug_global_kind(runtime, i));

			const ls_type* g_type = ls_debug_global_type(runtime, i);
			EXPECT_TRUE(g_type != nullptr);
			EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(g_type));
			EXPECT_EQ(8u, ls_type_get_size(g_type));
			EXPECT_EQ(2u, ls_type_struct_field_count(g_type));

			EXPECT_TRUE(equalStrings(ls_type_struct_field_name(g_type, 0), toLs("x")));
			EXPECT_EQ(0u, ls_type_struct_field_offset(g_type, 0));
			EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(ls_type_struct_field_type(g_type, 0)));

			EXPECT_TRUE(equalStrings(ls_type_struct_field_name(g_type, 1), toLs("y")));
			EXPECT_EQ(4u, ls_type_struct_field_offset(g_type, 1));
			EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(ls_type_struct_field_type(g_type, 1)));

			u32 value_size = 0;
			const void* value = ls_debug_global_value(runtime, i, &value_size);
			EXPECT_TRUE(value != nullptr);
			EXPECT_EQ(8u, value_size);

			i32 x_val = 0;
			memcpy(&x_val, (const u8*)value, sizeof(x_val));
			EXPECT_EQ(100, x_val);

			i32 y_val = 0;
			memcpy(&y_val, (const u8*)value + 4u, sizeof(y_val));
			EXPECT_EQ(200, y_val);
		}
	}
	EXPECT_TRUE(found_g);

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(300, ls_to_i32(runtime, -1));

	CAPI_END(module);
	return true;
}

TEST(DebugNullableTypeIntrospection) {
	const char* source = R"(
		fn main() : i32 {
			var null_val : ?i32 = null;
			var some_val : ?i32 = 42;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 5u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	// Find the two locals
	int null_idx = -1, some_idx = -1;
	for (u32 i = 0, count = ls_debug_frame_local_count(runtime, 0); i < count; ++i) {
		const ls_string_view local_name = ls_debug_local_name(runtime, 0, i);
		if (equalStrings(local_name, toLs("null_val"))) null_idx = (int)i;
		if (equalStrings(local_name, toLs("some_val"))) some_idx = (int)i;
	}
	EXPECT_TRUE(null_idx >= 0);
	EXPECT_TRUE(some_idx >= 0);

	// Check null_val (should be null)
	{
		const ls_type* type = ls_debug_local_type(runtime, 0, (u32)null_idx);
		EXPECT_TRUE(type != nullptr);
		EXPECT_EQ((int)LS_TYPE_NULLABLE, (int)ls_type_get_kind(type));
		EXPECT_EQ(5u, ls_type_get_size(type));  // 1 (flag) + 4 (i32)

		const ls_type* inner = ls_type_nullable_inner_type(type);
		EXPECT_TRUE(inner != nullptr);
		EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(inner));
		EXPECT_EQ(4u, ls_type_get_size(inner));

		u32 value_size = 0;
		const void* value = ls_debug_local_value(runtime, 0, (u32)null_idx, &value_size);
		EXPECT_TRUE(value != nullptr);
		EXPECT_EQ(5u, value_size);
		EXPECT_TRUE(ls_type_nullable_is_null(type, value));
	}

	// Check some_val (should be non-null with value 42)
	{
		const ls_type* type = ls_debug_local_type(runtime, 0, (u32)some_idx);
		EXPECT_TRUE(type != nullptr);
		EXPECT_EQ((int)LS_TYPE_NULLABLE, (int)ls_type_get_kind(type));
		EXPECT_EQ(5u, ls_type_get_size(type));

		u32 value_size = 0;
		const void* value = ls_debug_local_value(runtime, 0, (u32)some_idx, &value_size);
		EXPECT_TRUE(value != nullptr);
		EXPECT_EQ(5u, value_size);
		EXPECT_TRUE(!ls_type_nullable_is_null(type, value));

		const ls_type* inner = ls_type_nullable_inner_type(type);
		EXPECT_TRUE(inner != nullptr);
		EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(inner));

		const void* inner_value = ls_type_nullable_value_ptr(type, value);
		EXPECT_TRUE(inner_value != nullptr);
		i32 v = 0;
		memcpy(&v, inner_value, sizeof(v));
		EXPECT_EQ(42, v);
	}

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));

	CAPI_END(module);
	return true;
}

TEST(DebugTaggedUnionMemberCountAndTypes) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B | i32;

		fn main() : i32 {
			var u : Union = A { 7 };
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 12u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_TAGGED_UNION, (int)ls_type_get_kind(type));

	// Union = A | B | i32  => 3 members, size = 4 (tag) + 4 (max payload) = 8
	EXPECT_EQ(8u, ls_type_get_size(type));
	EXPECT_EQ(3u, ls_type_union_member_count(type));

	i32 struct_count = 0;
	i32 i32_count = 0;
	for (u32 i = 0; i < 3; ++i) {
		const ls_type* member = ls_type_union_member_type(type, i);
		EXPECT_TRUE(member != nullptr);
		EXPECT_EQ(4u, ls_type_get_size(member));
		if (ls_type_get_kind(member) == LS_TYPE_STRUCT) ++struct_count;
		if (ls_type_get_kind(member) == LS_TYPE_I32) ++i32_count;
	}
	EXPECT_EQ(2, struct_count);
	EXPECT_EQ(1, i32_count);

	// Out-of-bounds and null safety
	EXPECT_TRUE(ls_type_union_member_type(type, 99) == nullptr);
	EXPECT_EQ(0u, ls_type_union_member_count(nullptr));
	EXPECT_TRUE(ls_type_union_member_type(nullptr, 0) == nullptr);
	EXPECT_EQ(-1, ls_type_union_tag(nullptr, nullptr));

	// Read the tag from the value bytes
	u32 value_size = 0;
	const void* value = ls_debug_local_value(runtime, 0, 0, &value_size);
	EXPECT_TRUE(value != nullptr);
	const i32 tag = ls_type_union_tag(type, value);
	EXPECT_TRUE(tag >= 0 && tag < 3);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(ls_type_union_member_type(type, (u32)tag)));
	EXPECT_EQ(-1, ls_type_union_tag(type, nullptr));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));

	CAPI_END(module);
	return true;
}

TEST(DebugTaggedUnionTagChangesWithMember) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B | i32;

		fn main() : i32 {
			var u : Union = A { 7 };
			u = B { 3.14 };
			u = 42;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 14u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_TAGGED_UNION, (int)ls_type_get_kind(type));
	EXPECT_EQ(3u, ls_type_union_member_count(type));

	// After assigning i32(42), the tag should select the i32 member.
	u32 value_size = 0;
	const void* value = ls_debug_local_value(runtime, 0, 0, &value_size);
	EXPECT_TRUE(value != nullptr);
	const i32 tag = ls_type_union_tag(type, value);
	EXPECT_TRUE(tag >= 0 && tag < 3);
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(ls_type_union_member_type(type, (u32)tag)));

	// Verify the payload holds the i32 value 42
	i32 payload = 0;
	memcpy(&payload, (const u8*)value + 4, sizeof(payload));
	EXPECT_EQ(42, payload);

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));

	CAPI_END(module);
	return true;
}

TEST(TypeGetNameStruct) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn main() : i32 {
			var v : Vec2 = { 10, 20 };
			return v.x + v.y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 9u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(type));
	EXPECT_TRUE(equalStrings(ls_type_get_name(type), toLs("Vec2")));

	// Null pointer safety
	EXPECT_EQ(0u, size(ls_type_get_name(nullptr)));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TypeGetNameEnum) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}

		fn main() : i32 {
			var s : State = .Running;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 9u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_ENUM, (int)ls_type_get_kind(type));
	EXPECT_TRUE(equalStrings(ls_type_get_name(type), toLs("State")));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	CAPI_END(module);
	return true;
}

TEST(TypeEnumValueIntrospectionImplicit) {
	const char* source = R"(
		enum Color {
			Red,
			Green,
			Blue
		}

		fn main() : i32 {
			var c : Color = .Red;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 10u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_ENUM, (int)ls_type_get_kind(type));

	EXPECT_EQ(3u, ls_type_enum_value_count(type));
	EXPECT_TRUE(equalStrings(ls_type_enum_value_name(type, 0), toLs("Red")));
	EXPECT_EQ(0, ls_type_enum_value_value(type, 0));
	EXPECT_TRUE(equalStrings(ls_type_enum_value_name(type, 1), toLs("Green")));
	EXPECT_EQ(1, ls_type_enum_value_value(type, 1));
	EXPECT_TRUE(equalStrings(ls_type_enum_value_name(type, 2), toLs("Blue")));
	EXPECT_EQ(2, ls_type_enum_value_value(type, 2));

	// Out-of-bounds safety
	EXPECT_EQ(0u, size(ls_type_enum_value_name(type, 99)));
	EXPECT_EQ(0, ls_type_enum_value_value(type, 99));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	CAPI_END(module);
	return true;
}

TEST(TypeEnumValueIntrospectionExplicit) {
	const char* source = R"(
		enum Status {
			Idle = 10,
			Running = 20,
			Error = 30
		}

		fn main() : i32 {
			var s : Status = .Idle;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 10u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_ENUM, (int)ls_type_get_kind(type));

	EXPECT_EQ(3u, ls_type_enum_value_count(type));
	EXPECT_TRUE(equalStrings(ls_type_enum_value_name(type, 0), toLs("Idle")));
	EXPECT_EQ(10, ls_type_enum_value_value(type, 0));
	EXPECT_TRUE(equalStrings(ls_type_enum_value_name(type, 1), toLs("Running")));
	EXPECT_EQ(20, ls_type_enum_value_value(type, 1));
	EXPECT_TRUE(equalStrings(ls_type_enum_value_name(type, 2), toLs("Error")));
	EXPECT_EQ(30, ls_type_enum_value_value(type, 2));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	CAPI_END(module);
	return true;
}

TEST(TypeEnumValueIntrospectionMixed) {
	const char* source = R"(
		enum E {
			A,
			B = 5,
			C,
			D = 10,
			E
		}

		fn main() : i32 {
			var e : E = .A;
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 12u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_ENUM, (int)ls_type_get_kind(type));

	EXPECT_EQ(5u, ls_type_enum_value_count(type));
	EXPECT_EQ(0, ls_type_enum_value_value(type, 0));
	EXPECT_EQ(5, ls_type_enum_value_value(type, 1));
	EXPECT_EQ(6, ls_type_enum_value_value(type, 2));
	EXPECT_EQ(10, ls_type_enum_value_value(type, 3));
	EXPECT_EQ(11, ls_type_enum_value_value(type, 4));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	CAPI_END(module);
	return true;
}

TEST(TypeEnumValueIntrospectionNullSafety) {
	EXPECT_EQ(0u, ls_type_enum_value_count(nullptr));
	EXPECT_EQ(0u, size(ls_type_enum_value_name(nullptr, 0)));
	EXPECT_EQ(0, ls_type_enum_value_value(nullptr, 0));
	return true;
}

TEST(TypeGetNameNestedStruct) {
	const char* source = R"(
		struct Inner {
			a : i32;
			b : i32;
		}

		struct Outer {
			inner : Inner;
			sum : i32;
		}

		fn main() : i32 {
			var v : Outer = { { 1, 2 }, 3 };
			return v.sum;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 14u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* outer = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(outer != nullptr);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(outer));
	EXPECT_TRUE(equalStrings(ls_type_get_name(outer), toLs("Outer")));

	const ls_type* inner = ls_type_struct_field_type(outer, 0);
	EXPECT_TRUE(inner != nullptr);
	EXPECT_EQ((int)LS_TYPE_STRUCT, (int)ls_type_get_kind(inner));
	EXPECT_TRUE(equalStrings(ls_type_get_name(inner), toLs("Inner")));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TypeGetNamePrimitiveReturnsEmpty) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 42;
			return x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(ls_debug_set_breakpoint(runtime.bytecode, makeStringView(__func__), 4u, nullptr));
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	const ls_type* type = ls_debug_local_type(runtime, 0, 0);
	EXPECT_TRUE(type != nullptr);
	EXPECT_EQ((int)LS_TYPE_I32, (int)ls_type_get_kind(type));
	// Primitive types have no name
	EXPECT_EQ(0u, size(ls_type_get_name(type)));

	EXPECT_EQ((int)LS_RESULT_OK, (int)ls_debug_resume(runtime, LS_DEBUG_CONTINUE));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
