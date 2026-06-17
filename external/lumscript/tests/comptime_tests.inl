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
			var values : i32[N] = undefined;
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


TEST(ComptimeTypeValueCanNotBeRuntimeReturnValueFails) {
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
