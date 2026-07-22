TEST(ArrayOfEnums) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}

		fn main() : i32 {
            var b : [4]State = undefined;
			var c = State.Running;
			b[0] = .Idle;
			b[1] = State.Running;
			b[2] = c;
			b[3] = b[1];
            return b[0] as i32;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(StaticArrayTypecheckAndIndexing) {
	const char* source = R"(
		fn main() : i32 {
			var d : [4]i32 = undefined;
			d[0] = 40;
			d[1] = 2;
			return d[0] + d[1];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayLiteralInfersFixedArray) {
	const char* source = R"(
		fn main() : i32 {
			var values = [1, 2, 3];
			return values[0] + values[2];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayLiteralChecksExpectedFixedArrayType) {
	const char* source = R"(
		fn main() : i32 {
			var values : [3]i32 = [1, 2, 3];
			return values[1];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayLiteralConvertsToSlice) {
	const char* source = R"(
		fn main() : i32 {
			var values : []i32 = [1, 2, 3];
			return values[2];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayLiteralInfersNestedArrays) {
	const char* source = R"(
		fn main() : i32 {
			var values = [[1, 2], [3, 4]];
			return values[0][1] + values[1][0];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayLiteralCanInitializeComptimeArray) {
	const char* source = R"(
		comptime values : [3]i32 = [1, 2, 3];
		fn main() : i32 {
			return values[0] + values[2];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayLiteralCanBePassedAsSlice) {
	const char* source = R"(
		fn first(values : []i32) : i32 { return values[0]; }
		fn main() : i32 { return first([7, 8, 9]); }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayLiteralAllowsExpectedElementConversion) {
	const char* source = R"(
		fn main() : f32 {
			var values : [2]f32 = [1, 2];
			return values[0] + values[1];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayLiteralRejectsWrongFixedArrayLength) {
	const char* source = R"(
		fn main() : void {
			var values : [2]i32 = [1, 2, 3];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ArrayLiteralRejectsIncompatibleElement) {
	const char* source = R"(
		fn main() : void {
			var values : [2]i32 = [1, "bad"];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ArrayLiteralRejectsEmptyLiteral) {
	const char* source = R"(
		fn main() : void {
			var values = [];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StaticArrayConstantIndexOutOfRangeFails) {
	const char* source = R"(
		fn main() : void {
			var d : [4]i32 = undefined;
			d[99] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StaticArrayNegativeSizeFails) {
	const char* source = R"(
		fn main() : void {
			var d : [-1]i32 = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StaticArrayZeroSizeFails) {
	const char* source = R"(
		fn main() : void {
			var d : [0]i32 = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(PostfixArraySyntaxFails) {
	const char* source = R"(
		fn main() : void {
			var d : i32[4] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IndexMustBeInteger) {
	const char* source = R"(
		fn main() : void {
			var d : [4]i32 = undefined;
			d[1.5] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StaticArrayDifferentSizesDoNotTypecheck) {
	const char* source = R"(
		fn main() : void {
			var a : [4]i32 = undefined;
			var b : [8]i32 = undefined;
			a = b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IndexingRequiresArrayTypeFails) {
	const char* source = R"(
		fn main() : void {
			const x : i32 = 1;
			const y = x[0];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IndexingWithMultipleArgumentsFails) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			const x = values[0, 1];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MethodCallOnArrayElement) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		};

		fn foo(v : Vec2) : void {}

		fn main(v : i32) : f32 {
			var a : [4]Vec2 = undefined;
			a[0] = Vec2 { 0, 0 };
			a[0].foo();
			return a[0].x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BytecodeStaticSizedArrayLocal) {
	const char* source = R"(
		fn main() : i32 {
			var values : [3]i32 = undefined;
			return 42;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	CAPI_END(module);
	return true;
}

TEST(BytecodeStaticSizedArrayIndexing) {
	const char* source = R"(
		fn main() : i32 {
			var values : [3]i32 = undefined;
			values[0] = 20;
			values[1] = 22;
			return values[0] + values[1];
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	CAPI_END(module);
	return true;
}

TEST(BytecodeTemporaryArrayIndexing) {
	const char* source = R"(
		fn make() : [3]i32 {
			var values : [3]i32 = undefined;
			values[0] = 10;
			values[1] = 32;
			values[2] = 99;
			return values;
		}

		fn main() : i32 {
			return make()[1];
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(32, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeCompoundAssignArrayIndexEvaluatedOnce) {
	const char* source = R"(
		var hits : i32 = 0;

		fn idx() : i32 {
			hits += 1;
			return 1;
		}

		fn main() : i32 {
			var values : [3]i32 = undefined;
			values[1] = 41;
			values[idx()] += 1;
			return hits * 100 + values[1];
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	CAPI_END(module);
	return true;
}

TEST(BytecodeUndefinedArrayArgumentUsesFullByteSize) {
	const char* source = R"(
		fn consume(values : [3]i32) : void {}

		fn main() : i32 {
			var poison : i32 = 123;
			consume(undefined);
			return poison;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(123, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeRefParameterArrayCall) {
	const char* source = R"(
		fn bump(value : ref i32) : void {
			value += 2;
		}

		fn main() : i32 {
			var values : [3]i32 = undefined;
			values[0] = 20;
			values[1] = 20;
			bump(ref values[1]);
			return values[0] + values[1];
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(NullableIndexingRequiresNullCheckFails) {
	const char* source = R"(
		fn main() : void {
			var values : ?[4]i32 = undefined;
			const x = values[0];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructStaticArrayTypechecks) {
	const char* source = R"(
		struct Pair {
			a : i32;
			b : i32;
		}

		fn main() : void {
			var pairs : [3]Pair = undefined;
			pairs[0] = Pair { 1, 2 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MultiDimensionalStaticArrayDeclaration) {
	const char* source = R"(
		fn main() : void {
			var matrix : [2][3]i32 = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MultiDimensionalStaticArrayIndexing) {
	const char* source = R"(
		fn main() : i32 {
			var matrix : [2][3]i32 = undefined;
			matrix[0][0] = 10;
			matrix[0][1] = 20;
			matrix[1][2] = 30;
			return matrix[0][0] + matrix[0][1] + matrix[1][2];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(BytecodeMultiDimensionalStaticArrayIndexing) {
	const char* source = R"(
		fn main() : i32 {
			var matrix : [2][3]i32 = undefined;
			matrix[0][0] = 10;
			matrix[0][1] = 20;
			matrix[1][2] = 30;
			return matrix[0][0] + matrix[0][1] + matrix[1][2];
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(60, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(ThreeDimensionalStaticArray) {
	const char* source = R"(
		fn main() : i32 {
			var cube : [2][2][2]i32 = undefined;
			cube[0][0][0] = 1;
			cube[1][1][1] = 8;
			return cube[0][0][0] + cube[1][1][1];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MultiDimensionalArrayOutOfBoundsInnerDimensionFails) {
	const char* source = R"(
		fn main() : void {
			var matrix : [2][3]i32 = undefined;
			matrix[0][5] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MultiDimensionalArrayOutOfBoundsOuterDimensionFails) {
	const char* source = R"(
		fn main() : void {
			var matrix : [2][3]i32 = undefined;
			matrix[3][0] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ArrayOfNullableInt) {
	const char* source = R"(
		fn main() : void {
			var values : [4]?i32 = undefined;
			values[0] = 42;
			values[1] = null;
			if values[0] != null {
				const x = values[0];
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SliceOfNullableArray) {
	const char* source = R"(
		fn main() : void {
			var arrays : []?[3]i32 = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SliceOfNullableArrayOfNullableInt) {
	const char* source = R"(
		fn main() : void {
			var values : []?[3]?i32 = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableSliceOfArray) {
	const char* source = R"(
		fn main() : void {
			var arrays : ?[][3]i32 = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayOfSliceOfNullableInt) {
	const char* source = R"(
		fn main() : void {
			var values : [4][]?i32 = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MultiDimensionalArrayOfNullableInt) {
	const char* source = R"(
		fn main() : void {
			var matrix : [2][3]?i32 = undefined;
			matrix[0][1] = 42;
			matrix[1][2] = null;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ArrayOfNullableArrays) {
	const char* source = R"(
		fn main() : void {
			var arrays : [3]?[2]i32 = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}
