TEST(YieldExpressionReceivesExactTypedValue) {
	const char* source = R"(
		fn main() : i32 {
			const value : i32 = yield;
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	const ex_type* type = ex_primitive_type_from_kind(runtime.runtime, EX_TYPE_I32);
	EXPECT_TRUE(type != nullptr);
	const i32 value = 42;
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime.runtime, type, &value, sizeof(value)));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(YieldExpressionCanExpectDifferentTypesAtEachSuspension) {
	const char* source = R"(
		fn main() : i32 {
			const first : i32 = yield;
			const second : f32 = yield;
			return first;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	const i32 first = 42;
	const ex_type* i32_type = ex_primitive_type_from_kind(runtime.runtime, EX_TYPE_I32);
	EXPECT_TRUE(i32_type != nullptr);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_task_resume(runtime.runtime, i32_type, &first, sizeof(first)));
	const f32 second = 2.5f;
	const ex_type* f32_type = ex_primitive_type_from_kind(runtime.runtime, EX_TYPE_F32);
	EXPECT_TRUE(f32_type != nullptr);
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime.runtime, f32_type, &second, sizeof(second)));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(YieldExpressionRejectsWrongType) {
	const char* source = R"(
		fn other(value : i64) : i64 { return value; }
		fn main() : i32 {
			const value : i32 = yield;
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	const ex_type* type = ex_primitive_type_from_kind(runtime.runtime, EX_TYPE_I64);
	EXPECT_TRUE(type != nullptr);
	const i64 value = 42;
	EXPECT_EQ(EX_CALL_RESULT_INVALID_YIELD_VALUE, ex_task_resume(runtime.runtime, type, &value, sizeof(value)));
	CAPI_END(module);
	return true;
}

TEST(YieldExpressionRequiresAValue) {
	const char* source = R"(
		fn main() : i32 {
			const value : i32 = yield;
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_EQ(EX_CALL_RESULT_INVALID_YIELD_VALUE, ex_task_resume(runtime.runtime, nullptr, nullptr, 0));
	CAPI_END(module);
	return true;
}

TEST(YieldExpressionNeedsExplicitType) {
	const char* source = R"(
		fn main() : i32 {
			const value = yield;
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(YieldExpressionCannotUseUntypedNumericContext) {
	const char* source = R"(
		fn main() : i32 {
			const value = yield + 1;
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(YieldExpressionCannotBeUsedInNumericExpression) {
	const char* source = R"(
		fn main() : i32 {
			const value : i32 = yield + 1;
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(YieldSuspendsAndResumes) {
	const char* source = R"(
		var state : i32 = 0;
		fn main() : i32 {
			state = 1;
			yield;
			state = 2;
			return state;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_EQ(EX_RESULT_FAILURE, ex_debug_pause_event(runtime, nullptr)); // invalid output is rejected
	// The event itself is available and identifies a language-level yield.
	ex_debug_event event = {};
	EXPECT_EQ(EX_RESULT_OK, ex_debug_pause_event(runtime, &event));
	EXPECT_EQ(EX_DEBUG_PAUSE_YIELD, event.reason);
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(2, ex_task_to_i32(runtime, -1));
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);
	CAPI_END(module);
	return true;
}

TEST(MultipleTasksCanBeCooperativelyInterleaved) {
	const char* source = R"(
		fn worker(seed : i32) : i32 {
			var value : i32 = seed;
			yield;
			value += 1;
			yield;
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);

	ex_task* task_a = ex_task_create(runtime.runtime);
	ex_task* task_b = ex_task_create(runtime.runtime);
	EXPECT_TRUE(task_a != nullptr);
	EXPECT_TRUE(task_b != nullptr);
	if (!task_a || !task_b) {
		ex_task_destroy(task_a);
		ex_task_destroy(task_b);
		CAPI_END(module);
		return false;
	}

	const i32 seed_a = 10;
	const i32 seed_b = 20;
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_call(task_a, toLs("worker"), &seed_a, sizeof(seed_a)));
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_call(task_b, toLs("worker"), &seed_b, sizeof(seed_b)));

	// Advancing one task must not affect the other task's suspension.
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_task_resume(task_a, nullptr, nullptr, 0));
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_task_resume(task_b, nullptr, nullptr, 0));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(task_a, nullptr, nullptr, 0));
	EXPECT_EQ(11, ex_task_to_i32(task_a, -1));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(task_b, nullptr, nullptr, 0));
	EXPECT_EQ(21, ex_task_to_i32(task_b, -1));

	ex_task_destroy(task_a);
	ex_task_destroy(task_b);
	CAPI_END(module);
	return true;
}

TEST(MultipleTasksShareGlobalsWhileInterleaving) {
	const char* source = R"(
		var counter : i32 = 0;

		fn worker(step : i32) : i32 {
			counter += step;
			yield;
			counter += step;
			yield;
			return counter;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);

	ex_task* task_a = ex_task_create(runtime.runtime);
	ex_task* task_b = ex_task_create(runtime.runtime);
	EXPECT_TRUE(task_a != nullptr);
	EXPECT_TRUE(task_b != nullptr);
	if (!task_a || !task_b) {
		ex_task_destroy(task_a);
		ex_task_destroy(task_b);
		CAPI_END(module);
		return false;
	}

	const i32 step_a = 1;
	const i32 step_b = 10;
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_call(task_a, toLs("worker"), &step_a, sizeof(step_a)));
	EXPECT_EQ(1, test_global_i32(runtime.runtime, 0));
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_call(task_b, toLs("worker"), &step_b, sizeof(step_b)));
	EXPECT_EQ(11, test_global_i32(runtime.runtime, 0));

	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_task_resume(task_a, nullptr, nullptr, 0));
	EXPECT_EQ(12, test_global_i32(runtime.runtime, 0));
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_task_resume(task_b, nullptr, nullptr, 0));
	EXPECT_EQ(22, test_global_i32(runtime.runtime, 0));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(task_a, nullptr, nullptr, 0));
	EXPECT_EQ(22, ex_task_to_i32(task_a, -1));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(task_b, nullptr, nullptr, 0));
	EXPECT_EQ(22, ex_task_to_i32(task_b, -1));
	EXPECT_EQ(22, test_global_i32(runtime.runtime, 0));

	ex_task_destroy(task_a);
	ex_task_destroy(task_b);
	CAPI_END(module);
	return true;
}

TEST(YieldCanBeReachedMultipleTimes) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 0;
			value += 1;
			yield;
			value += 10;
			yield;
			return value + 100;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(111, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(YieldPreservesLocalsAndCallFrames) {
	const char* source = R"(
		fn child(value : i32) : i32 {
			var local : i32 = value + 1;
			yield;
			return local + 1;
		}

		fn main() : i32 {
			var before : i32 = 40;
			var result : i32 = child(before);
			return result + 1;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(43, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(YieldDoesNotRunDeferUntilFunctionReturns) {
	const char* source = R"(
		var cleanup_count : i32 = 0;
		fn cleanup() : void { cleanup_count += 1; }
		fn main() : i32 {
			defer cleanup();
			yield;
			return cleanup_count;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(0, ex_task_to_i32(runtime, -1));
	// The deferred call runs after the return value has been produced.
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(1, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(YieldInLoopCanDriveFrameByFrameWork) {
	const char* source = R"(
		var ticks : i32 = 0;
		fn main() : i32 {
			while ticks < 3 {
				ticks += 1;
				yield;
			}
			return ticks;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(3, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(YieldInConditionalBranch) {
	const char* source = R"(
		fn main(value : bool) : i32 {
			if value { yield; }
			return 7;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	test_push_bool(runtime, 1);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(7, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(YieldInDeferFails) {
	const char* source = R"(
		fn main() : void {
			defer yield;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(YieldRequiresSemicolon) {
	const char* source = R"(
		fn main() : void {
			yield
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ResumeWithoutYieldFails) {
	const char* source = R"(
		fn main() : i32 { return 42; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_NOT_SUSPENDED, ex_task_resume(runtime, nullptr, nullptr, 0));
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	EXPECT_EQ(EX_CALL_RESULT_NOT_SUSPENDED, ex_task_resume(runtime, nullptr, nullptr, 0));
	CAPI_END(module);
	return true;
}

TEST(CannotCallWhileYielded) {
	const char* source = R"(
		fn main() : void { yield; }
		fn other() : i32 { return 9; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	RuntimeGuard runtime(module, &module_host);
	EXPECT_TRUE(runtime);
	EXPECT_EQ(EX_CALL_RESULT_SUSPENDED, test_call(runtime, toLs("main")));
	EXPECT_EQ(EX_CALL_RESULT_INVALID_STATE, test_call(runtime, toLs("other")));
	EXPECT_EQ(EX_CALL_RESULT_OK, ex_task_resume(runtime, nullptr, nullptr, 0));
	CAPI_END(module);
	return true;
}
