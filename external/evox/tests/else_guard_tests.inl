TEST(ElseGuardNullableReturnValueRuntime) {
	const char* source = R"(
		fn maybe(present : bool) : ?i32 {
			if present { return 17; }
			return null;
		}
		fn unwrap(present : bool) : i32 {
			var value = maybe(present) else return 91;
			return value;
		}
		fn main() : i32 { return unwrap(true) * 100 + unwrap(false); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(1791, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardNullableBlockReturnRuntime) {
	const char* source = R"(
		var guard_calls : i32 = 0;
		fn maybe(present : bool) : ?i32 {
			if present { return 8; }
			return null;
		}
		fn unwrap(present : bool) : i32 {
			var value = maybe(present) else {
				guard_calls += 1;
				return 30;
			};
			return value;
		}
		fn main() : i32 {
			const a = unwrap(true);
			const before = guard_calls;
			const b = unwrap(false);
			return a * 1000 + before * 100 + b * 10 + guard_calls;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(8301, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardNullablePanicSkippedForValue) {
	const char* source = R"(
		fn maybe() : ?i32 { return 42; }
		fn main() : i32 {
			var value = maybe() else panic("missing");
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardNullablePanicTakenForNull) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn main() : i32 {
			var value = maybe() else panic("missing");
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_PANIC, test_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardNullableContinueRuntime) {
	const char* source = R"(
		fn even_value(value : i32) : ?i32 {
			if value % 2 == 0 { return value; }
			return null;
		}
		fn main() : i32 {
			var sum : i32 = 0;
			for i in 0..7 {
				var value = even_value(i) else continue;
				sum += value;
			}
			return sum;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(12, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardNullableBreakRuntime) {
	const char* source = R"(
		fn before_limit(value : i32) : ?i32 {
			if value < 4 { return value; }
			return null;
		}
		fn main() : i32 {
			var sum : i32 = 0;
			var i : i32 = 0;
			while i < 10 {
				var value = before_limit(i) else break;
				sum += value;
				i += 1;
			}
			return sum * 10 + i;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(64, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardLabeledBreakRuntime) {
	const char* source = R"(
		fn missing() : ?i32 { return null; }
		fn main() : i32 {
			var visits : i32 = 0;
			outer: for i in 0..3 {
				for j in 0..3 {
					visits += 1;
					var value = missing() else break outer;
					visits += value;
				}
			}
			return visits;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(1, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardLabeledContinueRuntime) {
	const char* source = R"(
		fn maybe(present : bool) : ?i32 {
			if present { return 1; }
			return null;
		}
		fn main() : i32 {
			var visits : i32 = 0;
			outer: for i in 0..3 {
				for j in 0..3 {
					visits += 1;
					var value = maybe(j != 0) else continue outer;
					visits += value;
				}
			}
			return visits;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(3, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardTerminatingIfBlockCompiles) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn unwrap(flag : bool) : i32 {
			var value = maybe() else {
				if flag { return 10; } else { return 20; }
			};
			return value;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ElseGuardTerminatingNestedBlockCompiles) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn unwrap() : i32 {
			var value = maybe() else { { { return 7; } } };
			return value;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ElseGuardInitializerEvaluatedExactlyOnce) {
	const char* source = R"(
		var calls : i32 = 0;
		fn maybe(present : bool) : ?i32 {
			calls += 1;
			if present { return 6; }
			return null;
		}
		fn take(present : bool) : i32 {
			var value = maybe(present) else return 4;
			return value;
		}
		fn main() : i32 {
			const a = take(true);
			const b = take(false);
			return a * 100 + b * 10 + calls;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(642, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardRunsDefersOnReturnValue) {
	const char* source = R"(
		var cleaned : i32 = 0;
		fn cleanup() : void { cleaned += 1; }
		fn maybe() : ?i32 { return null; }
		fn take() : i32 {
			defer cleanup();
			var value = maybe() else return 5;
			return value;
		}
		fn main() : i32 { return take() * 10 + cleaned; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(51, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardNullablePointerPayloadRuntime) {
	const char* source = R"(
		fn choose(ptr : *i32, present : bool) : ?*i32 {
			if present { return ptr; }
			return null;
		}
		fn set(ptr : *i32, present : bool) : bool {
			var required = choose(ptr, present) else return false;
			required.* = 73;
			return true;
		}
		fn main() : i32 {
			var value : i32 = 1;
			const absent = set(&value, false);
			const present = set(&value, true);
			if absent or not present { return -1; }
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(73, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardUnionExplicitReturnValueRuntime) {
	const char* source = R"(
		struct A { value : i32; }
		struct B { error : i32; }
		fn choose(ok : bool) : A | B {
			if ok { return A { 12 }; }
			return B { 9 };
		}
		fn take(ok : bool) : i32 {
			var result : A = choose(ok) else return 40;
			return result.value;
		}
		fn main() : i32 { return take(true) * 100 + take(false); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(1240, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardUnionPanicRuntime) {
	const char* source = R"(
		struct Value { number : i32; }
		struct Error { code : i32; }
		fn choose(ok : bool) : Value | Error {
			if ok { return Value { 5 }; }
			return Error { 1 };
		}
		fn take(ok : bool) : i32 {
			var value : Value = choose(ok) else panic("error");
			return value.number;
		}
		fn main() : i32 { return take(false); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_PANIC, test_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardUnionSubunionWithBlockReturnRuntime) {
	const char* source = R"(
		struct A { value : i32; }
		struct B { value : i32; }
		struct C { value : i32; }
		fn take(input : A | B | C) : i32 {
			var handled : A | B = input else { return 99; };
			if handled is A { return handled.value; }
			return handled.value * 10;
		}
		fn main() : i32 {
			return take(A { 2 }) * 10000 + take(B { 3 }) * 100 + take(C { 4 });
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(23099, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ElseGuardRejectsNonTerminatingExpression) {
	const char* source = R"(
		fn note() : void {}
		fn maybe() : ?i32 { return null; }
		fn main() : void {
			var value = maybe() else note();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsEmptyBlock) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn main() : void {
			var value = maybe() else {};
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsPartiallyTerminatingIf) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn main(flag : bool) : i32 {
			var value = maybe() else {
				if flag { return 1; }
			};
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsIfWithNonTerminatingArm) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn main(flag : bool) : i32 {
			var value = maybe() else {
				if flag { return 1; } else { var x = 2; }
			};
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsWrongReturnValueType) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn main() : i32 {
			var value = maybe() else return "wrong";
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsBareReturnFromNonVoidNullableFunction) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn main() : i32 {
			var value = maybe() else return;
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsBreakOutsideLoop) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn main() : void {
			var value = maybe() else break;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsContinueOutsideLoop) {
	const char* source = R"(
		fn maybe() : ?i32 { return null; }
		fn main() : void {
			var value = maybe() else continue;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsNonNullableNonUnionInitializerWithPanic) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 4 else panic("impossible");
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsNullableWrongPayloadAnnotation) {
	const char* source = R"(
		fn maybe() : ?i32 { return 1; }
		fn main() : void {
			var value : i64 = maybe() else panic("missing");
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsWholeUnionTargetWithGeneralGuard) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { x : i32; }
		fn choose() : A | B { return A { 1 }; }
		fn main() : void {
			var value : A | B = choose() else panic("impossible");
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ElseGuardRejectsUnionTargetContainingForeignMember) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { x : i32; }
		struct C { x : i32; }
		fn choose() : A | B { return A { 1 }; }
		fn main() : void {
			var value : A | C = choose() else panic("not selected");
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
