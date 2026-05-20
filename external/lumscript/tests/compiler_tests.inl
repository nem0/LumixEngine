TEST(ConstAssignmentFails) {
	const char* source = R"(
		fn main() : void {
			const a = 1;
			a = 2;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StringConcatenationTypechecks) {
	const char* source = R"(
		fn join(a : string, b : string) : string {
			return a + " " + b;
		}

		fn main() : string {
			const hello : string = "Hello";
			const target = "Lumix";
			return join(hello, target);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}


TEST(StringConcatenationRejectsNonString) {
	const char* source = R"(
		fn main() : string {
			return "count: " + 42;
		}
	)";
	EXPECT_COMPILE_FAIL(source);

	const char* global_source = R"(
		const a = 1;
		fn main() : void {
			a = 2;
		}
	)";
	EXPECT_COMPILE_FAIL(global_source);
	return true;
}

TEST(DuplicateDeclarationsFail) {
	const char* duplicate_struct = R"(
		struct A { x : i32; };
		struct A { y : i32; };
	)";
	EXPECT_COMPILE_FAIL(duplicate_struct);

	const char* duplicate_field = R"(
		struct A { x : i32; x : i32; };
	)";
	EXPECT_COMPILE_FAIL(duplicate_field);

	const char* duplicate_local = R"(
		fn main() : void {
			var a = 1;
			var a = 2;
		}
	)";
	EXPECT_COMPILE_FAIL(duplicate_local);

	const char* duplicate_param = R"(
		fn f(a : i32, a : i32) : i32 {
			return a;
		}
	)";
	EXPECT_COMPILE_FAIL(duplicate_param);

	const char* duplicate_global = R"(
		var g = 1;
		var g = 2;
	)";
	EXPECT_COMPILE_FAIL(duplicate_global);

	const char* duplicate_global_function = R"(
		var main = 1;
		fn main() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(duplicate_global_function);
	return true;
}

TEST(GlobalVariablesTypecheck) {
	const char* source = R"(
		var counter : i32 = 1;
		const step = 2;
		var label : string = "count";

		fn increment() : i32 {
			counter += step;
			return counter;
		}

		fn get_label() : string {
			return label;
		}
	)";
	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, &diagnostics.host, nullptr, nullptr));
	EXPECT_TRUE(ls_module_get_global_count(module) == 3);
	ls_module_destroy(module);
	return true;
}

TEST(VariableAndConstRequireInitializer) {
	const char* source = R"(
		var g : i32;

		fn main() : void {
			var local : i32;
			const c : i32;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(VariableCanBeExplicitlyUndefined) {
	const char* source = R"(
		var g : i32 = undefined;

		fn main() : i32 {
			var local : i32 = undefined;
			local = 7;
			return local + g;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ConstCanNotBeUndefined) {
	const char* source = R"(
		const g : i32 = undefined;

		fn main() : void {
			const c : i32 = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ExplicitCastRequired) {
	{
		const char* invalid = R"(
			fn main() : f32 {
				const x : i32 = 10;
				return x;
			}
		)";
		EXPECT_COMPILE_FAIL(invalid);
	}

	{
		const char* valid = R"(
			fn main() : f32 {
				const x : i32 = 10;
				return x as f32;
			}
		)";
		EXPECT_COMPILE(valid);
	}
	return true;
}

TEST(BinaryNumericOperatorsRequireSameOperandType) {
	{
		const char* mixed_add = R"(
			fn main() : i32 {
				const a : i32 = 1;
				const b : i64 = 2 as i64;
				return a + b;
			}
		)";
		EXPECT_COMPILE_FAIL(mixed_add);

		const char* mixed_compare = R"(
			fn main() : bool {
				const a : i32 = 1;
				const b : f32 = 1 as f32;
				return a < b;
			}
		)";
		EXPECT_COMPILE_FAIL(mixed_compare);
	}

	{
		const char* explicit_cast_ok = R"(
			fn main() : i64 {
				const a : i32 = 1;
				const b : i64 = 2 as i64;
				return (a as i64) + b;
			}
		)";
		EXPECT_COMPILE(explicit_cast_ok);
	}
	return true;
}

TEST(DivisionAndModuloByConstantZeroFail) {
	const char* divide_source = R"(
		fn main(v : i32) : i32 {
			return v / 0;
		}
	)";
	EXPECT_COMPILE_FAIL(divide_source);

	const char* modulo_source = R"(
		fn main(v : i32) : i32 {
			return v % 0;
		}
	)";
	EXPECT_COMPILE_FAIL(modulo_source);

	const char* float_source = R"(
		fn main() : f32 {
			return 1.0 / 0.0;
		}
	)";
	EXPECT_COMPILE(float_source);
	return true;
}

TEST(ImportAddsDeclarationsToCurrentModule) {
	const char* main_source = R"(
		import "math"

		fn main() : i32 {
			const v : Vec2 = Vec2 { 20, 22 };
			return sum(v);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	LumScriptImportFile file = { "math", math_source };
	LumScriptImportFiles files = { &file, 1 };
	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &diagnostics.host, &resolveLumScriptImportC, &files));
	EXPECT_TRUE(ls_module_get_struct_count(module) == 1);
	EXPECT_TRUE(ls_module_get_function_count(module) == 2);
	ls_module_destroy(module);
	return true;
}

TEST(MissingImportFails) {
	const char* source = R"(
		import "missing"

		fn main() : void {
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DuplicateUnaliasedImportIsNoOp) {
	const char* main_source = R"(
		import "math"
		import "math"

		fn main() : i32 {
			const v : Vec2 = Vec2 { 20, 22 };
			return sum(v);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	LumScriptImportFile file = { "math", math_source };
	LumScriptImportFiles files = { &file, 1 };
	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &diagnostics.host, &resolveLumScriptImportC, &files));
	EXPECT_EQ(1, ls_module_get_struct_count(module));
	EXPECT_EQ(2, ls_module_get_function_count(module));
	ls_module_destroy(module);
	return true;
}

TEST(DuplicateAliasedImportOfSamePathIsNoOp) {
	const char* main_source = R"(
		import "math" as m
		import "math" as m

		fn main() : i32 {
			const v : m.Vec2 = m.Vec2 { 20, 22 };
			return m.sum(v);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	LumScriptImportFile file = { "math", math_source };
	LumScriptImportFiles files = { &file, 1 };
	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &diagnostics.host, &resolveLumScriptImportC, &files));
	EXPECT_EQ(1, ls_module_get_struct_count(module));
	EXPECT_EQ(2, ls_module_get_function_count(module));
	ls_module_destroy(module);
	return true;
}

TEST(AliasedImportCollisionFails) {
	const char* source = R"(
		import "math_a" as m
		import "math_b" as m

		fn main() : i32 {
			return 0;
		}
	)";
	const char* math_a_source = R"(
		fn one() : i32 {
			return 1;
		}
	)";
	const char* math_b_source = R"(
		fn two() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ "math_a", math_a_source },
		{ "math_b", math_b_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	diagnostics.diagnostics.output_enabled = false;
	EXPECT_TRUE(!ls_module_compile(module, toLs(source), {}, &diagnostics.host, &resolveLumScriptImportC, &files));
	EXPECT_TRUE(diagnostics.diagnostics.has_error);
	ls_module_destroy(module);
	return true;
}

TEST(ImportCycleFails) {
	const char* source = R"(
		import "a"

		fn main() : i32 {
			return 0;
		}
	)";
	const char* a_source = R"(
		import "b"

		fn in_a() : i32 {
			return 1;
		}
	)";
	const char* b_source = R"(
		import "a"

		fn in_b() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ "a", a_source },
		{ "b", b_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	diagnostics.diagnostics.output_enabled = false;
	EXPECT_TRUE(!ls_module_compile(module, toLs(source), {}, &diagnostics.host, &resolveLumScriptImportC, &files));
	EXPECT_TRUE(diagnostics.diagnostics.has_error);
	ls_module_destroy(module);
	return true;
}

TEST(FirstParameterNamespaceResolutionPrecedenceTypecheck) {
	const char* main_source = R"(
		import "entity_mod" as entity
		import "helper_mod" as e

		fn destroy(x : entity.Entity) : i32 {
			return 3;
		}

		fn main() : i32 {
			const x : entity.Entity = entity.Entity { 7 };
			return e.destroy() + x.destroy() + destroy(x);
		}
	)";

	const char* entity_source = R"(
		struct Entity {
			id : i32;
		};

		fn destroy(x : Entity) : i32 {
			return x.id;
		}
	)";

	const char* helper_source = R"(
		fn destroy() : i32 {
			return 2;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ "entity_mod", entity_source },
		{ "helper_mod", helper_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(main_source), {}, &diagnostics.host, &resolveLumScriptImportC, &files));
	ls_module_destroy(module);
	return true;
}

TEST(EnumShorthandInComparison) {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		};

		fn check(state : State) : bool {
			return state == .Running;
		}

		fn main() : void {
			var s : State = .Idle;
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
		};

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
		};

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

TEST(EnumDoesNotConvertImplicitlyToInteger) {
	const char* assignment = R"(
		enum State {
			Idle,
			Running
		};

		fn main() : void {
			const value : i32 = State.Running;
		}
	)";
	EXPECT_COMPILE_FAIL(assignment);

	const char* argument = R"(
		enum State {
			Idle,
			Running
		};

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
		};

		fn main(value : i32) : bool {
			return value == State.Running;
		}
	)";
	EXPECT_COMPILE_FAIL(comparison);

	const char* integer_to_enum = R"(
		enum State {
			Idle,
			Running
		};

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
		};

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
		};

		fn main() : State {
			const raw : i32 = 123;
			return raw as State;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MatchTypechecks) {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		};

		fn enum_match(state : State) : i32 {
			match state {
				case .Idle:
					return 1;
				case .Running, .Paused:
					return 2;
			}
			return 0;
		}

		fn range_match(score : i32) : i32 {
			match score {
				case 0:
					return 0;
				case 1..9, 99:
					return 1;
				case _:
					return 2;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MatchArmAllowsMultipleStatements) {
	const char* source = R"(
		fn main(v : i32) : i32 {
			var result : i32 = 0;
			match v {
				case 0:
					result = 1;
					result += 2;
				case _:
					result = 10;
					result += 20;
			}
			return result;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MatchRejectsPatternTypeMismatch) {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case "bad":
					return;
				case _:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRequiresExhaustiveEnumOrFallback) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn main(state : State) : void {
			match state {
				case .Idle:
					return;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MatchRejectsDuplicateEnumCase) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn main(state : State) : void {
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

TEST(RefParameterTypechecks) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(ref x);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(RefParameterRequiresRefArgument) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentMustBeAssignable) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(ref (x + 1));
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentCanNotBeConst) {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			const x : i32 = 10;
			increment(ref x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentAllowsMutableGlobal) {
	const char* source = R"(
		var counter : i32 = 0;

		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			increment(ref counter);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(RefArgumentRejectsConstGlobal) {
	const char* source = R"(
		const counter : i32 = 0;

		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			increment(ref counter);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RefArgumentAllowsNestedMutableField) {
	const char* source = R"(
		struct Stats {
			hp : i32;
		};

		struct Player {
			stats : Stats;
		};

		fn bump(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var p = Player { Stats { 10 } };
			bump(ref p.stats.hp);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(RefArgumentRejectsNullableTargetType) {
	const char* source = R"(
		fn clear(v : ref ?i32) : void {
			v = null;
		}

		fn main() : void {
			var x : ?i32 = 10;
			clear(ref x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DeferTypechecks) {
	const char* source = R"(
		fn cleanup(v : ref i32) : void {
			v += 1;
		}

		fn main() : i32 {
			var x : i32 = 0;
			defer cleanup(ref x);
			x += 2;
			return x;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(DeferCanNotWrapReturn) {
	const char* source = R"(
		fn main() : i32 {
			defer return 1;
			return 2;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullablePromotionTypechecks) {
	const char* source = R"(
		struct Vec3 {
			x : f32;
			y : f32;
			z : f32;
		};

		fn length_if_present(v : ?Vec3) : f32 {
			if v != null {
				return v.x;
			}
			return 0 as f32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableUseWithoutCheckFails) {
	const char* source = R"(
		struct Vec3 {
			x : f32;
			y : f32;
			z : f32;
		};

		fn bad(v : ?Vec3) : f32 {
			return v.x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullOnlyAssignableToNullable) {
	const char* bad = R"(
		fn bad() : i32 {
			var x : i32 = null;
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(bad);

	const char* ok = R"(
		fn ok() : i32 {
			var x : ?i32 = null;
			if x != null {
				return x;
			}
			return 0;
		}
	)";
	EXPECT_COMPILE(ok);
	return true;
}

TEST(ExtendedScalarTypesTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			const a : i8 = 10 as i8;
			const b : u8 = 20 as u8;
			const c : i16 = 30 as i16;
			const d : u16 = 40 as u16;
			const e : i64 = 50 as i64;
			const f : u64 = 60 as u64;
			const g : f64 = 1 as f64;
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
		};

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

TEST(FirstClassFunctionsTypecheck) {
	const char* source = R"(
		fn add(a : i32, b : i32) : i32 {
			return a + b;
		}

		fn apply(f : fn(i32, i32) : i32, a : i32, b : i32) : i32 {
			return f(a, b);
		}

		fn main() : i32 {
			const f = add;
			const g : fn(i32, i32) : i32 = add;
			return apply(f, 20, 2) + g(10, 5);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FirstClassFunctionSignatureMismatchFails) {
	const char* source = R"(
		fn to_float(a : i32) : f32 {
			return a as f32;
		}

		fn main() : void {
			const f : fn(i32) : i32 = to_float;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NestedFunctionsTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			fn add(a : i32, b : i32) : i32 {
				return a + b;
			}
			return add(20, 22);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NestedFunctionCanNotCaptureOuterLocal) {
	const char* source = R"(
		fn main() : i32 {
			const x = 10;
			fn get() : i32 {
				return x;
			}
			return get();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NestedFunctionNotGloballyVisible) {
	const char* source = R"(
		fn main() : i32 {
			fn local() : i32 {
				return 1;
			}
			return local();
		}

		fn other() : i32 {
			return local();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StaticArrayTypecheckAndIndexing) {
	const char* source = R"(
		fn main() : i32 {
			var d : i32[4] = undefined;
			d[0] = 40;
			d[1] = 2;
			return d[0] + d[1];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(StaticArrayConstantIndexOutOfRangeFails) {
	const char* source = R"(
		fn main() : void {
			var d : i32[4] = undefined;
			d[99] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StaticArrayIndexMustBeInteger) {
	const char* source = R"(
		fn main() : void {
			var d : i32[4] = undefined;
			d[1.5] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BreakContinueTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			while i < 10 {
				i += 1;
				if i == 3 {
					continue;
				}
				if i == 8 {
					break;
				}
			}
			return i;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(BreakContinueOutsideLoopFail) {
	const char* source = R"(
		fn main() : void {
			break;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NamedLabelTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			outer: while i < 10 {
				i += 1;
				var j : i32 = 0;
				while j < 10 {
					j += 1;
					if j == 2 {
						continue outer;
					}
				}
			}
			return i;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NamedLabelUnknownFails) {
	const char* source = R"(
		fn main() : void {
			while true {
				break missing;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DuplicateNamedLabelFails) {
	const char* source = R"(
		fn main() : void {
			outer: while true {
				break;
			}
			outer: while true {
				break;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
