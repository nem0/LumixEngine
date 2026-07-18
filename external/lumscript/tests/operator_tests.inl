TEST(TernaryOperatorBasic) {
	const char* source = R"(
		fn main() : i32 {
			var x = 5;
			return x > 3 ? 10 : 20;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TernaryOperatorNested) {
	const char* source = R"(
		fn main() : i32 {
			var x = 5;
			return x > 10 ? 1 : x > 3 ? 2 : 3;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TernaryOperatorWithBool) {
	const char* source = R"(
		fn main() : bool {
			var cond = true;
			return cond ? true : false;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TernaryOperatorTypeMismatch) {
	const char* source = R"(
		fn main() : void {
			var x = 5;
			var result = x > 3 ? 10 : "string";
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TernaryOperatorNonBoolCondition) {
	const char* source = R"(
		fn main() : void {
			var result = 5 ? 10 : 20;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(OperatorOverloadsTypecheck) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator +(a : Vec2, b : Vec2) : Vec2 {
			return Vec2 { a.x + b.x, a.y + b.y };
		}

		operator -(a : Vec2) : Vec2 {
			return Vec2 { -a.x, -a.y };
		}

		fn main() : Vec2 {
			const a = Vec2 { 1.0, 2.0 };
			const b = Vec2 { 3.0, 4.0 };
			return -(a + b);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ImportedOperatorOverloadsTypecheck) {
	const char* main_source = R"(
		import "math"

		fn main() : Meters {
			const a = Meters { 1.0 };
			const b = Meters { 2.0 };
			return a + b;
		}
	)";
	const char* math_source = R"(
		struct Meters {
			value : f32;
		}

		operator +(a : Meters, b : Meters) : Meters {
			return Meters { a.value + b.value };
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportedOperatorOverloadsWithAliasTypecheck) {
	const char* main_source = R"(
		import "math" as math

		fn main() : math.Meters {
			const a = math.Meters { 1.0 };
			const b = math.Meters { 2.0 };
			return a + b;
		}
	)";
	const char* math_source = R"(
		struct Meters {
			value : f32;
		}

		operator +(a : Meters, b : Meters) : Meters {
			return Meters { a.value + b.value };
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ImportedOperatorPlusEqualTypecheck) {
	const char* main_source = R"(
		import "math"

		fn main() : void {
			var value = Meters { 1.0 };
			value += Meters { 2.0 };
		}
	)";
	const char* math_source = R"(
		struct Meters {
			value : f32;
		}

		operator +(a : Meters, b : Meters) : Meters {
			return Meters { a.value + b.value };
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(NonOverloadableOperatorFails) {
	const char* source = R"(
		fn main() : void {
		}

		operator and(a : bool, b : bool) : bool {
			return a and b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(PrimitiveOperatorOverloadFails) {
	const char* source = R"(
		operator +(a : f32, b : f32) : f32 {
			return a + b;
		}

		fn main() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NonMinusOperatorRequiresTwoParametersFails) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator +(a : Vec2) : Vec2 {
			return a;
		}

		fn main() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(OperatorDoesNotAllowImplicitCastFails) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator +(a : Vec2, b : Vec2) : Vec2 {
			return Vec2 { a.x + b.x, a.y + b.y };
		}

		fn main() : Vec2 {
			const a = Vec2 { 1.0, 2.0 };
			return a + 1.0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(OperatorOverloadAmbiguityFails) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator +(a : Vec2, b : Vec2) : Vec2 {
			return Vec2 { a.x + b.x, a.y + b.y };
		}

		operator +(a : Vec2, b : Vec2) : Vec2 {
			return Vec2 { a.x + b.x, a.y + b.y };
		}

		fn main() : Vec2 {
			const a = Vec2 { 1.0, 2.0 };
			return a + a;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EqualityOperatorOverloadAmbiguityFails) {
	const char* source = R"(
		struct Box {
			value : i32;
		}

		operator ==(a : Box, b : Box) : bool {
			return a.value == b.value;
		}

		operator ==(a : Box, b : Box) : bool {
			return a.value == b.value;
		}

		fn main() : bool {
			const value = Box { 1 };
			return value == value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RejectedOperatorCandidateDoesNotRetypeOperands) {
	// When a candidate is rejected during probing, the commit pass of the winning
	// candidate must correctly retype all operands regardless of stale probe state.
	const char* source = R"(
		struct FirstBox {
			value : i32;
		}

		struct SecondBox {
			value : i32;
		}

		operator +(a : FirstBox, b : i32) : i32 {
			return 77;
		}

		operator +(a : SecondBox, b : i32) : i32 {
			return b + 1;
		}

		fn main() : i32 {
			return FirstBox { 42 } + 5;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(77, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(RejectedSameHostOperatorCandidateDoesNotRetypeOperands) {
	// Both candidates are stored on Box. The rejected candidate must not undo the
	// i32 type selected for the literal by the winning overload.
	const char* source = R"(
		struct Box { value : i32; }
		struct Other { value : i32; }

		operator +(a : Box, b : i32) : i32 {
			return a.value + b;
		}

		operator +(a : Box, b : Other) : i32 {
			return 0;
		}

		fn main() : i32 {
			return Box { 40 } + 2;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(PrimitiveCompoundAssignmentIgnoresOperatorOverloadCompiles) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator +(a : Vec2, b : Vec2) : Vec2 {
			return Vec2 { a.x + b.x, a.y + b.y };
		}

		fn main() : void {
			var v : f32 = 1.0;
			v += 2.0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(CustomCompoundAssignmentAcceptsDifferentRhsType) {
	const char* source = R"(
		struct Box {
			value : i32;
		}

		operator +(box : Box, amount : i32) : Box {
			return Box { box.value + amount };
		}

		fn main() : void {
			var box = Box { 1 };
			box += 2;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(AllOverloadableOperatorsTypecheck) {
	const char* main_source = R"(
		struct Box {
			value : f32;
		}

		operator +(a : Box, b : Box) : Box {
			return Box { a.value + b.value };
		}

		operator -(a : Box, b : Box) : Box {
			return Box { a.value - b.value };
		}

		operator *(a : Box, b : Box) : Box {
			return Box { a.value * b.value };
		}

		operator /(a : Box, b : Box) : Box {
			return Box { a.value / b.value };
		}

		operator %(a : Box, b : Box) : Box {
			return a;
		}

		operator ==(a : Box, b : Box) : bool {
			return a.value == b.value;
		}

		operator !=(a : Box, b : Box) : bool {
			return a.value != b.value;
		}

		operator <(a : Box, b : Box) : bool {
			return a.value < b.value;
		}

		operator <=(a : Box, b : Box) : bool {
			return a.value <= b.value;
		}

		operator >(a : Box, b : Box) : bool {
			return a.value > b.value;
		}

		operator >=(a : Box, b : Box) : bool {
			return a.value >= b.value;
		}

		operator -(a : Box) : Box {
			return Box { -a.value };
		}

		fn main() : bool {
			const a = Box { 1.0 };
			const b = Box { 2.0 };
			const add = a + b;
			const sub = a - b;
			const mul = a * b;
			const div = a / b;
			const mod = a % b;
			const eq = a == b;
			const ne = a != b;
			const lt = a < b;
			const le = a <= b;
			const gt = a > b;
			const ge = a >= b;
			const neg = -a;
			return eq or ne or lt or le or gt or ge or (add == sub) or (mul == div) or (mod == neg);
		}
	)";
	EXPECT_COMPILE(main_source);
	return true;
}

TEST(BinaryNumericOperatorsRequireSameOperandType) {
	{
		const char* mixed_add = R"(
			fn main() : i32 {
				const a : i32 = 1;
				const b : i64 = 2;
				return a + b;
			}
		)";
		EXPECT_COMPILE_FAIL(mixed_add);

		const char* mixed_compare = R"(
			fn main() : bool {
				const a : i32 = 1;
				const b : f32 = 1;
				return a < b;
			}
		)";
		EXPECT_COMPILE_FAIL(mixed_compare);
	}

	{
		const char* explicit_cast_ok = R"(
			fn main() : i64 {
				const a : i32 = 1;
				const b : i64 = 2;
				return (a as i64) + b;
			}
		)";
		EXPECT_COMPILE(explicit_cast_ok);
	}
	return true;
}

TEST(ComparisonInfersLiteralOperandTypeBidirectionally) {
	// `x == 3` already compiles because the i64 lhs flows as the hint into the
	// literal. The flipped form must behave the same: a literal on the left
	// should infer its type from the non-literal operand on the right instead of
	// eagerly defaulting to i32. Currently red - operand inference is one-way.
	const char* source = R"(
		fn main() : bool {
			const x : i64 = 5;
			return 3 == x;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComparisonInfersFloatLiteralOperandTypeBidirectionally) {
	const char* source = R"(
		fn main() : bool {
			const x : f32 = 1.5;
			return 1.5 == x;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ModuloRequiresIntegerOperandsFails) {
	const char* source = R"(
		fn main() : void {
			const a : f32 = 5.0;
			const b : f32 = 2.0;
			const mod = a % b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BooleanNotRequiresBoolOperandFails) {
	const char* source = R"(
		fn main() : void {
			const value : i32 = 1;
			const negated = not value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnaryMinusRequiresNumericOperandFails) {
	const char* source = R"(
		fn main() : void {
			const value : string = "abc";
			const negated = -value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(CustomOperatorGlobalCompoundAssignmentRuntime) {
	const char* main_source = R"(
		struct Meters {
			value : f32;
		}

		operator +(a : Meters, b : Meters) : Meters {
			return Meters { a.value + b.value };
		}

		var total = Meters { 1.0 };

		fn main() : f32 {
			total += Meters { 2.0 };
			return total.value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(3.0f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(CustomOperatorBinaryRuntime) {
	const char* main_source = R"(
		struct Meters {
			value : f32;
		}

		operator +(a : Meters, b : Meters) : Meters {
			return Meters { a.value + b.value };
		}

		fn main() : Meters {
			const a = Meters { 1.0 };
			const b = Meters { 2.0 };
			return a + b;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(3.0f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(CustomOperatorBinarySubtractionRuntime) {
	const char* main_source = R"(
		struct Meters {
			value : i32;
		}

		operator -(a : Meters, b : Meters) : Meters {
			return Meters { a.value - b.value };
		}

		fn main() : i32 {
			const a = Meters { 10 };
			const b = Meters { 3 };
			return (a - b).value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(PrimitiveOperatorDeclarationFailsEvenUnused) {
	const char* source = R"(
		operator +(a : i32, b : i32) : i32 {
			return a + b;
		}

		fn main() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(CompoundAssignOnStructWithNoOverloadFails) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		fn main() : void {
			var v = Vec2 { 1.0, 2.0 };
			v += Vec2 { 3.0, 4.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumOperatorDeclarationFails) {
	// Operators with enum parameters are not allowed; use a wrapper struct instead.
	const char* source = R"(
		enum Direction { Up, Down }

		operator +(a : Direction, b : Direction) : Direction { return a; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MixedStructPrimitiveOperatorF32LiteralTypecheck) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator *(a : Vec2, b : f32) : Vec2 {
			return Vec2 { a.x * b, a.y * b };
		}

		fn main() : Vec2 {
			const v = Vec2 { 1.0, 2.0 };
			return v * 3.0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MixedStructPrimitiveOperatorI64LiteralTypecheck) {
	// 2 defaults to i32 without a hint; the i64 parameter hint must flip it to i64.
	const char* source = R"(
		struct Counter {
			value : i64;
		}

		operator +(a : Counter, b : i64) : Counter {
			return Counter { a.value + b };
		}

		fn main() : Counter {
			const c = Counter { 10 };
			return c + 5;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MixedStructPrimitiveOperatorRuntime) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator *(a : Vec2, b : f32) : Vec2 {
			return Vec2 { a.x * b, a.y * b };
		}

		fn main() : f32 {
			const v = Vec2 { 1.0, 2.0 };
			const scaled = v * 3.0;
			return scaled.y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(6.0f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(MixedStructPrimitiveOperatorWrongLiteralTypeFails) {
	// f64 literal used where the only overload expects f32 on the left — no match.
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator *(a : Vec2, b : f32) : Vec2 {
			return Vec2 { a.x * b, a.y * b };
		}

		fn main() : Vec2 {
			const v = Vec2 { 1.0, 2.0 };
			const scale : f64 = 3.0;
			return v * scale;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumOperatorMixedDeclarationFails) {
	// Enum parameter is disallowed even when the other parameter is a struct.
	const char* source = R"(
		struct Box { value : i32; }
		enum Direction { Up, Down }

		operator +(a : Direction, b : Box) : i32 { return 1; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumOperatorRhsDeclarationFails) {
	// Enum parameter on the right side is also disallowed.
	const char* source = R"(
		struct Box { value : i32; }
		enum Direction { Up, Down }

		operator +(a : Box, b : Direction) : i32 { return 1; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumUnaryOperatorDeclarationFails) {
	// Unary operators with an enum parameter are disallowed.
	const char* source = R"(
		enum Direction { Up, Down }

		operator -(a : Direction) : Direction { return a; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FunctionLiteralOperatorOperandFails) {
	// A function value can never match a struct operator parameter; resolution
	// must reject it cleanly without repeatedly checking the literal's body.
	const char* source = R"(
		struct Box { value : i32; }

		operator +(a : Box, b : Box) : Box {
			return a;
		}

		fn main() : void {
			const b = Box { 1 };
			const r = b + fn() : i32 { return 0; };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(PrimitiveLhsUserTypeRhsOperatorTypecheck) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator *(a : f32, b : Vec2) : Vec2 {
			return Vec2 { a * b.x, a * b.y };
		}

		fn main() : Vec2 {
			return 3.0 * Vec2 { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(PrimitiveLhsUserTypeRhsOperatorRuntime) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator *(a : f32, b : Vec2) : Vec2 {
			return Vec2 { a * b.x, a * b.y };
		}

		fn main() : f32 {
			const v = 3.0 * Vec2 { 1.0, 2.0 };
			return v.y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(6.0f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(MultiplePrimitiveLhsOverloadsOnSameTypeRuntime) {
	// Two primitive-side overloads on the same type; concrete typed variables must
	// select the right one via exact matching.
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator *(a : f32, b : Vec2) : Vec2 {
			return Vec2 { a * b.x, a * b.y };
		}

		operator *(a : i32, b : Vec2) : Vec2 {
			return Vec2 { a as f32 * b.x, a as f32 * b.y };
		}

		fn main() : f32 {
			const v = Vec2 { 1.0, 2.0 };
			const sf : f32 = 3.0;
			const si : i32 = 3;
			const by_float = sf * v;
			const by_int = si * v;
			return by_float.y + by_int.y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(12.0f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}


TEST(ImportedPrimitiveLhsUserTypeRhsOperatorTypecheck) {
	const char* main_source = R"(
		import "math"

		fn main() : Vec2 {
			return 3.0 * Vec2 { 1.0, 2.0 };
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator *(a : f32, b : Vec2) : Vec2 {
			return Vec2 { a * b.x, a * b.y };
		}
	)";
	LumScriptImportFile file = { toLs("math"), toLs(math_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(OperatorUsingAnotherOperator) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator -(a : Vec2, b : Vec2) : Vec2 {
			return a + Vec2 { -b.x, -b.y };
		}

		operator +(a : Vec2, b : Vec2) : Vec2 {
			return Vec2 { a.x + b.x, a.y + b.y };
		}

		fn main() : Vec2 {
			const a = Vec2 { 3.0, 5.0 };
			const b = Vec2 { 1.0, 2.0 };
			return a - b;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FunctionLiteralOperandBodyErrorIsReported) {
	// The function literal's body has a real type error (string returned as i32).
	// Operator resolution checks operands normally before inspecting candidates, so
	// the body error must be reported.
	const char* source = R"(
		struct Box { value : i32; }

		operator +(a : Box, b : Box) : Box {
			return a;
		}

		fn main() : void {
			const b = Box { 1 };
			const r = b + fn() : i32 { return "oops"; };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(CompoundAssignmentNonNumericRhsFails) {
	// `i32 += string` must be rejected: the numeric LHS fast-path in compound
	// assignment must still verify the rhs is convertible to the target type.
	const char* source = R"(
		fn main() : void {
			var x : i32 = 0;
			x += "hello";
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MemberCompoundAssignmentWithOperatorRuntime) {
	// Issue #1: member compound assignment should use resolved_op_fn for operator overloads.
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator +(a : Vec2, b : Vec2) : Vec2 {
			return Vec2 { a.x + b.x, a.y + b.y };
		}

		struct Container {
			v : Vec2;
		}

		fn main() : f32 {
			var obj = Container { Vec2 { 1.0, 2.0 } };
			obj.v += Vec2 { 3.0, 4.0 };
			return obj.v.x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(4.0f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(BracketCompoundAssignmentWithOperatorRuntime) {
	// Issue #1: bracket compound assignment should use resolved_op_fn for operator overloads.
	const char* source = R"(
		struct Meters {
			value : f32;
		}

		operator +(a : Meters, b : Meters) : Meters {
			return Meters { a.value + b.value };
		}

		fn main() : f32 {
			var boxes : [3]Meters = undefined;
			boxes[0] = Meters { 1.0 };
			boxes[0] += Meters { 10.0 };
			return boxes[0].value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(11.0f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TypelessStructLiteralOperatorOperandFails) {
	// Operator overload resolution never supplies a contextual struct type.
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		operator +(a : Vec2, b : Vec2) : Vec2 {
			return Vec2 { a.x + b.x, a.y + b.y };
		}

		fn main() : f32 {
			const v = Vec2 { 1.0, 2.0 };
			const r = v + { 3.0, 4.0 };
			return r.y;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TypelessStructLiteralLhsOperatorOperandFails) {
	const char* source = R"(
		struct Vec2 { x : f32; y : f32; }
		operator +(a : Vec2, b : Vec2) : Vec2 { return a; }
		fn main() : Vec2 { return { 1.0, 2.0 } + Vec2 { 3.0, 4.0 }; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

