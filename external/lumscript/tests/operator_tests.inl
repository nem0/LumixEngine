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

TEST(RejectedOperatorCandidateDoesNotRetypeOperands) {
	const char* source = R"(
		enum First {
			Value
		}

		enum Second {
			Value
		}

		struct FirstBox {
			value : i32;
		}

		struct SecondBox {
			value : i32;
		}

		operator +(value : First, box : FirstBox) : i32 {
			return 77;
		}

		operator +(value : Second, box : SecondBox) : i32 {
			return box.value + 1;
		}

		fn main() : i32 {
			return .Value + FirstBox { 42 };
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(77, ls_to_i32(runtime, -1));
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
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, nullptr, nullptr));
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
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, nullptr, nullptr));
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
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, nullptr, nullptr));
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
