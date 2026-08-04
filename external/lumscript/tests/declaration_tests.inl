TEST(BasicFunction) {
	EXPECT_COMPILE("fn main() : void {}");
	return true;
}

TEST(BasicLocalVar) {
	EXPECT_COMPILE("fn main() : void { var i : i32 = 42; }");
	return true;
}

TEST(BasicLocalConst) {
	EXPECT_COMPILE("fn main() : void { const i : i32 = 42; }");
	return true;
}

TEST(BasicGlobalVar) {
	EXPECT_COMPILE("var x = 42;");
	return true;
}

TEST(GlobalTypeLiteralInitializerFails) {
	EXPECT_COMPILE_FAIL("var x = i32;");
	return true;
}

TEST(BasicGlobalConst) {
	EXPECT_COMPILE("const enabled = true;");
	return true;
}

TEST(BasicReturn) {
	EXPECT_COMPILE("fn main() : i32 { return 42; }");
	return true;
}

TEST(BasicAssign) {
	EXPECT_COMPILE("fn main() : void { var i = 42; i = 32; }");
	return true;
}

TEST(AssignmentToMemberWithoutBaseFails) {
	const char* source = R"(
		fn set(v : *i32) : void {
		}

		fn main() : void {
			set(&.field);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructLiteralFieldsAllowImplicitConversions) {
	const char* source = R"(
		struct Values {
			optional : ?i32;
			items : []i32;
		}

		fn main() : void {
			var items : [2]i32 = undefined;
			const values = Values { 1, items };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(AssignBinaryOp) {
	EXPECT_COMPILE("fn main() : void { var i = 42; i = 32 * 46; }");
	return true;
}


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

TEST(DuplicateDeclarationsFail) {
	const char* duplicate_struct = R"(
		struct A { x : i32; }
		struct A { y : i32; }
	)";
	EXPECT_COMPILE_FAIL(duplicate_struct);

	const char* duplicate_field = R"(
		struct A { x : i32; x : i32; }
	)";
	EXPECT_COMPILE_FAIL(duplicate_field);

	const char* duplicate_enum_member = R"(
		enum A { X, X }
	)";
	EXPECT_COMPILE_FAIL(duplicate_enum_member);

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

TEST(StructDeclarationTrailingSemicolonFails) {
	const char* source = R"(
		struct A { x : i32; };
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(GlobalVariablesTypecheck) {
	const char* source = R"(
		var counter : i32 = 1;
		const step = 2;
		var label : []const u8 = "count";

		fn increment() : i32 {
			counter += step;
			return counter;
		}

		fn get_label() : []const u8 {
			return label;
		}
	)";
	TestContext diagnostics;
	ls_module* module = ls_module_create(&diagnostics.host);
	EXPECT_TRUE(module != nullptr);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	EXPECT_TRUE(ls_module_get_global_count(module) == 5);
	ls_module_destroy(module);
	return true;
}

TEST(GlobalInitializerTypeMismatchFails) {
	const char* source = R"(
		var g : i32 = "count";
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(GlobalVariableNeedsTypeOrInitializerFails) {
	const char* source = R"(
		var g;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(GlobalUndefinedInitializerRequiresExplicitTypeFails) {
	const char* source = R"(
		var g = undefined;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ModuleGlobalsAreVisibleInFunctions) {
	const char* source = R"(
		var g_inputs : i32 = undefined;

		fn init(inputs : i32) : void {
			g_inputs = inputs;
		}
	)";
	EXPECT_COMPILE(source);
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

TEST(ValueMustBeInitializedIfNotUndefinedFails) {
	const char* source = R"(
		fn main() : void {
			var x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);

	const char* source2 = R"(
		fn main() : void {
			var x : i32;
		}
	)";
	EXPECT_COMPILE_FAIL(source2);

	return true;
}

TEST(UndefinedInitializerRequiresExplicitTypeFails) {
	const char* source = R"(
		fn main() : void {
			var x = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MemberAccessOnPrimitiveFails) {
	const char* source = R"(
		fn main() : i32 {
			const value = 1;
			return value.field;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstGlobalCanNotBeUndefined) {
	const char* source = R"(
		const g : i32 = undefined;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstLocalCanNotBeUndefined) {
	const char* source = R"(
		fn main() : void {
			const c : i32 = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructLiteralFieldCountMismatchFails) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn main() : void {
			const v : Vec2 = Vec2 { 1 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructLiteralTypeMismatchFails) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn main() : void {
			const v : Vec2 = Vec2 { 1, true };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructLiteralWithoutExpectedTypeFails) {
	const char* source = R"(
		fn main() : void {
			const v = { 1, 2 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(CompoundAssignmentTypeMismatchFails) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
		}

		operator +(a : Vec2, b : Vec2) : i32 {
			return a.x + b.x;
		}

		fn main() : void {
			var value = Vec2 { 1 };
			value += Vec2 { 2 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DivisionAndModuloByConstantZeroCompile) {
	const char* divide_source = R"(
		fn main(v : i32) : i32 {
			return v / 0;
		}
	)";
	EXPECT_COMPILE(divide_source);

	const char* modulo_source = R"(
		fn main(v : i32) : i32 {
			return v % 0;
		}
	)";
	EXPECT_COMPILE(modulo_source);

	const char* float_source = R"(
		fn main() : f32 {
			return 1.0 / 0.0;
		}
	)";
	EXPECT_COMPILE(float_source);
	return true;
}

TEST(MixedNumericEqualityRequiresExplicitCastFails) {
	const char* source = R"(
		fn main(a : i32, b : f64) : bool {
			return a == b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FailedFunctionTypecheckRemainsFailed) {
	const char* source = R"(
		fn bad() : i32 {
			return true;
		}
	)";
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	context.diagnostics.output_enabled = false;
	EXPECT_TRUE(ls_module_parse(module, toLs(source), {}) == LS_RESULT_OK);
	EXPECT_TRUE(ls_module_typecheck(module) == LS_RESULT_FAILURE);
	EXPECT_TRUE(ls_module_typecheck(module) == LS_RESULT_FAILURE);
	ls_module_destroy(module);
	return true;
}


TEST(UnknownStructField) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		fn main() : void {
			var v = Vec2 { 1.0, 2.0 };
			v.z = 32;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UFCSNativeType) {
	const char* source = R"(
		fn foo(i : i32) : void {}
		fn main() : void {
			4.foo();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ValueAsTypeFails) {
	const char* source = R"(
		fn main() : void {
			var x : type = 42;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UFCSWithExtraArguments) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}

		fn scale(v : Vec2, factor : i32) : i32 {
			return (v.x + v.y) * factor;
		}

		fn main() : i32 {
			const v : Vec2 = Vec2 { 3, 4 };
			return v.scale(10);
		}
	)";

	EXPECT_COMPILE(source);
	return true;
}

TEST(StructMethodCallOnReturnValue) {
	const char* source = R"(
		struct State {
			v : i32;
		}
        
        fn bar(s : State) : bool  { return s.v == 1; }

        fn foo() : State { return State { 1 }; }

		fn main() : bool {
            var b = foo().bar();
            return b;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}


TEST(UnknownSymbolRuntimeVarDeclFails) {
	const char* source = R"(
		var tmp : Missing = undefined;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(ConstImportedValueUsedInComptimeFails) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			var value : [lib.COUNT]i32 = undefined;
			return 42;
		}
	)";
	const char* lib_source = R"(
		const COUNT = 4;
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}


TEST(UnknownImportedValueUsedInComptimeFails) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			var value : [lib.Missing]i32 = undefined;
			return 42;
		}
	)";
	const char* lib_source = R"(
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(ComptimeNegativeIntegerLiteralRangeChecks) {
	const char* source = R"(
		comptime X : i8 = -128;
		comptime Y : bool = -128 > X;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TernarnyUntypedBranches) {
	const char* source = R"(
		var forward = true;
		var backward = false;
		var speed_y : f32 = forward ? 3.0 :
							backward ? -3.0 :
							0;
	)";
	EXPECT_COMPILE(source);
	return true;
}

