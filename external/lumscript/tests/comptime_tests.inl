TEST(ComptimeBasic) {
	const char* source = R"(
		comptime N = 32;
		comptime enabled = true;
		comptime scale = 1.5;
		comptime Name = "Lumix";
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeFunction) {
	const char* source = R"(
		comptime foo = fn() : void {};
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeBinaryExpression) {
	const char* source = R"(
		comptime N = 32 - 2;
		comptime enabled = 2 > 1;
		comptime scale = 1.5 * 2.7;
	)";
	EXPECT_COMPILE(source);
	return true;
}



TEST(ComptimePrimitiveValueTypechecks) {
	const char* source = R"(
		comptime N = 32;
		comptime enabled = true;
		comptime scale = 1.5;

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeStringValueTypechecks) {
	const char* source = R"(
		comptime Name = "Lumix";

		fn main() : string {
			return Name;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeBoolValueTypechecks) {
	const char* source = R"(
		comptime Enabled = true;

		fn main() : bool {
			return Enabled;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeFloatValueTypechecks) {
	const char* source = R"(
		comptime Scale = 1.5;

		fn main() : f64 {
			return Scale;
		}
	)";
	EXPECT_COMPILE(source);

	const char* does_not_implicitly_convert = R"(
		comptime Scale = 1.5;

		fn main() : f32 {
			return Scale;
		}
	)";
	EXPECT_COMPILE_FAIL(does_not_implicitly_convert);
	return true;
}

TEST(ComptimeAnnotatedPrimitiveValueTypechecks) {
	const char* source = R"(
		comptime N : i32 = 32;

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeAnnotatedPrimitiveTypeMismatchFails) {
	const char* source = R"(
		comptime N : i32 = 1.5;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimePrimitiveValueCanBeStaticArraySize) {
	const char* source = R"(
		comptime N = 4;

		fn main() : void {
			var values : [N]i32 = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeTypeBindingTypechecks) {
	const char* source = R"(
		comptime Int = i32;

		fn main() : Int {
			return 42;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeEnumBindingTypechecks) {
	const char* source = R"(
		comptime State = enum {
			Idle,
			Running
		}

		fn main() : State {
			return State.Idle;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeStructBindingTypechecks) {
	const char* source = R"(
		comptime Vec2 = struct {
			x : f32;
			y : f32;
		}

		fn main() : Vec2 {
			return Vec2 { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeFunctionReturningTypeFails) {
	const char* source = R"(
		comptime make_vec2 = fn(T : type) : type {
			return struct {
				x : T;
				y : T;
			};
		}

		comptime Vec2 = make_vec2(f32);

		fn main() : Vec2 {
			return Vec2 { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeFunctionBindingTypechecks) {
	const char* source = R"(
		comptime add = fn(a : i32, b : i32) : i32 {
			return a + b;
		}

		fn main() : i32 {
			return add(20, 22);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeNestedExpressionTypechecks) {
	const char* source = R"(
		comptime A = 20;
		comptime B = A + 22;

		fn main() : i32 {
			return B;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeInitializerCanCallTopLevelFunction) {
	const char* source = R"(
		fn double(v : i32) : i32 {
			return v * 2;
		}

		comptime N = double(16);

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeInitializerCanCallComptimeFunctionBinding) {
	const char* source = R"(
		comptime double = fn(v : i32) : i32 {
			return v * 2;
		}

		comptime N = double(16);

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeFunctionCanCallOtherComptimeFunction) {
	const char* source = R"(
		fn double(v : i32) : i32 {
			return v * 2;
		}

		fn quadruple(v : i32) : i32 {
			return double(double(v));
		}

		comptime N = quadruple(8);

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeInitializerCanNotCallTypeProducingFunction) {
	const char* source = R"(
		fn make_vec2(T : type) : type {
			return struct {
				x : T;
				y : T;
			};
		}

		comptime Vec2 = make_vec2(f32);

		fn main() : Vec2 {
			return Vec2 { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeCallWithRuntimeArgumentFails) {
	const char* source = R"(
		fn double(v : i32) : i32 {
			return v * 2;
		}

		var runtime_value : i32 = 16;
		comptime N = double(runtime_value);

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeCallCanNotCallExternFunctionFails) {
	const char* source = R"(
		extern fn native_value() : i32;

		comptime N = native_value();

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeDuplicateNameFails) {
	const char* source = R"(
		comptime N = 1;
		comptime N = 2;

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeCanNotBeReassignedFails) {
	const char* source = R"(
		comptime N = 1;

		fn main() : void {
			N = 2;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeInitializerMustBeComptimeFails) {
	const char* source = R"(
		var runtime_value : i32 = 1;
		comptime N = runtime_value;

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeLocalDeclarationFails) {
	const char* source = R"(
		fn main() : void {
			comptime N = 32;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(ComptimeTypeLiteralInRuntimeContextFails) {
	const char* source = R"(
		fn main() : type {
			return i32;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeRuntimeValueAsTypeFails) {
	const char* source = R"(
		fn main() : void {
			var T = i32;
			var value : T = 42;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeUnresolvedNameFails) {
	const char* source = R"(
		comptime N = Missing;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeImportedValueVisibleWithAlias) {
	const char* main_source = R"(
		import "constants" as constants;

		fn main() : i32 {
			return constants.N;
		}
	)";

	const LumScriptImportFile files[] = {
		{ toLs("constants"), toLs("comptime N = 32;") }
	};
	LumScriptImportFiles imports = { files, lengthOf(files) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, imports);
	return true;
}

TEST(ComptimeImportedTypeVisibleWithAlias) {
	const char* main_source = R"(
		import "types" as types

		fn main() : types.Int {
			return 42;
		}
	)";

	const LumScriptImportFile files[] = {
		{ toLs("types"), toLs("comptime Int = i32;") }
	};
	LumScriptImportFiles imports = { files, lengthOf(files) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, imports);
	return true;
}

TEST(ComptimeImportedValueNotVisibleWithoutImportFails) {
	const char* source = R"(
		fn main() : i32 {
			return constants.N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeIfPrunesUnselectedArm) {
	const char* source = R"(
		fn main() : void {
			if false {
				var impossible : MissingType = undefined;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeIfTemplateSpecializationPrunesUnselectedArm) {
	const char* source = R"(
		fn write(value : $T) : void {
			if T == i32 {
				var number : i32 = value;
			} else {
				var text : string = value;
			}
		}

		fn main() : void {
			write(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeIfElseIfAndElsePruneUnselectedArms) {
	const char* source = R"(
		comptime enabled = false;
		fn main() : i32 {
			if enabled {
				var bad : MissingType = undefined;
			} else if true {
				return 7;
			} else {
				var also_bad : MissingType = undefined;
			}
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeMatchPrunesUnselectedCase) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime state = State.Idle;

		fn main() : i32 {
			match state {
				case .Idle:
					return 1;
				case .Running:
					var bad : MissingType = undefined;
				case:
					var also_bad : MissingType = undefined;
			}
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(RuntimeIfChecksBothArms) {
	const char* source = R"(
		fn main(flag : bool) : void {
			if flag {
				var ok : i32 = 1;
			} else {
				var bad : MissingType = undefined;
			}
		}
	)";
	// A runtime condition must check both arms even when a call site passes a constant.
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeIfSelectedArmExecutes) {
	const char* source = R"(
		comptime enabled = true;
		fn main() : i32 {
			var result : i32 = 0;
			if enabled {
				result = 7;
			} else {
				var bad : MissingType = undefined;
			}
			return result;
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

TEST(TemplateRuntimeIfDoesNotBecomeComptime) {
	const char* source = R"(
		fn choose(T : comptime type, flag : bool) : void {
			if flag {
				var ok : T = undefined;
			} else {
				var bad : MissingType = undefined;
			}
		}

		fn main() : void {
			choose(i32, true);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeIfFunctionCallConditionPrunesArm) {
	const char* source = R"(
		fn enabled() : bool { return true; }
		comptime flag = enabled();

		fn main() : i32 {
			if flag {
				return 3;
			} else {
				var bad : MissingType = undefined;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeValueParameterIfPrunesArm) {
	const char* source = R"(
		fn choose(flag : comptime bool) : i32 {
			if flag {
				return 5;
			} else {
				var bad : MissingType = undefined;
			}
		}

		fn main() : i32 {
			return choose(true);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeIfRejectsSyntacticallyInvalidUnselectedArm) {
	const char* source = R"(
		fn main() : void {
			if false {
				var broken = ;
			}
		}
	)";
	// Unselected arms are not type-checked, but they must still be syntactically valid.
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeIfPrunesReturnInUnselectedArm) {
	const char* source = R"(
		fn main() : i32 {
			if false {
				return "wrong type";
			}
			return 9;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeIfNestedPruning) {
	const char* source = R"(
		comptime outer = true;
		comptime inner = false;
		fn main() : i32 {
			if outer {
				if inner {
					var bad : MissingType = undefined;
				} else {
					return 11;
				}
			} else {
				var also_bad : MissingType = undefined;
			}
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeMatchCommaAlternativesAndFallback) {
	const char* source = R"(
		enum State { Idle, Running, Paused }
		comptime state = State.Running;

		fn main() : i32 {
			match state {
				case .Idle, .Running:
					return 1;
				case:
					var bad : MissingType = undefined;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeMatchRequiresExhaustiveCases) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime state = State.Idle;
		fn main() : void {
			match state {
				case .Idle:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeMatchRejectsDuplicateCases) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime state = State.Idle;
		fn main() : void {
			match state {
				case .Idle:
					return;
				case .Idle:
					return;
				case .Running:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeMatchSelectedArmExecutes) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime state = State.Running;
		fn main() : i32 {
			match state {
				case .Idle:
					var bad : MissingType = undefined;
				case .Running:
					return 8;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(8, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeMatchFunctionCallScrutinee) {
	const char* source = R"(
		enum State { Idle, Running }
		fn current() : State { return State.Running; }
		comptime state = current();
		fn main() : i32 {
			match state {
				case .Idle:
					var bad : MissingType = undefined;
				case .Running:
					return 9;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeMatchSelectedFallbackExecutes) {
	const char* source = R"(
		enum State { Idle, Running, Paused }
		comptime state = State.Paused;
		fn main() : i32 {
			match state {
				case .Idle:
					return 1;
				case .Running:
					return 2;
				case:
					return 3;
			}
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(3, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeMatchPrunesInvalidAlternativeArm) {
	const char* source = R"(
		enum State { Idle, Running, Paused }
		comptime state = State.Idle;
		fn main() : i32 {
			match state {
				case .Idle, .Running:
					return 4;
				case .Paused:
					var bad : MissingType = undefined;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForRange) {
	const char* source = R"(
		fn main() : i32 {
			var total : i32 = 0;
			unroll for i = 0..3 { total += i; }
			return total;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForComptimeSequenceWithIndex) {
	const char* source = R"(
		comptime values : []i32 = [1, 2, 3];
		fn main() : i32 {
			var total : i32 = 0;
			unroll for i, value in values { total += i + value; }
			return total;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForBreakContinue) {
	const char* source = R"(
		fn main() : i32 {
			var total : i32 = 0;
			unroll for i = 0..4 {
				if i == 1 { continue; }
				if i == 3 { break; }
				total += i;
			}
			return total;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForLabeledBreakContinue) {
	const char* source = R"(
		fn main() : void {
			outer:
			unroll for i = 0..2 {
				unroll for j = 0..2 {
					if i == j { continue outer; }
					if j > 0 { break outer; }
				}
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForPerCopyCompileTimeBranch) {
	const char* source = R"(
		fn main() : i32 {
			var total : i32 = 0;
			unroll for i = 0..3 { if i > 0 { total += i; } }
			return total;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}
