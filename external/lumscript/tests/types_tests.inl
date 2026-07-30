TEST(ExtendedScalarTypesTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			const a : i8 = 10;
			const b : u8 = 20;
			const c : i16 = 30;
			const d : u16 = 40;
			const e : i64 = 50;
			const f : u64 = 60;
			const g : f64 = 1;
			return a as i32 + b as i32 + c as i32 + d as i32 + e as i32 + f as i32 + g as i32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedLiteralsUseExpectedTypes) {
	const char* source = R"(
		struct Pair {
			x : u8;
			y : f64;
		}

		fn takes_f32(v : f32) : f32 {
			return v;
		}

		fn takes_i16(v : i16) : i16 {
			return v;
		}

		fn returns_i64() : i64 {
			return 42;
		}

		fn main() : f32 {
			const a : i8 = 10;
			const b : u16 = 20;
			const c : f64 = 1.5;
			const d = Pair { 255, 2.5 };
			return takes_f32(12) + takes_i16(3) as f32 + d.y as f32 + returns_i64() as f32 + a as f32 + b as f32 + c as f32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedConstantExpressionsUseExpectedTypes) {
	const char* source = R"(
		fn takes_f32(v : f32) : f32 {
			return v;
		}

		fn returns_i16() : i16 {
			return 10 + 20;
		}

		fn main() : f32 {
			const value : f32 = 12 + 13;
			return value + takes_f32(20 + 5) + returns_i16() as f32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedLiteralsConcretizeInAllContexts) {
	const char* source = R"(
		struct Pair { value : i16; }
		struct Box { value : i16; }

		operator +(lhs : Box, rhs : i16) : i16 { return lhs.value + rhs; }
		fn takes_i16(value : i16) : i16 { return value; }
		fn takes_comptime(value : comptime i16) : i16 { return value; }
		fn returns_i16() : i16 { return 1; }
		fn identity(value : $T) : T { return value; }

		comptime global_value : i16 = 1;
		comptime Number = i32 | string;

		fn main() : i32 {
			var assigned : i16 = 0;
			assigned = 2;
			const annotated : i16 = 3;
			comptime local_value : i16 = 4;
			const array : [2]i16 = [5, 6];
			const pair = Pair { 7 };
			const call = takes_i16(8);
			const comptime_call = takes_comptime(9);
			const returned = returns_i16();
			const wide : i64 = 10;
			const arithmetic = wide + 11;
			const conditional = true ? wide : 12;
			const overloaded = Box { 13 } + 14;
			const cast = 15 as i16;
			const inferred = identity(16);
			const default_value = 17;
			const union_value : Number = 18;

			for index in 0..length(array) {
				if typeof(index) != isize { var impossible : MissingType = undefined; }
			}
			match wide {
				case 10: {}
				case: {}
			}

			if typeof(global_value) != i16 { var impossible : MissingType = undefined; }
			if typeof(assigned) != i16 { var impossible : MissingType = undefined; }
			if typeof(annotated) != i16 { var impossible : MissingType = undefined; }
			if typeof(local_value) != i16 { var impossible : MissingType = undefined; }
			if typeof(array[0]) != i16 { var impossible : MissingType = undefined; }
			if typeof(pair.value) != i16 { var impossible : MissingType = undefined; }
			if typeof(call) != i16 { var impossible : MissingType = undefined; }
			if typeof(comptime_call) != i16 { var impossible : MissingType = undefined; }
			if typeof(returned) != i16 { var impossible : MissingType = undefined; }
			if typeof(arithmetic) != i64 { var impossible : MissingType = undefined; }
			if typeof(conditional) != i64 { var impossible : MissingType = undefined; }
			if typeof(overloaded) != i16 { var impossible : MissingType = undefined; }
			if typeof(cast) != i16 { var impossible : MissingType = undefined; }
			if typeof(inferred) != i32 { var impossible : MissingType = undefined; }
			if typeof(default_value) != i32 { var impossible : MissingType = undefined; }
			if typeof(union_value) != Number { var impossible : MissingType = undefined; }
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const ambiguous : i32 | i64 = 1;
		}
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const ambiguous : i8 | i16 = 255;
		}
	)");
	return true;
}

TEST(UntypedLiteralsConcretizeInAllContextsWithUnion) {
	EXPECT_COMPILE(R"(
		fn main() : void {
			const u : i8 | bool = 1;
		}
	)");

	return true;
}

TEST(ComptimeUntypedIntegerMustFitAnnotatedType) {
	EXPECT_COMPILE_FAIL(R"(
		comptime value : i8 = 128;
	)");
	return true;
}

TEST(ComptimeUntypedIntegerUnionMustBeUnambiguous) {
	EXPECT_COMPILE_FAIL(R"(
		comptime value : i8 | i16 = 1;
	)");
	return true;
}

TEST(UntypedComptimeBindingsRemainUntypedUntilConsumed) {
	const char* source = R"(
		comptime integer = 12;
		comptime decimal = 1.5;

		fn as_i8() : i8 { return integer; }
		fn as_i64() : i64 { return integer; }
		fn as_f32() : f32 { return decimal; }
		fn as_f64() : f64 { return decimal; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("as_i8")));
	EXPECT_EQ(12, ls_to_i8(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("as_i64")));
	EXPECT_EQ(12, ls_to_i64(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("as_f32")));
	EXPECT_FLOAT_EQ(1.5f, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("as_f64")));
	EXPECT_FLOAT_EQ(1.5, ls_to_f64(runtime, -1));
	CAPI_END(module);

	EXPECT_COMPILE_FAIL(R"(
		comptime integer = 12;
		comptime T = typeof(integer);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime decimal = 1.5;
		comptime T = typeof(decimal);
	)");
	return true;
}

TEST(UntypedNumericExpressionsUseDefaultTypes) {
	const char* integer_source = R"(
		fn takes_i64(v : i64) : void {
		}

		fn main() : void {
			const value = 12 + 13;
			takes_i64(value);
		}
	)";
	EXPECT_COMPILE_FAIL(integer_source);

	const char* decimal_source = R"(
		fn takes_f64(v : f64) : void {
		}

		fn main() : void {
			const value = 1.5;
			takes_f64(value);
		}
	)";
	EXPECT_COMPILE(decimal_source);

	const char* decimal_is_not_f32 = R"(
		fn takes_f32(v : f32) : void {
		}

		fn main() : void {
			const value = 1.5;
			takes_f32(value);
		}
	)";
	EXPECT_COMPILE_FAIL(decimal_is_not_f32);
	return true;
}

TEST(UntypedIntegerLiteralsInferI64ForLargeValues) {
	const char* source = R"(
		fn takes_i64(v : i64) : void {
		}

		fn main() : void {
			const value = 2147483648;
			const negative = -2147483649;
			takes_i64(value);
			takes_i64(negative);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedIntegerExpressionsInferI64ForLargeValues) {
	const char* source = R"(
		fn takes_i64(v : i64) : void {
		}

		fn main() : void {
			const value = 2147483648 + 1;
			takes_i64(value);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedIntegerLiteralI64MinCompiles) {
	const char* source = R"(
		fn takes_i64(v : i64) : void {
		}

		fn main() : void {
			const value = -9223372036854775808;
			takes_i64(value);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedIntegerLiteralU64MaxComparisonRejectsNegativeLiteral) {
	const char* source = R"(
		fn main() : bool {
			return 18446744073709551615 == -1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UntypedIntegerLiteralU64MaxComparisonWithExplicitCast) {
	const char* source = R"(
		fn main() : bool {
			return 18446744073709551615 == (-1 as u64);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedIntegerLiteralU64MaxCompiles) {
	const char* source = R"(
		fn main() : void {
			const x = 18446744073709551615;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UntypedIntegerLiteralTooLargeForU64Fails) {
	const char* source = R"(
		fn main() : void {
			const x = 18446744073709551616;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UntypedIntegerConstantMustFitSignedType) {
	const char* source = R"(
		fn main() : i8 {
			return 128;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UntypedNegativeIntegerDoesNotFitUnsignedType) {
	const char* source = R"(
		fn main() : u8 {
			return -1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UntypedIntegerMustBeExactlyRepresentableAsFloat) {
	const char* source = R"(
		fn main() : f32 {
			return 16777217;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UntypedNegativeIntegerMustBeExactlyRepresentableAsFloat) {
	const char* source = R"(
		fn main() : f32 {
			return -16777217;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UntypedIntegerMustBeExactlyRepresentableAsDouble) {
	const char* source = R"(
		fn main() : f64 {
			return 9007199254740993;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UntypedNegativeIntegerMustBeExactlyRepresentableAsDouble) {
	const char* source = R"(
		fn main() : f64 {
			return -9007199254740993;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UntypedFloatConstantMustFitExpectedType) {
	const char* source = R"(
		fn main() : f32 {
			return 340282400000000000000000000000000000000.0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DecimalLiteralDoesNotConcretizeToInteger) {
	const char* source = R"(
		fn main() : i32 {
			const a : i32 = 1.5;
			return a;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// A struct field whose type is a function returning a nullable of that same
// struct is a valid reference cycle (the function holds a pointer-sized ref,
// not an inline layout cycle). resolveSignature must not treat this as a
// definition cycle.
TEST(SelfReferentialStructFunctionFieldCompiles) {
	const char* source = R"(
		struct Node {
			next : fn() : ?Node;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FunctionTypeNamedParametersCompile) {
	const char* source = R"(
		comptime Handler = fn(ref i32, comptime type, value : i32, f32, enabled : bool) : void;

		struct Callbacks {
			unary : fn(value : i32) : i32;
			mixed : fn(left : i32, f32, right : bool) : void;
		}

		fn identity(value : i32) : i32 {
			return value;
		}

		fn main() : i32 {
			const callback : fn(argument : i32) : i32 = identity;
			return callback(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FunctionTypeRefQualifierIsPartOfType) {
	const char* source = R"(
		fn set(value : ref i32) : void { value = 7; }
		const callback : fn(value : i32) : void = set;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FunctionTypeRefQualifierIndirectCall) {
	const char* source = R"(
		fn set(value : ref i32) : void { value = 7; }
		const callback : fn(value : ref i32) : void = set;

		fn main() : i32 {
			var value : i32 = 1;
			callback(ref value);
			return value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(IndirectByValueStructRecursionFails) {
	const char* source = R"(
		struct A {
			b : B;
		}

		struct B {
			a : A;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullCanInitializeCPtr) {
	const char* source = R"(
		fn main() : cptr {
			return null;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullCPtrRuntime) {
	const char* source = R"(
		fn main() : cptr {
			return null;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_to_ptr(runtime, -1) == nullptr);
	CAPI_END(module);
	return true;
}
