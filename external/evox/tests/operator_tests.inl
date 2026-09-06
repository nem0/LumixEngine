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

TEST(TernaryUntypedBranchAdoptsConcreteBranchType) {
	const char* source = R"(
		fn main() : i32 {
			const x : i32 = 9;
			// the false branch stays untyped through the binary expression
			const a = false ? x : 2 + 3;
			const b = true ? 2 + 3 : x;
			return a + b;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(10, ex_to_i32(runtime, -1));
	CAPI_END(module);
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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

TEST(StructNonOverloadableOperatorFails) {
	const char* source = R"(
		struct Value {
			x : i32;
		}

		fn main() : void {
			const value = Value { 1 };
			const result = value and value;
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

TEST(SliceEqualityOperatorOverloadFails) {
	// Slice equality is built in and cannot be overridden, like primitive equality.
	const char* source = R"(
		operator ==(a : []const u8, b : []const u8) : bool {
			return false;
		}

		fn main() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceInequalityOperatorOverloadFails) {
	const char* source = R"(
		operator !=(a : []i32, b : []i32) : bool {
			return false;
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(77, ex_to_i32(runtime, -1));
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
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
			const value : []const u8 = "abc";
			const negated = -value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnaryMinusUnsignedFails) {
	const char* source = R"(
		fn main() : u8 {
			const value : u8 = 1;
			return -value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NotBindsLooserThanEquality) {
	const char* source = R"(
		fn main() : i32 {
			const a : bool = true;
			const b : bool = false;
			// not (a == b) is true; (not a) == b would be false
			return not a == b ? 1 : 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(1, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(NotBindsLooserThanComparison) {
	const char* source = R"(
		fn main() : i32 {
			const a : i32 = 1;
			const b : i32 = 2;
			// not (a < b) is false; (not a) < b would not compile
			return not a < b ? 1 : 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(0, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(NotBindsTighterThanAnd) {
	const char* source = R"(
		fn main() : i32 {
			const a : bool = false;
			const b : bool = true;
			// (not a) and (not b) is false; not (a and (not b)) would be true
			return not a and not b ? 1 : 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(0, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(NotBindsTighterThanAndOnRight) {
	const char* source = R"(
		fn main() : i32 {
			const a : bool = true;
			const b : bool = false;
			// a and (not b) is true
			return a and not b ? 1 : 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(1, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(NotIsRightAssociative) {
	const char* source = R"(
		fn main() : i32 {
			const ready : bool = true;
			// not (not ready) is true
			return not not ready ? 1 : 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(1, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(NotBindsLooserThanArithmeticFails) {
	const char* source = R"(
		fn main() : void {
			const a : i32 = 1;
			const b : i32 = 2;
			// parses as not (a + b); the operand is i32, not bool
			const negated = not a + b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

#define EXPECT_EXPR_I32(expr, expected) \
	do { \
		const char* source = "fn main() : i32 { return " expr "; }"; \
		CAPI_BEGIN(module, diagnostics); \
		EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr)); \
		CAPI_RUNTIME(module, runtime); \
		EXPECT_TRUE(ex_call(runtime, toLs("main"))); \
		EXPECT_EQ(expected, ex_to_i32(runtime, -1)); \
		CAPI_END(module); \
	} while (false)

TEST(MultiplicativeBindsTighterThanAdditive) {
	EXPECT_EXPR_I32("2 + 3 * 4", 14);			// (2 + 3) * 4 would be 20
	EXPECT_EXPR_I32("3 * 4 + 2", 14);
	EXPECT_EXPR_I32("20 - 12 / 4", 17);			// (20 - 12) / 4 would be 2
	EXPECT_EXPR_I32("1 + 7 % 4", 4);			// (1 + 7) % 4 would be 0
	return true;
}

TEST(AdditiveIsLeftAssociative) {
	EXPECT_EXPR_I32("10 - 3 - 2", 5);			// 10 - (3 - 2) would be 9
	return true;
}

TEST(MultiplicativeIsLeftAssociative) {
	EXPECT_EXPR_I32("100 / 5 / 2", 10);			// 100 / (5 / 2) would be 50
	EXPECT_EXPR_I32("13 % 7 * 2", 12);			// 13 % (7 * 2) would be 13
	return true;
}

TEST(UnaryMinusBindsTighterThanBinaryOperators) {
	EXPECT_EXPR_I32("-2 + 3", 1);				// -(2 + 3) would be -5
	EXPECT_EXPR_I32("7 + -2 * 3", 1);			// operand of * is -2, not 2
	return true;
}

TEST(ParenthesesOverridePrecedence) {
	EXPECT_EXPR_I32("(2 + 3) * 4", 20);
	EXPECT_EXPR_I32("10 - (3 - 2)", 9);
	return true;
}

TEST(ArithmeticBindsTighterThanComparison) {
	// (1 + 1) < 3 is true; comparing first would not typecheck
	EXPECT_EXPR_I32("1 + 1 < 3 ? 1 : 0", 1);
	EXPECT_EXPR_I32("1 < 1 + 3 ? 1 : 0", 1);
	return true;
}

TEST(ComparisonBindsTighterThanEquality) {
	// (1 < 2) == true is true; 1 < (2 == true) would not typecheck
	EXPECT_EXPR_I32("1 < 2 == true ? 1 : 0", 1);
	return true;
}

TEST(EqualityBindsTighterThanAnd) {
	// true and (1 == 1) is true; (true and 1) == 1 would not typecheck
	EXPECT_EXPR_I32("true and 1 == 1 ? 1 : 0", 1);
	return true;
}

TEST(AndBindsTighterThanOr) {
	// true or (false and false) is true; (true or false) and false would be false
	EXPECT_EXPR_I32("true or false and false ? 1 : 0", 1);
	// (false and false) or true is true
	EXPECT_EXPR_I32("false and false or true ? 1 : 0", 1);
	return true;
}

TEST(TernaryBindsLooserThanArithmetic) {
	// true ? 1 : (2 + 3) is 1; (true ? 1 : 2) + 3 would be 4
	EXPECT_EXPR_I32("true ? 1 : 2 + 3", 1);
	// (1 + 1 == 2) ? 7 : 8 is 7
	EXPECT_EXPR_I32("1 + 1 == 2 ? 7 : 8", 7);
	return true;
}

TEST(TernaryIsRightAssociative) {
	// false ? 1 : (false ? 2 : 3) is 3
	EXPECT_EXPR_I32("false ? 1 : false ? 2 : 3", 3);
	EXPECT_EXPR_I32("false ? 1 : true ? 2 : 3", 2);
	return true;
}

#undef EXPECT_EXPR_I32

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
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(3.0f, ex_to_f32(runtime, -1));
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
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(3.0f, ex_to_f32(runtime, -1));
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
	EXPECT_TRUE(ex_module_compile(module, toLs(main_source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(7, ex_to_i32(runtime, -1));
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(6.0f, ex_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(MixedStructPrimitiveOperatorWrongLiteralTypeFails) {
	// f64 literal used where the only overload expects f32 on the left - no match.
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(6.0f, ex_to_f32(runtime, -1));
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(12.0f, ex_to_f32(runtime, -1));
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
	EvoxImportFile file = { toLs("math"), toLs(math_source) };
	EvoxImportFiles files = { &file, 1 };
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(4.0f, ex_to_f32(runtime, -1));
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(11.0f, ex_to_f32(runtime, -1));
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


TEST(ComptimeOnlyOperatorOperandFails) {
	const char* source = R"(
		struct Descriptor { value_type : type; }

		operator +(a : Descriptor, b : Descriptor) : i32 {
			return 1;
		}

		comptime first = Descriptor { i32 };
		comptime second = Descriptor { f32 };

		fn main() : i32 {
			return first + second;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeOnlyIndexedOperatorOperandFails) {
	const char* source = R"(
		struct Descriptor { value_type : type; }

		operator +(a : Descriptor, b : Descriptor) : i32 {
			return 1;
		}

		comptime descriptors = [
			Descriptor { i32 },
			Descriptor { f32 }
		];

		fn main() : i32 {
			return descriptors[0] + descriptors[1];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceEqualityReturnsContentComparison) {
	// Slices now have content equality. This used to assert a compile error, and
	// before that the generic comparison silently compared nothing and always
	// returned true - so check the value, not just that it compiles.
	const char* source = R"(
		fn main() : i32 {
			var a : [2]i32 = undefined;
			a[0] = 1; a[1] = 2;
			var b : [2]i32 = undefined;
			b[0] = 1; b[1] = 3;
			var s1 : []i32 = a[:];
			var s2 : []i32 = a[:];
			var s3 : []i32 = b[:];
			if s1 == s2 and s1 != s3 { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ArrayEqualityFails) {
	const char* source = R"(
		fn main() : bool {
			var a : [2]i32 = undefined;
			var b : [2]i32 = undefined;
			return a == b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableEqualityAgainstNullableFails) {
	// Nullable values compare only against the null literal.
	const char* source = R"(
		fn main() : bool {
			var a : ?i32 = 1;
			var b : ?i32 = 2;
			return a == b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableEqualityAgainstNullLiteralRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var a : ?i32 = 1;
			var b : ?i32 = null;
			var total : i32 = 0;
			if a != null {
				total += 1;
			}
			if b == null {
				total += 41;
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Compiles `source` with the optimizer enabled and reports whether any
// function's bytecode stream contains `op`. Probe modules hold one tiny
// straight-line function with runtime-typed parameters (so nothing folds at
// compile time), which keeps a raw byte scan free of meaningful collisions
// with immediate payloads.
static bool anyFunctionContainsOpcode(const char* source, ex_op op, const char* test_name) {
	TestContext context;
	ex_module* module = ex_module_create(&context.host);
	if (!module) return false;
	if (!ex_module_compile(module, toLs(source), makeStringView(test_name), nullptr, nullptr)) return false;
	ex_bytecode_compile_options options = {};
	options.optimize = true;
	ex_bytecode* bytecode = ex_bytecode_compile(module, &context.host, &options);
	if (!bytecode) return false;
	bool found = false;
	for (u32 i = 0; i < bytecode->function_count && !found; ++i) {
		const ex_function_bc& fn = bytecode->functions[i];
		for (u32 j = 0; j < fn.code_size; ++j) {
			if (fn.code[j] == (u8)op) {
				found = true;
				break;
			}
		}
	}
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(module);
	return found;
}

TEST(FusedMsubEmitted) {
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f64) : f64 { return x * 2.0 - 1.5; }
	)", EX_OP_MSUB_F64, __func__));
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f32) : f32 { return x * 2.0 - 1.5; }
	)", EX_OP_MSUB_F32, __func__));
	return true;
}

TEST(FusedNmaddEmitted) {
	// c - a*b and -(a*b) + c share the NMADD opcode.
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f64) : f64 { return 1.5 - x * 2.0; }
	)", EX_OP_NMADD_F64, __func__));
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f64) : f64 { return -(x * 2.0) + 1.5; }
	)", EX_OP_NMADD_F64, __func__));
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f32) : f32 { return 1.5 - x * 2.0; }
	)", EX_OP_NMADD_F32, __func__));
	return true;
}

TEST(FusedNmsubEmitted) {
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f64) : f64 { return -(x * 2.0) - 1.5; }
	)", EX_OP_NMSUB_F64, __func__));
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f32) : f32 { return -(x * 2.0) - 1.5; }
	)", EX_OP_NMSUB_F32, __func__));
	return true;
}

TEST(FusedNegatedSubtractCollapsesToMadd) {
	// c - -(a*b) is a*b + c: no NEG instruction needed.
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f64) : f64 { return 1.5 - -(x * 2.0); }
	)", EX_OP_MADD_F64, __func__));
	return true;
}

TEST(FusedMandelShapeEmitted) {
	// x*x - y*(-y) folds both multiplies into one ternary with no NEG left.
	EXPECT_TRUE(anyFunctionContainsOpcode(R"(
		fn probe(x : f64, y : f64) : f64 { return x * x - y * (-y); }
	)", EX_OP_MADD_F64, __func__));
	return true;
}

TEST(NegationPushThroughAddSubRuntime) {
	const char* source = R"(
		fn add_neg_rhs(x : f64) : f64 { return 10.0 + -x; }
		fn add_neg_lhs(x : f64) : f64 { return -x + 10.0; }
		fn sub_neg_rhs(x : f64) : f64 { return 10.0 - -x; }
		fn sub_negated_mul(x : f64) : f64 { return -(x * 4.0) - 2.0; }
		fn mandel_check(x : f64, y : f64) : f64 { return x * x - y * (-y); }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	ex_bytecode_compile_options options = {};
	options.optimize = true;
	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, &options);
	EXPECT_TRUE(bytecode != nullptr);
	if (bytecode) {
		ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
		EXPECT_TRUE(runtime != nullptr);
		if (runtime) {
			struct Case {
				const char* name;
				double arg;
				double expected;
			};
			const Case cases[] = {
				{"add_neg_rhs", 3.0, 7.0},
				{"add_neg_lhs", 3.0, 7.0},
				{"sub_neg_rhs", 3.0, 13.0},
				{"sub_negated_mul", 3.0, -14.0},
			};
			for (const Case& c : cases) {
				ex_push_f64(runtime, c.arg);
				EXPECT_TRUE(ex_call(runtime, toLs(c.name)));
				EXPECT_FLOAT_EQ(c.expected, ex_to_f64(runtime, -1));
			}
			ex_push_f64(runtime, 3.0);
			ex_push_f64(runtime, 4.0);
			EXPECT_TRUE(ex_call(runtime, toLs("mandel_check")));
			EXPECT_FLOAT_EQ(25.0, ex_to_f64(runtime, -1));
		}
		ex_bytecode_destroy(bytecode);
	}
	CAPI_END(module);
	return true;
}

TEST(FusedMultiplyOpsRuntime) {
	const char* source = R"(
		fn msub_f64(x : f64) : f64 { return x * 4.0 - 2.0; }
		fn nmadd_sub_form(x : f64) : f64 { return 2.0 - x * 4.0; }
		fn nmadd_neg_form(x : f64) : f64 { return -(x * 4.0) + 2.0; }
		fn nmsub_f64(x : f64) : f64 { return -(x * 4.0) - 2.0; }
		fn negated_collapse(x : f64) : f64 { return 2.0 - -(x * 4.0); }
		fn plain_madd(x : f64) : f64 { return x * 4.0 + 2.0; }
		fn both_sides_mul(x : f64) : f64 { return x * 4.0 - x * 2.0; }
		fn msub_f32(x : f32) : f32 { return x * 4.0 - 2.0; }
		fn nmadd_f32(x : f32) : f32 { return 2.0 - x * 4.0; }
		fn nmsub_f32(x : f32) : f32 { return -(x * 4.0) - 2.0; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	ex_bytecode_compile_options options = {};
	options.optimize = true;
	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, &options);
	EXPECT_TRUE(bytecode != nullptr);
	if (bytecode) {
		ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
		EXPECT_TRUE(runtime != nullptr);
		if (runtime) {
			struct F64Case {
				const char* name;
				double arg;
				double expected;
			};
			const F64Case f64_cases[] = {
				{"msub_f64", 3.0, 10.0},
				{"nmadd_sub_form", 3.0, -10.0},
				{"nmadd_neg_form", 3.0, -10.0},
				{"nmsub_f64", 3.0, -14.0},
				{"negated_collapse", 3.0, 14.0},
				{"plain_madd", 3.0, 14.0},
				{"both_sides_mul", 3.0, 6.0},
			};
			for (const F64Case& c : f64_cases) {
				ex_push_f64(runtime, c.arg);
				EXPECT_TRUE(ex_call(runtime, toLs(c.name)));
				EXPECT_FLOAT_EQ(c.expected, ex_to_f64(runtime, -1));
			}
			struct F32Case {
				const char* name;
				float arg;
				float expected;
			};
			const F32Case f32_cases[] = {
				{"msub_f32", 3.0f, 10.0f},
				{"nmadd_f32", 3.0f, -10.0f},
				{"nmsub_f32", 3.0f, -14.0f},
			};
			for (const F32Case& c : f32_cases) {
				ex_push_f32(runtime, c.arg);
				EXPECT_TRUE(ex_call(runtime, toLs(c.name)));
				EXPECT_FLOAT_EQ(c.expected, ex_to_f32(runtime, -1));
			}
		}
		ex_bytecode_destroy(bytecode);
	}
	CAPI_END(module);
	return true;
}
