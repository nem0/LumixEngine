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
