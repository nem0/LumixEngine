TEST(ImportEnumMemberWithoutAliasCompiles) {
	const char* main_source = R"(
		import "core:Keycode"

		fn main() : i32 {
			return Keycode.W as i32;
		}
	)";
	const char* keycode_source = R"(
		enum Keycode {
			W = 87
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("core:Keycode"), toLs(keycode_source) }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportedEnumShorthandWithoutHintCompiles) {
	const char* main_source = R"(
		import "state"

		fn main() : bool {
			const state : State = State.Running;
			return .Running == state;
		}
	)";
	const char* state_source = R"(
		enum State {
			Idle,
			Running
		}
	)";
	LumScriptImportFile file = { toLs("state"), toLs(state_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(EnumShorthandInComparison) {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		}
		fn check(state : State) : bool {
			return state == .Running or .Idle == state;
		}

		fn main() : void {
			var s : State = .Idle;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FirstParameterNamespaceResolutionEnum) {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		}

		fn test(s : State) : void {}

		fn main() : void {
			var s = State.Idle;
			s.test();
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(EnumShorthandInAssignment) {
	const char* source = R"(
		enum Priority {
			Low = 0,
			Medium = 5,
			High = 10
		}
		fn main() : void {
			var p : Priority = .High;
			p = .Low;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(EnumShorthandInFunctionArg) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
		fn set_state(s : State) : void {
		}

		fn main() : void {
			set_state(.Running);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(EnumShorthandAmbiguousFails) {
	const char* source = R"(
		fn main() : void {
			var x = .Running;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumDeclarationTrailingSemicolonFails) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumDoesNotConvertImplicitlyToInteger) {
	const char* assignment = R"(
		enum State {
			Idle,
			Running
		}
		fn main() : void {
			const value : i32 = State.Running;
		}
	)";
	EXPECT_COMPILE_FAIL(assignment);

	const char* argument = R"(
		enum State {
			Idle,
			Running
		}
		fn takes_i32(value : i32) : void {
		}

		fn main() : void {
			takes_i32(State.Running);
		}
	)";
	EXPECT_COMPILE_FAIL(argument);

	const char* comparison = R"(
		enum State {
			Idle,
			Running
		}
		fn main(value : i32) : bool {
			return value == State.Running;
		}
	)";
	EXPECT_COMPILE_FAIL(comparison);

	const char* integer_to_enum = R"(
		enum State {
			Idle,
			Running
		}
		fn main(value : i32) : void {
			const state : State = value;
		}
	)";
	EXPECT_COMPILE_FAIL(integer_to_enum);
	return true;
}

TEST(EnumCanBeExplicitlyCastToInteger) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
		fn takes_i32(value : i32) : void {
		}

		fn main(value : i32) : bool {
			const running : i32 = State.Running as i32;
			const state : State = value as State;
			takes_i32(State.Idle as i32);
			return state == State.Running and value == State.Running as i32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntegerToEnumCastAllowsAnyIntegerValue) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
		fn main() : State {
			const raw : i32 = 123;
			return raw as State;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(EnumMethodCallOnReturnValue) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
        
        fn bar(s : State) : bool  { return s == .Idle; }

        fn foo() : State { return State.Idle; }

		fn main() : bool {
            var b = foo().bar();
            return b;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}


TEST(EnumShorthandMethodCallFail) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}

		fn foo(s : State) : void {}

		fn main() : void {
            .Idle.foo();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(EnumUnknownShorthand) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}

		fn main() : void {
            var a : State = .Idle;
            var b : State = .Error;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumUnknownMemberMethod) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}

		fn main() : bool {
            var b = State.Idle;
			b.foo();
            return b == State.Idle;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumMemberAccessThroughValueFails) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}

		fn main() : void {
			var s : State = State.Idle;
			const v = State.Idle; // Both State. and s. are resolved to enumresolvedtype, which is not correct
			const v2 = s.Idle;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(EnumValueInitializedFromOtherEnumFails) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}

		enum State2 {
			Idle,
			Running
		}

		fn main() : void {
			var s : State = State2.Idle;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumMemberWithExplicitValueReturnsValue) {
	const char* source = R"(
		enum Priority {
			Low = 0,
			Medium = 5,
			High = 10
		}
		fn main() : i32 {
			return Priority.Medium as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(EnumMemberWithImplicitValueReturnsIndex) {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		}
		fn main() : i32 {
			return State.Running as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(EnumValueExceedsInt32Max) {
	const char* source = R"(
		enum LargeValue {
			Big = 2147483648
		}
		fn main() : i64 {
			return LargeValue.Big as i64;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(2147483648LL, ls_to_i64(runtime, -1));
	CAPI_END(module);
	return true;
}
