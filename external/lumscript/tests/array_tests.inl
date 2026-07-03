TEST(ArrayOfEnums) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
        
		fn main() : i32 {
            var b : State[4] = undefined;
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

TEST(StaticArrayNegativeSizeFails) {
	const char* source = R"(
		fn main() : void {
			var d : i32[-1] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StaticArrayZeroSizeFails) {
	const char* source = R"(
		fn main() : void {
			var d : i32[0] = undefined;
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

TEST(StaticArrayDifferentSizesDoNotTypecheck) {
	const char* source = R"(
		fn main() : void {
			var a : i32[4] = undefined;
			var b : i32[8] = undefined;
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
			var values : i32[4] = undefined;
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
			var a : Vec2[4] = undefined;
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
			var values : i32[3] = undefined;
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
			var values : i32[3] = undefined;
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

TEST(BytecodeCompoundAssignArrayIndexEvaluatedOnce) {
	const char* source = R"(
		var hits : i32 = 0;

		fn idx() : i32 {
			hits += 1;
			return 1;
		}

		fn main() : i32 {
			var values : i32[3] = undefined;
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
		fn consume(values : i32[3]) : void {}

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

	ls_runtime* runtime = ls_runtime_create(bytecode);
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
			var values : i32[3] = undefined;
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

	ls_runtime* runtime = ls_runtime_create(bytecode);
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
			var values : ?i32[4] = undefined;
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
			var pairs : Pair[3] = undefined;
			pairs[0] = Pair { 1, 2 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MultiDimensionalStaticArrayDeclaration) {
	const char* source = R"(
		fn main() : void {
			var matrix : i32[2][3] = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(MultiDimensionalStaticArrayIndexing) {
	const char* source = R"(
		fn main() : i32 {
			var matrix : i32[2][3] = undefined;
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
			var matrix : i32[2][3] = undefined;
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

	ls_runtime* runtime = ls_runtime_create(bytecode);
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
			var cube : i32[2][2][2] = undefined;
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
			var matrix : i32[2][3] = undefined;
			matrix[0][5] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MultiDimensionalArrayOutOfBoundsOuterDimensionFails) {
	const char* source = R"(
		fn main() : void {
			var matrix : i32[2][3] = undefined;
			matrix[3][0] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
