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
	EvoxImportFile files_storage[] = {
		{ toLs("core:Keycode"), toLs(keycode_source) }
	};
	EvoxImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(EnumAllowsDuplicateDiscriminants) {
	const char* source = R"(
		enum Keycode {
			KANA = 21,
			HANGEUL = 21,
			HANGUL = 21
		}

		fn main() : i32 {
			return Keycode.HANGUL as i32;
		}
	)";
	EXPECT_COMPILE(source);
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
	EvoxImportFile file = { toLs("state"), toLs(state_source) };
	EvoxImportFiles files = { &file, 1 };
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

		fn main() : i32 {
			var result : i32 = 0;
			var state = foo();
			if state.bar() { result += 1; }
			if foo().bar() { result += 2; }
			return result;
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(5, ex_task_to_i32(runtime, -1));
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(1, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(EnumU64ValueExceedsInt32Max) {
	const char* source = R"(
		enum LargeValue : u64 {
			Big = 2147483648
		}
		fn main() : i64 {
			return LargeValue.Big as i64;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(2147483648LL, ex_task_to_i64(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(EnumBackingTypeControlsSizeAndAlignment) {
	const char* source = R"(
		enum E8 : u8 { Value }
		enum E16 : i16 { Value }
		enum E32 : u32 { Value }
		enum E64 : i64 { Value }
		enum ESize : isize { Value }

		fn main() : i32 {
			if sizeof(E8) != 1 or alignof(E8) != 1 { return 1; }
			if sizeof(E16) != 2 or alignof(E16) != 2 { return 2; }
			if sizeof(E32) != 4 or alignof(E32) != 4 { return 3; }
			if sizeof(E64) != 8 or alignof(E64) != 8 { return 4; }
			if sizeof(ESize) != sizeof(isize) or alignof(ESize) != alignof(isize) { return 5; }
			return 42;
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

TEST(EnumDefaultBackingTypeIsI32) {
	EXPECT_COMPILE(R"(
		enum State { Idle, Running }
		fn main() : void {
			if sizeof(State) != sizeof(i32) { var bad : MissingType = undefined; }
			if alignof(State) != alignof(i32) { var bad : MissingType = undefined; }
		}
	)");
	return true;
}

TEST(EnumBackingTypeAcceptsComptimeTypeAlias) {
	EXPECT_COMPILE(R"(
		comptime Storage = u16;
		enum Code : Storage { Ok, Failed }
		fn main() : void {
			if sizeof(Code) != sizeof(u16) { var bad : MissingType = undefined; }
			if alignof(Code) != alignof(u16) { var bad : MissingType = undefined; }
		}
	)");
	return true;
}

TEST(AnonymousEnumSupportsBackingType) {
	const char* source = R"(
		comptime Result = enum : i16 {
			NotFound = -1,
			Ok = 20
		};
		fn main() : i32 {
			return Result.NotFound as i32 + Result.Ok as i32 + sizeof(Result) as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(21, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(EnumImplicitValueContinuesAfterExplicitValue) {
	const char* source = R"(
		enum State : u16 {
			Idle,
			Paused = 40,
			Stopping,
			Stopped
		}
		fn main() : i32 {
			return State.Idle as i32
				+ State.Paused as i32
				+ State.Stopping as i32
				+ State.Stopped as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(123, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SignedEnumBackingPreservesNegativeValues) {
	const char* source = R"(
		enum Signed : i8 {
			Minimum = -128,
			NegativeTwo = -2,
			NegativeOne
		}
		fn main() : i64 {
			return Signed.Minimum as i64 + Signed.NegativeTwo as i64 + Signed.NegativeOne as i64;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(-131LL, ex_task_to_i64(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnsignedEnumBackingSupportsU64Maximum) {
	const char* source = R"(
		enum Flags : u64 {
			All = 18446744073709551615
		}
		fn main() : u64 {
			return Flags.All as u64;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_TRUE(ex_task_to_u64(runtime, -1) == 18446744073709551615ull);
	CAPI_END(module);
	return true;
}

TEST(EnumValuesPreserveUntypedIntegerSign) {
	const char* source = R"(
		comptime UnsignedValue = 18446744073709551615;
		comptime UnsignedAlias = UnsignedValue;
		comptime SignedValue = -2;
		comptime SignedAlias = SignedValue;

		enum Unsigned : u64 { Value = UnsignedAlias }
		enum Signed : i8 { Value = SignedAlias }

		fn main() : i32 {
			if Unsigned.Value as u64 != 18446744073709551615 { return 1; }
			if Signed.Value as i64 != -2 { return 2; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(0, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(IntegerToBackedEnumCastUsesBackingWidth) {
	const char* source = R"(
		enum Small : u8 { Zero }
		fn main() : i32 {
			const raw : u16 = 300;
			const value = raw as Small;
			return value as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(44, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(BackedEnumWorksInAggregatesGlobalsAndCalls) {
	const char* source = R"(
		enum Tiny : u8 { A = 10, B = 20, C = 12 }
		struct Packed {
			before : u8;
			value : Tiny;
			after : u8;
		}

		var global : Tiny = .B;

		fn read(value : Tiny) : i32 { return value as i32; }
		fn make() : Tiny { return .C; }

		fn main() : i32 {
			if sizeof(Packed) != 3 or alignof(Packed) != 1 { return 0; }
			const packed = Packed { 1, .A, 2 };
			const values : [3]Tiny = [.A, global, make()];
			return packed.before as i32 + read(packed.value) + packed.after as i32
				+ read(values[0]) + read(values[1]) + read(values[2]);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(55, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(EnumBackingRejectsNonIntegerTypes) {
	EXPECT_COMPILE_FAIL("enum Bad : bool { Value }");
	EXPECT_COMPILE_FAIL("enum Bad : byte { Value }");
	EXPECT_COMPILE_FAIL("enum Bad : f32 { Value }");
	EXPECT_COMPILE_FAIL("enum Bad : f64 { Value }");
	EXPECT_COMPILE_FAIL(R"(
		struct Storage { value : i32; }
		enum Bad : Storage { Value }
	)");
	EXPECT_COMPILE_FAIL(R"(
		enum Base { Value }
		enum Bad : Base { Value }
	)");
	return true;
}

TEST(EnumBackingAcceptsComptimeTypeProducingFunction) {
	EXPECT_COMPILE(R"(
		fn storage_type() : type { return u8; }
		enum Code : storage_type() { Value }
		fn main() : void {
			if sizeof(Code) != sizeof(u8) { var bad : MissingType = undefined; }
			if alignof(Code) != alignof(u8) { var bad : MissingType = undefined; }
		}
	)");
	return true;
}

TEST(EnumExplicitValueMustFitUnsignedBacking) {
	EXPECT_COMPILE_FAIL("enum TooLarge : u8 { Value = 256 }");
	EXPECT_COMPILE_FAIL("enum Negative : u8 { Value = -1 }");
	EXPECT_COMPILE_FAIL("enum TooLarge : u16 { Value = 65536 }");
	EXPECT_COMPILE_FAIL("comptime Negative = -1; comptime Alias = Negative; enum Bad : u8 { Value = Alias }");
	return true;
}

TEST(EnumExplicitValueMustFitSignedBacking) {
	EXPECT_COMPILE_FAIL("enum TooLarge : i8 { Value = 128 }");
	EXPECT_COMPILE_FAIL("enum TooSmall : i8 { Value = -129 }");
	EXPECT_COMPILE_FAIL("enum TooLarge : i16 { Value = 32768 }");
	EXPECT_COMPILE_FAIL("enum TooSmall : i16 { Value = -32769 }");
	return true;
}

TEST(DefaultEnumBackingRejectsValuesOutsideI32) {
	EXPECT_COMPILE_FAIL("enum TooLarge { Value = 2147483648 }");
	EXPECT_COMPILE_FAIL("enum TooSmall { Value = -2147483649 }");
	return true;
}

TEST(EnumImplicitValueOverflowFails) {
	EXPECT_COMPILE_FAIL("enum Overflow : u8 { Last = 255, Invalid }");
	EXPECT_COMPILE_FAIL("enum Overflow : i8 { Last = 127, Invalid }");
	return true;
}

TEST(EnumMemberValueMustBeComptimeInteger) {
	EXPECT_COMPILE_FAIL("enum Fractional : u8 { Value = 1.5 }");
	EXPECT_COMPILE_FAIL(R"(
		var runtime_value : i32 = 1;
		enum Runtime : i32 { Value = runtime_value }
	)");
	return true;
}

TEST(EnumDuplicateExplicitDiscriminantIsAllowed) {
	EXPECT_COMPILE(R"(
		enum Duplicate : u8 {
			First = 10,
			Second = 10
		}
	)");
	return true;
}

TEST(EnumDuplicateImplicitDiscriminantIsAllowed) {
	EXPECT_COMPILE(R"(
		enum Duplicate : u8 {
			First = 1,
			Second = 0,
			Third
		}
	)");
	return true;
}

TEST(BackedEnumWorksInMatch) {
	const char* source = R"(
		enum Code : i8 { Negative = -1, Zero, Positive }
		fn classify(code : Code) : i32 {
			match code {
				case .Negative: { return 10; }
				case .Zero: { return 20; }
				case .Positive: { return 12; }
			}
		}
		fn main() : i32 { return classify(.Negative) + classify(.Zero) + classify(.Positive); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(BackedEnumValuesReflectionPreservesDiscriminants) {
	const char* source = R"(
		enum Code : u8 { First = 250, Second }
		comptime members = Code::values;
		fn main() : i32 {
			return members[0].value as i32 + members[1].value as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(501, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
