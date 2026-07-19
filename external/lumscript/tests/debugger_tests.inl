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
	EXPECT_TRUE(!ls_call(runtime, toLs("main")));

	EXPECT_EQ(2u, ls_debug_stack_depth(runtime));

	const ls_string_view innermost_name = ls_debug_frame_function_name(runtime, 0);
	EXPECT_TRUE(equalStrings(innermost_name, toLs("divide")));

	ls_debug_location innermost_location;
	EXPECT_TRUE(ls_debug_frame_location(runtime, 0, &innermost_location));
	EXPECT_EQ(3u, innermost_location.line);

	const ls_string_view outer_name = ls_debug_frame_function_name(runtime, 1);
	EXPECT_TRUE(equalStrings(outer_name, toLs("main")));

	ls_debug_location outer_location;
	EXPECT_TRUE(ls_debug_frame_location(runtime, 1, &outer_location));
	EXPECT_EQ(7u, outer_location.line);

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

	ls_debug_enable(runtime, 1);
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

TEST(DebugBreakpointIgnoredWhenDebuggingDisabled) {
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
	// ls_debug_enable is never called, so debugging stays off (the default):
	// the patched LS_OP_BREAK byte must still run as if it were the original
	// instruction, transparently, with no suspension.
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(!ls_debug_is_suspended(runtime));
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
	ls_debug_enable(runtime, 1);

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

	ls_debug_enable(runtime, 1);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_debug_is_suspended(runtime));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)LS_DEBUG_PAUSE_ERROR, (int)event.reason);

	EXPECT_EQ(2u, ls_debug_stack_depth(runtime));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 0), toLs("divide")));
	EXPECT_TRUE(equalStrings(ls_debug_frame_function_name(runtime, 1), toLs("main")));

	const ls_result resume_result = ls_debug_resume(runtime, LS_DEBUG_ABORT);
	EXPECT_EQ((int)LS_RESULT_FAILURE, (int)resume_result);
	EXPECT_TRUE(!ls_debug_is_suspended(runtime));

	CAPI_END(module);
	return true;
}

TEST(DebugDisablingSuspendedRuntimeAbortsAndUnblocksCalls) {
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
	ls_debug_enable(runtime, 1);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	ls_debug_enable(runtime, 0);
	EXPECT_TRUE(!ls_debug_is_suspended(runtime));

	// A fresh call now succeeds; the breakpoint is still set but debugging is
	// off, so it doesn't trap again.
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
	ls_debug_enable(runtime, 1);
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
	ls_debug_enable(runtime, 1);
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
	ls_debug_enable(runtime, 1);
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
	ls_debug_enable(runtime, 1);
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
	ls_debug_enable(runtime, 1);
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
	ls_debug_enable(runtime, 1);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));

	ls_debug_event event;
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	EXPECT_EQ(4u, event.location.line);
	EXPECT_EQ(2u, ls_debug_stack_depth(runtime));

	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_debug_resume(runtime, LS_DEBUG_STEP_OUT));
	EXPECT_TRUE(ls_debug_pause_event(runtime, &event));
	// Stops at the call site (line 9, "var y = helper();"), immediately after
	// helper() returns into it — standard step-out convention (matches gdb's
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
	ls_debug_enable(runtime, 1);

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
	ls_debug_enable(runtime, 1);
	EXPECT_EQ((int)LS_RESULT_SUSPENDED, (int)ls_call(runtime, toLs("main")));
	// Step over "return a;" — runs to completion, never re-suspends.
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
	ls_debug_enable(runtime, 1);

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
	ls_debug_enable(runtime, 1);
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

	ls_debug_enable(runtime, 1);
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
	ls_debug_enable(runtime, 1);
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
	ls_debug_enable(runtime, 1);
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
