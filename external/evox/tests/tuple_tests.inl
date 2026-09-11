TEST(TupleExplicitLiteralAndIndexing) {
	EXPECT_COMPILE(R"(
		fn main() : f32 {
			var a : tuple { i32, f32 } = .{ 42, 3.14 };
			var b = a[1];
			return b;
		}
	)");
	return true;
}

TEST(TupleIndexingPreservesElementTypes) {
	EXPECT_COMPILE(R"(
		fn main() : i32 {
			const value : tuple { i32, f32, bool } = .{ 7, 2.5, true };
			const i = value[0];
			const f = value[1];
			const flag = value[2];
			if typeof(i) != i32 { var impossible : MissingType = undefined; }
			if typeof(f) != f32 { var impossible : MissingType = undefined; }
			if typeof(flag) != bool { var impossible : MissingType = undefined; }
			return i;
		}
	)");
	return true;
}

TEST(TupleLiteralConcretizesNumericElements) {
	EXPECT_COMPILE(R"(
		fn main() : void {
			const value : tuple { i8, u64, f64 } = .{ 1, 2, 3.5 };
		}
	)");
	return true;
}

TEST(TupleCanBeMutable) {
	EXPECT_COMPILE(R"(
		fn main() : i32 {
			var value : tuple { i32, f32 } = .{ 10, 2.0 };
			value[0] = 32;
			return value[0];
		}
	)");
	return true;
}

TEST(TupleCanBeConst) {
	EXPECT_COMPILE(R"(
		fn main() : f32 {
			const value : tuple { i32, f32 } = .{ 10, 2.0 };
			return value[1];
		}
	)");
	return true;
}

TEST(NestedTuples) {
	EXPECT_COMPILE(R"(
		fn main() : f32 {
			const value : tuple { i32, tuple { bool, f32 } } = .{ 1, .{ true, 3.5 } };
			return value[1][1];
		}
	)");
	return true;
}

TEST(TupleAsFunctionParameterAndReturnValue) {
	EXPECT_COMPILE(R"(
		fn make() : tuple { i32, f32 } {
			return .{ 42, 3.14 };
		}

		fn read(value : tuple { i32, f32 }) : f32 {
			return value[1];
		}

		fn main() : f32 {
			return read(make());
		}
	)");
	return true;
}

TEST(TupleInStructAndArray) {
	EXPECT_COMPILE(R"(
		struct Record {
			value : tuple { i32, f32 };
		}

		fn main() : f32 {
			var records : [2]Record = [
				Record { .{ 1, 1.5 } },
				Record { .{ 2, 2.5 } }
			];
			return records[1].value[1];
		}
	)");
	return true;
}

TEST(TupleCanBeGlobal) {
	EXPECT_COMPILE(R"(
		var value : tuple { i32, f32 } = .{ 10, 2.5 };

		fn main() : i32 {
			return value[0];
		}
	)");
	return true;
}

TEST(TupleCanBeComptime) {
	EXPECT_COMPILE(R"(
		comptime value : tuple { i32, f32 } = .{ 10, 2.5 };

		fn main() : f32 {
			return value[1];
		}
	)");
	return true;
}

TEST(ComptimeTupleRejectsNonIntegerIndexInArraySize) {
	EXPECT_COMPILE_FAIL(R"(
		comptime values : tuple { i32 } = .{ 4 };
		fn main() : void { var array : [values[true]]i32 = undefined; }
	)");
	return true;
}

TEST(ComptimeTupleRejectsOutOfBoundsIndexInArraySize) {
	EXPECT_COMPILE_FAIL(R"(
		comptime values : tuple { i32 } = .{ 4 };
		fn main() : void { var array : [values[99]]i32 = undefined; }
	)");
	return true;
}

TEST(TupleIndexingRequiresConstantIndex) {
	EXPECT_COMPILE_FAIL(R"(
		fn main(index : i32) : i32 {
			const value : tuple { i32, i32 } = .{ 10, 32 };
			return value[index];
		}
	)");
	return true;
}

TEST(TupleLiteralRequiresExpectedType) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value = .{ 1, 2.0 };
		}
	)");
	return true;
}

TEST(TupleRejectsWrongElementType) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ "wrong", 2.0 };
		}
	)");
	return true;
}

TEST(TupleRejectsWrongElementCount) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1 };
		}
	)");
	return true;
}

TEST(TupleRejectsOutOfRangeIndex) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0 };
			const element = value[2];
		}
	)");
	return true;
}

TEST(TupleRejectsNonIntegerIndex) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0 };
			const element = value[1.5];
		}
	)");
	return true;
}

TEST(TupleRejectsAssignmentOfDifferentTupleType) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			var a : tuple { i32, f32 } = .{ 1, 2.0 };
			var b : tuple { i32, i32 } = .{ 1, 2 };
			a = b;
		}
	)");
	return true;
}

// Expected types must flow into every literal context, not just declarations.
TEST(TupleLiteralInAssignment) {
	EXPECT_COMPILE(R"(
		fn main() : i32 {
			var value : tuple { i16, f32 } = .{ 1, 2.0 };
			value = .{ 42, 3.5 };
			return value[0] as i32;
		}
	)");
	return true;
}

TEST(TupleLiteralInCallArgument) {
	EXPECT_COMPILE(R"(
		fn read(value : tuple { i8, f32 }) : f32 { return value[1]; }
		fn main() : f32 { return read(.{ 12, 3.5 }); }
	)");
	return true;
}

TEST(TupleLiteralInArray) {
	EXPECT_COMPILE(R"(
		fn main() : f32 {
			const values : [2]tuple { i32, f32 } = [.{ 1, 2.5 }, .{ 2, 3.5 }];
			return values[1][1];
		}
	)");
	return true;
}

TEST(TupleSingleElement) {
	EXPECT_COMPILE(R"(
		fn main() : i32 {
			const value : tuple { i32 } = .{ 42 };
			if typeof(value[0]) != i32 { var impossible : MissingType = undefined; }
			return value[0];
		}
	)");
	return true;
}

TEST(TupleNumericExpressionsUseElementContext) {
	EXPECT_COMPILE(R"(
		comptime number = 12;
		fn main() : void {
			const value : tuple { i8, u64, f32 } = .{ number + 1, 18446744073709551615, 1.25 + 2.5 };
			if typeof(value[0]) != i8 { var impossible : MissingType = undefined; }
			if typeof(value[1]) != u64 { var impossible : MissingType = undefined; }
			if typeof(value[2]) != f32 { var impossible : MissingType = undefined; }
		}
	)");
	return true;
}

TEST(TupleLiteralSuppliesEnumAndStructContext) {
	EXPECT_COMPILE(R"(
		enum State { Idle, Running }
		struct Point { x : i16; y : f32; }
		fn main() : f32 {
			const value : tuple { State, Point } = .{ .Running, { 12, 2.5 } };
			return value[1].y;
		}
	)");
	return true;
}

TEST(TupleIndexCanBeComptimeExpression) {
	EXPECT_COMPILE(R"(
		comptime first = 0;
		fn main() : f32 {
			const value : tuple { bool, i32, f32 } = .{ true, 12, 2.5 };
			comptime last : isize = 2;
			const middle : i32 = value[first + 1];
			return value[last];
		}
	)");
	return true;
}

TEST(TupleIndexCanBeComptimeParameter) {
	EXPECT_COMPILE(R"(
		fn read(value : tuple { i32, i32 }, index : comptime isize) : i32 {
			return value[index];
		}
		fn main() : i32 { return read(.{ 10, 32 }, 1); }
	)");
	return true;
}

TEST(TupleUnrolledIndexPreservesHeterogeneousTypes) {
	EXPECT_COMPILE(R"(
		fn main() : void {
			const value : tuple { i32, f32, bool } = .{ 10, 2.5, true };
			unroll for i in 0..3 {
				if i == 0 {
					if typeof(value[i]) != i32 { var impossible : MissingType = undefined; }
				} else if i == 1 {
					if typeof(value[i]) != f32 { var impossible : MissingType = undefined; }
				} else {
					if typeof(value[i]) != bool { var impossible : MissingType = undefined; }
				}
			}
		}
	)");
	return true;
}

TEST(TupleTypeAliasAndGenericIdentity) {
	EXPECT_COMPILE(R"(
		comptime Pair = tuple { i32, f32 };
		fn identity(value : $T) : T { return value; }
		fn main() : f32 {
			const value : Pair = .{ 12, 2.5 };
			const copy = identity(value);
			if typeof(copy) != Pair { var impossible : MissingType = undefined; }
			return copy[1];
		}
	)");
	return true;
}

TEST(TupleTypeFactory) {
	EXPECT_COMPILE(R"(
		fn Pair(A : comptime type, B : comptime type) : type { return tuple { A, B }; }
		fn main() : f32 {
			const value : Pair(i32, f32) = .{ 12, 2.5 };
			return value[1];
		}
	)");
	return true;
}

TEST(TupleTypeFactoryRequiresComptimeParametersForElementTypes) {
	EXPECT_COMPILE_FAIL(R"(
		fn Pair(A : type, B : type) : type { return tuple { A, B }; }
		fn main() : f32 {
			const value : Pair(i32, f32) = .{ 12, 2.5 };
			return value[1];
		}
	)");
	return true;
}

TEST(TupleTypeFactoryDoesNotPromoteOtherTypeParameters) {
	EXPECT_COMPILE_FAIL(R"(
		fn Pair(A : comptime type, B : type) : type { return tuple { A, B }; }
		fn main() : f32 {
			const value : Pair(i32, f32) = .{ 12, 2.5 };
			return value[1];
		}
	)");
	return true;
}

TEST(TupleTypeKindAndName) {
	EXPECT_COMPILE(R"(
		comptime Pair = tuple { i32, f32 };
		fn main() : void {
			const value : Pair = .{ 12, 2.5 };
			if Pair::kind != .Tuple { var impossible : MissingType = undefined; }
			if value::kind != .Tuple { var impossible : MissingType = undefined; }
			if Pair::name != "tuple { i32, f32 }" { var impossible : MissingType = undefined; }
			if value::name != Pair::name { var impossible : MissingType = undefined; }
		}
	)");
	return true;
}

// Each rejection isolates one invalid use after an otherwise valid tuple setup.
TEST(TupleRejectsTooManyElements) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0, true };
		}
	)");
	return true;
}

TEST(TupleRejectsEmptyLiteralForNonemptyType) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32 } = .{};
		}
	)");
	return true;
}

TEST(TupleRejectsWrongNestedElementCount) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, tuple { bool, f32 } } = .{ 1, .{ true } };
		}
	)");
	return true;
}

TEST(TupleRejectsWrongNestedElementType) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, tuple { bool, f32 } } = .{ 1, .{ true, "bad" } };
		}
	)");
	return true;
}

TEST(TupleRejectsIntegerLiteralOverflow) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { bool, i8 } = .{ true, 128 };
		}
	)");
	return true;
}

TEST(TupleRejectsNegativeLiteralForUnsignedElement) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { u8, bool } = .{ -1, true };
		}
	)");
	return true;
}

TEST(TupleRejectsImplicitConcreteNumericConversion) {
	EXPECT_COMPILE_FAIL(R"(
		fn main(number : i32) : void {
			const value : tuple { i64, bool } = .{ number, true };
		}
	)");
	return true;
}

TEST(TupleRejectsStructLiteralSyntax) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = { 1, 2.0 };
		}
	)");
	return true;
}

TEST(TupleRejectsArrayLiteralSyntax) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, i32 } = [1, 2];
		}
	)");
	return true;
}

TEST(TupleLiteralCannotInitializeStruct) {
	EXPECT_COMPILE_FAIL(R"(
		struct Pair { a : i32; b : f32; }
		fn main() : void { const value : Pair = .{ 1, 2.0 }; }
	)");
	return true;
}

TEST(TupleLiteralCannotInitializeArray) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { const value : [2]i32 = .{ 1, 2 }; }
	)");
	return true;
}

TEST(TupleLiteralCannotInferGenericArgument) {
	EXPECT_COMPILE_FAIL(R"(
		fn identity(value : $T) : T { return value; }
		fn main() : void { const value = identity(.{ 1, 2.0 }); }
	)");
	return true;
}

TEST(TupleRejectsNegativeIndex) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0 };
			const element = value[-1];
		}
	)");
	return true;
}

TEST(TupleRejectsComputedOutOfRangeIndex) {
	EXPECT_COMPILE_FAIL(R"(
		comptime last = 1;
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0 };
			const element = value[last + 1];
		}
	)");
	return true;
}

TEST(TupleRejectsBooleanIndex) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, i32 } = .{ 1, 2 };
			const element = value[true];
		}
	)");
	return true;
}

TEST(TupleRejectsStringIndex) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0 };
			const element = value["0"];
		}
	)");
	return true;
}

TEST(TupleRejectsRuntimeIndexEvenForHomogeneousElements) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : i32 {
			const value : tuple { i32, i32 } = .{ 10, 32 };
			var index : isize = 1;
			return value[index];
		}
	)");
	return true;
}

TEST(TupleRejectsNestedOutOfRangeIndex) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, tuple { bool, f32 } } = .{ 1, .{ true, 2.0 } };
			const element = value[1][2];
		}
	)");
	return true;
}

TEST(TupleRejectsOutOfRangeWrite) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			var value : tuple { i32, i32 } = .{ 1, 2 };
			value[2] = 3;
		}
	)");
	return true;
}

TEST(TupleRejectsWrongElementAssignmentType) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			var value : tuple { i32, bool } = .{ 1, true };
			value[1] = 42;
		}
	)");
	return true;
}

TEST(TupleRejectsConstElementAssignment) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0 };
			value[0] = 42;
		}
	)");
	return true;
}

TEST(TupleRejectsConstNestedElementAssignment) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, tuple { bool, f32 } } = .{ 1, .{ true, 2.0 } };
			value[1][1] = 3.0;
		}
	)");
	return true;
}

TEST(TupleRejectsParameterElementAssignment) {
	EXPECT_COMPILE_FAIL(R"(
		fn mutate(value : tuple { i32, f32 }) : void { value[0] = 42; }
		fn main() : void { mutate(.{ 1, 2.0 }); }
	)");
	return true;
}

TEST(TupleRejectsWriteThroughConstPointer) {
	EXPECT_COMPILE_FAIL(R"(
		fn mutate(value : *const tuple { i32, f32 }) : void { value.*[0] = 42; }
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0 };
			mutate(&value);
		}
	)");
	return true;
}

TEST(TupleRejectsMutablePointerToConstElement) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, f32 } = .{ 1, 2.0 };
			const pointer : *i32 = &value[0];
		}
	)");
	return true;
}

TEST(TupleRejectsAssignmentWithDifferentArity) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			var value : tuple { i32, i32 } = .{ 1, 2 };
			const other : tuple { i32 } = .{ 3 };
			value = other;
		}
	)");
	return true;
}

TEST(TupleRejectsAssignmentWithReorderedTypes) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			var value : tuple { i32, bool } = .{ 1, true };
			const other : tuple { bool, i32 } = .{ true, 1 };
			value = other;
		}
	)");
	return true;
}

TEST(TupleRejectsWrongCallArgumentElementType) {
	EXPECT_COMPILE_FAIL(R"(
		fn consume(value : tuple { i32, bool }) : void {}
		fn main() : void { consume(.{ 1, 2 }); }
	)");
	return true;
}

TEST(TupleRejectsWrongReturnElementCount) {
	EXPECT_COMPILE_FAIL(R"(
		fn make() : tuple { i32, bool } { return .{ 1 }; }
		fn main() : void { const value = make(); }
	)");
	return true;
}

TEST(TupleRejectsComptimeInitializerReadingRuntimeStorage) {
	EXPECT_COMPILE_FAIL(R"(
		fn main(number : i32) : void {
			comptime value : tuple { i32, bool } = .{ number, true };
		}
	)");
	return true;
}

TEST(TupleDoesNotExposeStructFieldsReflection) {
	EXPECT_COMPILE_FAIL(R"(
		comptime Pair = tuple { i32, f32 };
		fn main() : void { comptime fields = Pair::fields; }
	)");
	return true;
}

TEST(TupleDoesNotExposeUnionTypesReflection) {
	EXPECT_COMPILE_FAIL(R"(
		comptime Pair = tuple { i32, f32 };
		fn main() : void { comptime types = Pair::types; }
	)");
	return true;
}

// Runtime checks guard against accepted syntax with incorrect aggregate codegen.
TEST(BytecodeTupleMixedWidthElements) {
	const char* source = R"(
		fn main() : i32 {
			var value : tuple { u8, i64, bool, f64, i16 } = .{ 7, 10000000000, true, 2.5, -12 };
			if value[0] != 7 { return 1; }
			if value[1] != 10000000000 { return 2; }
			if not value[2] { return 3; }
			if value[3] != 2.5 { return 4; }
			if value[4] != -12 { return 5; }
			value[1] += 2;
			value[3] *= 2.0;
			if value[0] != 7 or value[1] != 10000000002 or not value[2] { return 6; }
			if value[3] != 5.0 or value[4] != -12 { return 7; }
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

TEST(BytecodeTupleCopyAndWholeAssignment) {
	const char* source = R"(
		fn main() : i32 {
			var original : tuple { i32, tuple { bool, f32 } } = .{ 10, .{ true, 2.5 } };
			var copy = original;
			copy[0] = 20;
			copy[1][0] = false;
			copy[1][1] = 9.0;
			if original[0] != 10 or not original[1][0] or original[1][1] != 2.5 { return 1; }
			original = copy;
			copy[1][1] = 1.0;
			if original[0] != 20 or original[1][0] or original[1][1] != 9.0 { return 2; }
			original = .{ 42, .{ true, 3.5 } };
			return original[0];
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

TEST(BytecodeTupleParameterAndReturnAreValueCopies) {
	const char* source = R"(
		fn change(value : tuple { i32, f64, bool }) : tuple { i32, f64, bool } {
			var copy = value;
			copy[0] += 32;
			copy[1] = 7.5;
			copy[2] = false;
			return copy;
		}
		fn main() : i32 {
			var value : tuple { i32, f64, bool } = .{ 10, 2.5, true };
			const result = change(value);
			if value[0] != 10 or value[1] != 2.5 or not value[2] { return 1; }
			if result[1] != 7.5 or result[2] { return 2; }
			return result[0];
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

TEST(BytecodeTupleTemporaryIndexEvaluatesCallOnce) {
	const char* source = R"(
		var calls : i32 = 0;
		fn make() : tuple { bool, tuple { i32, f64 } } {
			calls += 1;
			return .{ true, .{ 42, 2.5 } };
		}
		fn main() : i32 {
			const result = make()[1][0];
			if calls != 1 { return 1; }
			return result;
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

TEST(BytecodeTupleLiteralEvaluatesEachElementOnce) {
	const char* source = R"(
		var calls : i32 = 0;
		fn integer() : i32 { calls += 1; return 42; }
		fn decimal() : f64 { calls += 1; return 2.5; }
		fn main() : i32 {
			const value : tuple { i32, f64 } = .{ integer(), decimal() };
			if calls != 2 or value[1] != 2.5 { return 1; }
			return value[0];
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

TEST(BytecodeTupleElementPointerMutatesOriginal) {
	const char* source = R"(
		fn bump(value : *i32) : void { value.* += 32; }
		fn read(value : *const f64) : f64 { return value.*; }
		fn main() : i32 {
			var value : tuple { bool, tuple { i32, f64 } } = .{ true, .{ 10, 2.5 } };
			bump(&value[1][0]);
			if not value[0] or read(&value[1][1]) != 2.5 { return 1; }
			const snapshot = value;
			if read(&snapshot[1][1]) != 2.5 { return 2; }
			return value[1][0];
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

TEST(BytecodeTuplePointerParameter) {
	const char* source = R"(
		fn change(value : *tuple { i32, f64 }) : void {
			value.*[0] += 32;
			value.*[1] = 7.5;
		}
		fn main() : i32 {
			var value : tuple { i32, f64 } = .{ 10, 2.5 };
			change(&value);
			if value[1] != 7.5 { return 1; }
			return value[0];
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

TEST(BytecodeTupleArrayDynamicIndexAndSlice) {
	const char* source = R"(
		fn change(values : []tuple { i32, f64 }, index : isize) : void {
			values[index][0] += 32;
			values[index][1] = 7.5;
		}
		fn main() : i32 {
			var values : [2]tuple { i32, f64 } = [.{ 1, 1.5 }, .{ 10, 2.5 }];
			change(values, 1);
			if values[0][0] != 1 or values[0][1] != 1.5 { return 1; }
			if values[1][1] != 7.5 { return 2; }
			return values[1][0];
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

TEST(BytecodeTupleStructAndArrayElements) {
	const char* source = R"(
		struct Point { x : i32; y : f64; }
		fn main() : i32 {
			var value : tuple { Point, [2]i32, bool } = .{ { 10, 2.5 }, [1, 2], true };
			var copy = value;
			copy[0].x = 40;
			copy[1][1] = 7;
			if value[0].x != 10 or value[1][1] != 2 { return 1; }
			if copy[0].y != 2.5 or not copy[2] { return 2; }
			return copy[0].x + value[1][1];
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

TEST(BytecodeTupleFunctionAndStringElements) {
	const char* source = R"(
		fn add(value : i32) : i32 { return value + 32; }
		fn main() : i32 {
			const value : tuple { []const u8, fn(i32) : i32, i32 } = .{ "tuple", add, 10 };
			if value[0] != "tuple" { return 1; }
			return value[1](value[2]);
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

TEST(BytecodeTuplePointerAndSliceCopiesSharePointees) {
	const char* source = R"(
		fn main() : i32 {
			var number : i32 = 10;
			var array : [2]i32 = [1, 2];
			const value : tuple { *i32, []i32 } = .{ &number, array[:] };
			const copy = value;
			copy[0].* += 30;
			copy[1][1] = 7;
			if value[0].* != 40 or value[1][1] != 7 or array[1] != 7 { return 1; }
			return number + 2;
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

TEST(BytecodeTupleGlobalMutation) {
	const char* source = R"(
		var value : tuple { i32, f64, bool } = .{ 10, 2.5, true };
		fn bump() : void { value[0] += 32; value[1] = 7.5; }
		fn main() : i32 {
			if value[0] != 10 or value[1] != 2.5 or not value[2] { return 1; }
			bump();
			if value[1] != 7.5 or not value[2] { return 2; }
			return value[0];
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

TEST(TupleEmptyLiteralAndValuePassing) {
	EXPECT_COMPILE(R"(
		fn make() : tuple {} { return .{}; }
		fn identity(value : tuple {}) : tuple {} { return value; }
		fn main() : void {
			var value : tuple {} = .{};
			value = identity(make());
			const nested : tuple { tuple {}, i32 } = .{ value, 42 };
			if typeof(nested[0]) != tuple {} { var impossible : MissingType = undefined; }
		}
	)");
	return true;
}

TEST(TupleTrailingCommaInType) {
	EXPECT_COMPILE(R"(
		fn main() : i32 {
			const single : tuple { i32, } = .{ 42 };
			const pair : tuple { i32, bool, } = .{ single[0], true };
			return pair[0];
		}
	)");
	return true;
}

TEST(TupleTrailingCommaInLiteral) {
	EXPECT_COMPILE(R"(
		fn main() : i32 {
			const single : tuple { i32 } = .{ 42, };
			const pair : tuple { i32, bool } = .{ single[0], true, };
			return pair[0];
		}
	)");
	return true;
}

TEST(TupleStructuralTypeIdentity) {
	EXPECT_COMPILE(R"(
		comptime A = tuple { i32, tuple { bool, f64 } };
		comptime B = tuple { i32, tuple { bool, f64, }, };
		fn accept(value : B) : A { return value; }
		fn main() : i32 {
			if A != B { var impossible : MissingType = undefined; }
			const original : A = .{ 42, .{ true, 2.5 } };
			var copy : B = original;
			copy = accept(original);
			return copy[0];
		}
	)");
	return true;
}

TEST(TupleTypeIdentityIncludesOrderArityAndElementIdentity) {
	EXPECT_COMPILE(R"(
		struct A { x : i32; }
		struct B { x : i32; }
		comptime Ordered = tuple { i32, bool };
		comptime Reordered = tuple { bool, i32 };
		comptime Single = tuple { i32 };
		comptime WrappedA = tuple { A };
		comptime WrappedB = tuple { B };
		fn main() : void {
			if Ordered == Reordered { var impossible : MissingType = undefined; }
			if Ordered == Single { var impossible : MissingType = undefined; }
			if Single == i32 { var impossible : MissingType = undefined; }
			if WrappedA == WrappedB { var impossible : MissingType = undefined; }
		}
	)");
	return true;
}

TEST(TupleIndexCanBeComptimeCall) {
	EXPECT_COMPILE(R"(
		fn index() : isize { return 1; }
		fn main() : f64 {
			const value : tuple { bool, f64 } = .{ true, 2.5 };
			comptime selected = index();
			return value[selected];
		}
	)");
	return true;
}

TEST(TupleRejectsIndexIntoEmptyTuple) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple {} = .{};
			const element = value[0];
		}
	)");
	return true;
}

TEST(TupleRejectsElementInEmptyLiteral) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { const value : tuple {} = .{ 1 }; }
	)");
	return true;
}

TEST(TupleRejectsScalarForSingleElementTuple) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void { const value : tuple { i32 } = 42; }
	)");
	return true;
}

TEST(TupleRejectsSingleElementTupleAsScalar) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : i32 {
			const value : tuple { i32 } = .{ 42 };
			return value;
		}
	)");
	return true;
}

TEST(TupleRejectsTemporaryElementAssignment) {
	EXPECT_COMPILE_FAIL(R"(
		fn make() : tuple { i32, bool } { return .{ 42, true }; }
		fn main() : void { make()[0] = 7; }
	)");
	return true;
}

TEST(TupleRejectsPointerToTemporaryElement) {
	EXPECT_COMPILE_FAIL(R"(
		fn make() : tuple { i32, bool } { return .{ 42, true }; }
		fn main() : void { const pointer : *const i32 = &make()[0]; }
	)");
	return true;
}

TEST(TupleRejectsBuiltinEquality) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : bool {
			const a : tuple { i32, bool } = .{ 42, true };
			const b = a;
			return a == b;
		}
	)");
	return true;
}

TEST(TupleRejectsBuiltinInequality) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : bool {
			const a : tuple { i32, bool } = .{ 42, true };
			const b = a;
			return a != b;
		}
	)");
	return true;
}

TEST(TupleRejectsBuiltinOrdering) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : bool {
			const a : tuple { i32 } = .{ 1 };
			const b : tuple { i32 } = .{ 2 };
			return a < b;
		}
	)");
	return true;
}

TEST(TupleRejectsEqualityOperatorDeclaration) {
	EXPECT_COMPILE_FAIL(R"(
		comptime Pair = tuple { i32, bool };
		operator ==(a : Pair, b : Pair) : bool { return a[0] == b[0] and a[1] == b[1]; }
		fn main() : void {}
	)");
	return true;
}

TEST(TupleRejectsUnaryOperatorDeclaration) {
	EXPECT_COMPILE_FAIL(R"(
		comptime Single = tuple { i32 };
		operator -(value : Single) : Single { return .{ -value[0] }; }
		fn main() : void {}
	)");
	return true;
}

TEST(TupleRejectsOperatorWithTupleAsFirstParameter) {
	EXPECT_COMPILE_FAIL(R"(
		struct Amount { value : i32; }
		operator +(a : tuple { i32 }, b : Amount) : i32 { return a[0] + b.value; }
		fn main() : void {}
	)");
	return true;
}

TEST(TupleRejectsOperatorWithTupleAsSecondParameter) {
	EXPECT_COMPILE_FAIL(R"(
		struct Amount { value : i32; }
		operator +(a : Amount, b : tuple { i32 }) : i32 { return a.value + b[0]; }
		fn main() : void {}
	)");
	return true;
}

TEST(BytecodeTupleElementRetainsStructOperators) {
	const char* source = R"(
		struct Amount { value : i32; }
		operator +(a : Amount, b : Amount) : Amount { return Amount { a.value + b.value }; }
		fn main() : i32 {
			var value : tuple { Amount, bool } = .{ { 10 }, true };
			value[0] += Amount { 32 };
			if not value[1] { return 1; }
			return value[0].value;
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

TEST(BytecodeComptimeArrayOfTuplesMaterialization) {
	const char* source = R"(
		comptime values : [2]tuple { i32, bool } = [
			.{ 10, true },
			.{ 20, false }
		];
		fn main() : i32 {
			var copy = values;
			if copy[0][0] != 10 { return 1; }
			if not copy[0][1] { return 2; }
			if copy[1][0] != 20 { return 3; }
			if copy[1][1] { return 4; }
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

TEST(BytecodeTupleComptimeMaterialization) {
	const char* source = R"(
		comptime value : tuple { i32, tuple { bool, f64 } } = .{ 42, .{ true, 2.5 } };
		fn read(input : tuple { i32, tuple { bool, f64 } }) : i32 {
			if not input[1][0] or input[1][1] != 2.5 { return 1; }
			return input[0];
		}
		fn main() : i32 {
			comptime first = value[0];
			if first != 42 { var impossible : MissingType = undefined; }
			var copy = value;
			copy[0] = 7;
			if value[0] != 42 { return 2; }
			return read(value);
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


TEST(TupleLocalComptimeElementRuntimeReturn) {
	EXPECT_COMPILE(R"(
		fn main() : bool {
			comptime args : tuple { i32, bool } = .{ 42, true };
			return args[1];
		}
	)");
	return true;
}

TEST(TupleUnpackLiteralUsesParameterContext) {
	EXPECT_COMPILE(R"(
		fn accept(a : i8, b : u64, c : f32) : void {}
		fn main() : void { accept(.{ -12, 18446744073709551615, 2.5 }...); }
	)");
	return true;
}

TEST(TupleUnpackLiteralContextAfterOrdinaryArgument) {
	EXPECT_COMPILE(R"(
		fn accept(a : bool, b : i16, c : f64) : void {}
		fn main() : void { accept(true, .{ 12, 2.5 }...); }
	)");
	return true;
}

TEST(TupleUnpackLiteralSuppliesAggregateAndEnumContext) {
	EXPECT_COMPILE(R"(
		enum State { Idle, Running }
		struct Point { x : i16; y : f32; }
		fn accept(state : State, point : Point, pair : tuple { bool, i8 }) : void {}
		fn main() : void { accept(.{ .Running, { 12, 2.5 }, .{ true, 7 } }...); }
	)");
	return true;
}

TEST(TupleUnpackGenericInfersEachElementType) {
	EXPECT_COMPILE(R"(
		fn accept(a : $A, b : $B, c : $C) : void {
			if A != i16 { var impossible : MissingType = undefined; }
			if B != f64 { var impossible : MissingType = undefined; }
			if C != bool { var impossible : MissingType = undefined; }
		}
		fn main() : void {
			const args : tuple { i16, f64, bool } = .{ 12, 2.5, true };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRepeatedGenericType) {
	EXPECT_COMPILE(R"(
		fn first(a : $T, b : T) : T { return a; }
		fn main() : i32 {
			const args : tuple { i32, i32 } = .{ 42, 7 };
			return first(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackComptimeParameter) {
	EXPECT_COMPILE(R"(
		fn accept(size : comptime i32, flag : bool) : void {
			if size != 42 { var impossible : MissingType = undefined; }
		}
		fn main() : void {
			comptime args : tuple { i32, bool } = .{ 42, true };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsTooFewArguments) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32, b : i32) : void {}
		fn main() : void {
			const args : tuple { i32 } = .{ 1 };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsTooManyArguments) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32) : void {}
		fn main() : void {
			const args : tuple { i32, i32 } = .{ 1, 2 };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsEmptyTupleForRequiredParameter) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32) : void {}
		fn main() : void {
			const args : tuple {} = .{};
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsNonemptyTupleForNoParameters) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept() : void {}
		fn main() : void {
			const args : tuple { i32 } = .{ 1 };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsExcessOrdinaryArgument) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32, b : i32) : void {}
		fn main() : void {
			const args : tuple { i32, i32 } = .{ 1, 2 };
			accept(0, args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsWrongElementType) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32, b : bool) : void {}
		fn main() : void {
			const args : tuple { i32, i32 } = .{ 1, 2 };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsReorderedElementTypes) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32, b : bool) : void {}
		fn main() : void {
			const args : tuple { bool, i32 } = .{ true, 1 };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsImplicitConcreteNumericConversion) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i64) : void {}
		fn main() : void {
			const args : tuple { i32 } = .{ 42 };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsLiteralOverflow) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : bool, b : i8) : void {}
		fn main() : void { accept(true, .{ 128 }...); }
	)");
	return true;
}

TEST(TupleUnpackRejectsNegativeUnsignedLiteral) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : u8) : void {}
		fn main() : void { accept(.{ -1 }...); }
	)");
	return true;
}

TEST(TupleUnpackRejectsRecursiveFlattening) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32, b : bool, c : f64) : void {}
		fn main() : void {
			const args : tuple { i32, tuple { bool, f64 } } = .{ 1, .{ true, 2.5 } };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackDoesNotDiscardNestedEmptyTuple) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept() : void {}
		fn main() : void {
			const args : tuple { tuple {} } = .{ .{} };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsScalarOperand) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32) : void {}
		fn main() : void { const args : i32 = 42; accept(args...); }
	)");
	return true;
}

TEST(TupleUnpackRejectsStructOperand) {
	EXPECT_COMPILE_FAIL(R"(
		struct Pair { a : i32; b : i32; }
		fn accept(a : i32, b : i32) : void {}
		fn main() : void { const args = Pair { 1, 2 }; accept(args...); }
	)");
	return true;
}

TEST(TupleUnpackRejectsArrayOperand) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32, b : i32) : void {}
		fn main() : void { const args : [2]i32 = [1, 2]; accept(args...); }
	)");
	return true;
}

TEST(TupleUnpackRejectsSliceOperand) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32, b : i32) : void {}
		fn main(args : []i32) : void { accept(args...); }
	)");
	return true;
}

TEST(TupleUnpackRejectsPointerWithoutDereference) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : i32) : void {}
		fn main() : void {
			var args : tuple { i32 } = .{ 42 };
			const pointer = &args;
			accept(pointer...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsOutsideCall) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const args : tuple { i32 } = .{ 42 };
			const value = args...;
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsRuntimeComptimeArgument) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : comptime i32) : void {}
		fn main(value : i32) : void {
			const args : tuple { i32 } = .{ value };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsConflictingGenericElements) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : $T, b : T) : void {}
		fn main() : void {
			const args : tuple { i32, bool } = .{ 42, true };
			accept(args...);
		}
	)");
	return true;
}

TEST(TupleUnpackRejectsDroppingPointeeConst) {
	EXPECT_COMPILE_FAIL(R"(
		fn accept(a : *i32) : void {}
		fn main() : void {
			const value : i32 = 42;
			const args : tuple { *const i32 } = .{ &value };
			accept(args...);
		}
	)");
	return true;
}

TEST(BytecodeTupleUnpackDeclarationOrder) {
	const char* source = R"(
		fn digits(a : i32, b : i32, c : i32) : i32 { return 100 * a + 10 * b + c; }
		fn main() : i32 {
			const args : tuple { i32, i32, i32 } = .{ 1, 2, 3 };
			if digits(args...) != 123 { return 1; }
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

TEST(BytecodeTupleUnpackMixedWidths) {
	const char* source = R"(
		fn check(a : u8, b : i64, c : bool, d : f64, e : i16) : i32 {
			if a != 7 or b != 10000000000 or not c or d != 2.5 or e != -12 { return 1; }
			return 42;
		}
		fn main() : i32 {
			const args : tuple { u8, i64, bool, f64, i16 } = .{ 7, 10000000000, true, 2.5, -12 };
			return check(args...);
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

TEST(BytecodeTupleUnpackMixedArgumentPositions) {
	const char* source = R"(
		fn digits(a : i32, b : i32, c : i32, d : i32) : i32 { return 1000 * a + 100 * b + 10 * c + d; }
		fn main() : i32 {
			const first : tuple { i32, i32 } = .{ 1, 2 };
			const middle : tuple { i32, i32 } = .{ 2, 3 };
			const last : tuple { i32, i32 } = .{ 3, 4 };
			if digits(first..., 3, 4) != 1234 { return 1; }
			if digits(1, middle..., 4) != 1234 { return 2; }
			if digits(1, 2, last...) != 1234 { return 3; }
			if digits(first..., last...) != 1234 { return 4; }
			if digits(1, .{ 2, 3 }..., 4) != 1234 { return 5; }
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

TEST(BytecodeTupleUnpackSingleElement) {
	const char* source = R"(
		fn identity(value : i32) : i32 { return value; }
		fn main() : i32 {
			const single : tuple { i32 } = .{ 42 };
			return identity(single...);
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

TEST(BytecodeTupleUnpackNestedElementsStayWhole) {
	const char* source = R"(
		fn check(empty : tuple {}, pair : tuple { i32, bool }, tail : i32) : i32 {
			if not pair[1] { return 1; }
			return pair[0] + tail;
		}
		fn main() : i32 {
			const args : tuple { tuple {}, tuple { i32, bool }, i32 } = .{ .{}, .{ 10, true }, 32 };
			return check(args...);
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

TEST(BytecodeTupleUnpackTemporaryEvaluatedOnce) {
	const char* source = R"(
		var calls : i32 = 0;
		fn make() : tuple { i32, i32 } { calls += 1; return .{ 10, 32 }; }
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn main() : i32 {
			const result = add(make()...);
			if calls != 1 { return 1; }
			return result;
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

TEST(TupleUnpackRejectsEmptyTemporary) {
	EXPECT_COMPILE_FAIL(R"(
		fn make() : tuple {} { return .{}; }
		fn answer() : i32 { return 42; }
		fn main() : i32 { return answer(make()...); }
	)");
	return true;
}

TEST(BytecodeTupleUnpackIndexedOperandEvaluatedOnce) {
	const char* source = R"(
		var calls : i32 = 0;
		fn index() : isize { calls += 1; return 1; }
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn main() : i32 {
			const values : [2]tuple { i32, i32 } = [.{ 1, 2 }, .{ 10, 32 }];
			const result = add(values[index()]...);
			if calls != 1 { return 1; }
			return result;
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

TEST(BytecodeTupleUnpackFieldDereferenceAndParameter) {
	const char* source = R"(
		struct Record { args : tuple { i32, i32 }; }
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn forward(args : tuple { i32, i32 }) : i32 { return add(args...); }
		fn main() : i32 {
			var record = Record { .{ 10, 32 } };
			const pointer = &record.args;
			if add(record.args...) != 42 { return 1; }
			if add(pointer.*...) != 42 { return 2; }
			record.args[0] = 20;
			record.args[1] = 22;
			return forward(record.args);
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

TEST(BytecodeTupleUnpackIndirectCall) {
	const char* source = R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn apply(callback : fn(i32, i32) : i32, args : tuple { i32, i32 }) : i32 {
			return callback(args...);
		}
		fn main() : i32 { return apply(add, .{ 10, 32 }); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(BytecodeTupleUnpackAggregateValueCopies) {
	const char* source = R"(
		struct Point { x : i32; y : f64; }
		fn change(point : Point, array : [2]i32) : i32 {
			var p = point;
			var a = array;
			p.x += 30;
			a[0] = 99;
			if p.y != 2.5 { return 1; }
			return p.x + a[1];
		}
		fn main() : i32 {
			var args : tuple { Point, [2]i32 } = .{ { 10, 2.5 }, [1, 2] };
			const result = change(args...);
			if args[0].x != 10 or args[1][0] != 1 { return 2; }
			return result;
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

TEST(BytecodeTupleUnpackPointerAndSliceRetainAliasing) {
	const char* source = R"(
		fn change(number : *i32, values : []i32) : void { number.* += 30; values[1] = 7; }
		fn main() : i32 {
			var number : i32 = 10;
			var values : [2]i32 = [1, 2];
			const args : tuple { *i32, []i32 } = .{ &number, values[:] };
			change(args...);
			if values[1] != 7 or args[1][1] != 7 { return 1; }
			return number + 2;
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

TEST(BytecodeTupleUnpackComptimeCall) {
	const char* source = R"(
		fn add(a : i32, b : i32) : i32 { return a + b; }
		comptime args : tuple { i32, i32 } = .{ 10, 32 };
		fn main() : i32 {
			comptime result = add(args...);
			if result != 42 { var impossible : MissingType = undefined; }
			return result;
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

TEST(BytecodeTupleUnpackTemporaryOrderAndLifetime) {
	const char* source = R"(
		var trace : i32 = 0;
		var calls : i32 = 0;
		fn mark(value : i32) : i32 { trace = trace * 10 + value; return value; }
		fn pair(a : i32, b : i32) : tuple { i32, i32 } {
			calls += 1;
			return .{ mark(a), mark(b) };
		}
		fn add(a : i32, b : i32) : i32 { return a + b; }
		fn choose() : fn(i32, i32) : i32 { mark(1); return add; }
		fn sum(start : i32, values : ...i32) : i32 {
			var result = start;
			for value in values { result += value; }
			return result;
		}
		struct Receiver { value : i32; }
		fn receiver() : Receiver { return { mark(1) }; }
		fn combine(self : Receiver, a : i32, b : i32) : i32 { return self.value + a + b; }
		fn forward(args : $T) : i32 { return add(args...); }
		fn main() : i32 {
			// Nested calls must neither reorder nor overwrite the preceding argument.
			if add(mark(1), add(pair(2, 3)...)) != 6 { return 1; }
			if trace != 123 or calls != 1 { return 2; }
			trace = 0; calls = 0;
			if sum(mark(1), pair(2, 3)..., mark(4), pair(5, 6)...) != 21 { return 3; }
			if trace != 123456 or calls != 2 { return 4; }
			trace = 0; calls = 0;
			if choose()(pair(2, 3)...) != 5 { return 5; }
			if trace != 123 or calls != 1 { return 6; }
			trace = 0; calls = 0;
			if receiver().combine(pair(2, 3)...) != 6 { return 7; }
			if trace != 123 or calls != 1 { return 8; }
			trace = 0; calls = 0;
			unroll for i in 0..3 {
				if sum(0, pair(1, 2)...) != 3 { return 9; }
			}
			if trace != 121212 or calls != 3 { return 10; }
			trace = 0; calls = 0;
			if forward(pair(1, 2)) != 3 or forward(pair(3, 4)) != 7 { return 11; }
			if trace != 1234 or calls != 2 { return 12; }
			return 42;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	for (bool optimize : {false, true}) {
		ex_bytecode_compile_options options = {};
		options.optimize = optimize;
		ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, &options);
		EXPECT_TRUE(bytecode != nullptr);
		ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
		EXPECT_TRUE(runtime != nullptr);
		ex_task* task = ex_task_create(runtime);
		EXPECT_TRUE(task != nullptr);
		EXPECT_EQ(EX_CALL_RESULT_OK, ex_call(task, toLs("main"), nullptr, 0));
		EXPECT_EQ(42, ex_task_to_i32(task, -1));
		ex_task_destroy(task);
		ex_runtime_destroy(runtime);
		ex_bytecode_destroy(bytecode);
	}
	CAPI_END(module);
	return true;
}

TEST(BytecodeTupleUnpackIntoVariadicParameters) {
	const char* source = R"(
		fn sum(start : i32, values : ...i32) : i32 {
			var result = start;
			for value in values { result += value; }
			return result;
		}
		fn main() : i32 {
			const args : tuple { i32, i32, i32 } = .{ 10, 12, 20 };
			if sum(args...) != 42 { return 1; }
			return sum(0, args...);
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
