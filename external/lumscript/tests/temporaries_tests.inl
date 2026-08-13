TEST(TemporaryFieldAssignmentFails) {
	const char* source = R"(
		struct Data {
			value : i32;
		}

		fn get_data() : Data {
			return Data { 42 };
		}

		fn main() : void {
			get_data().value = 10;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemporaryCompoundAssignmentFails) {
	const char* source = R"(
		struct Data {
			value : i32;
		}

		fn get_data() : Data {
			return Data { 42 };
		}

		fn main() : void {
			get_data().value += 5;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemporaryArrayIndexAssignmentFails) {
	const char* source = R"(
		struct Data {
			values : [4]i32;
		}

		fn get_data() : Data {
			var arr : [4]i32 = undefined;
			arr[0] = 1;
			arr[1] = 2;
			arr[2] = 3;
			arr[3] = 4;
			return Data { arr };
		}

		fn main() : void {
			get_data().values[0] = 10;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemporaryArrayIndexCompoundAssignmentFails) {
	const char* source = R"(
		struct Data {
			values : [4]i32;
		}

		fn get_data() : Data {
			var arr : [4]i32 = undefined;
			arr[0] = 1;
			arr[1] = 2;
			arr[2] = 3;
			arr[3] = 4;
			return Data { arr };
		}

		fn main() : void {
			get_data().values[0] += 4;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemporarySliceIndexAssignmentFails) {
	const char* source = R"(
		struct Data {
			values : []i32;
		}

		fn get_data() : Data {
			return Data { null };
		}

		fn main() : void {
			get_data().values[0] = 10;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

	TEST(NestedArrayIndexAssignmentNeedsIRSupport) {
	const char* source = R"(
		fn main() : i32 {
			var values : [2][2]i32 = [[1, 2], [3, 4]];
			values[0][1] = 9;
			return values[0][1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(9, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DynamicNestedArrayIndexAssignmentNeedsIRSupport) {
	const char* source = R"(
		fn main() : i32 {
			var values : [2][2]i32 = [[1, 2], [3, 4]];
			var outer : i32 = 0;
			var inner : i32 = 1;
			values[outer][inner] = 9;
			return values[outer][inner];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(9, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DynamicNonSquareNestedArrayIndexStore) {
	const char* source = R"(
		fn main() : i32 {
			var values : [2][3]i32 = [[10, 20, 30], [40, 50, 60]];
			var outer : i32 = 1;
			var inner : i32 = 1;
			values[outer][inner] = 99;
			return values[1][0] + values[1][1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(139, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DynamicNonSquareNestedArrayIndexLoad) {
	const char* source = R"(
		fn main() : i32 {
			var values : [2][3]i32 = [[10, 20, 30], [40, 50, 60]];
			var outer : i32 = 1;
			var inner : i32 = 1;
			return values[outer][inner];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(50, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(NestedArrayIndicesEvaluateLeftToRight) {
	const char* source = R"(
		var order : i32 = 0;

		fn outer() : i32 {
			order = order * 10 + 1;
			return 1;
		}

		fn inner() : i32 {
			order = order * 10 + 2;
			return 1;
		}

		fn main() : i32 {
			var values : [2][3]i32 = undefined;
			values[outer()][inner()] = 42;
			return order;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComputedArraySliceNeedsIRSupport) {
	const char* source = R"(
		fn main() : i32 {
			var values : [2]i32 = [4, 5];
			var view = values[0:2][:];
			return view[1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemporaryFromCallExpressionFails) {
	const char* source = R"(
		fn create_int() : i32 {
			return 42;
		}

		fn main() : void {
			create_int() = 10;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemporaryFromBinaryExpressionFails) {
	const char* source = R"(
		fn main() : void {
			(1 + 2) = 10;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemporaryFromStructLiteralFails) {
	const char* source = R"(
		struct Data {
			value : i32;
		}

		fn main() : void {
			Data { 42 }.value = 10;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemporaryFromCastFails) {
	const char* source = R"(
		fn get_i64() : i64 {
			return 42;
		}

		struct Data {
			value : i32;
		}

		fn main() : void {
			(get_i64() as i32) = 10;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemporaryAddressTakenFails) {
	const char* source = R"(
		fn increment(v : *i32) : void {
			v.* += 1;
		}

		fn get_i32() : i32 {
			return 42;
		}

		fn main() : void {
			increment(&get_i32());
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(VariableFieldAssignmentSucceeds) {
	const char* source = R"(
		struct Data {
			value : i32;
		}

		fn get_data() : Data {
			return Data { 42 };
		}

		fn main() : void {
			var data = get_data();
			data.value = 10;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(VariableCompoundAssignmentSucceeds) {
	const char* source = R"(
		struct Data {
			value : i32;
		}

		fn get_data() : Data {
			return Data { 42 };
		}

		fn main() : void {
			var data = get_data();
			data.value += 5;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(VariableArrayIndexAssignmentSucceeds) {
	const char* source = R"(
		struct Data {
			values : [4]i32;
		}

		fn get_data() : Data {
			var arr : [4]i32 = undefined;
			arr[0] = 1;
			arr[1] = 2;
			arr[2] = 3;
			arr[3] = 4;
			return Data { arr };
		}

		fn main() : void {
			var data = get_data();
			data.values[0] = 10;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(BuilderPatternWithUFCSSucceeds) {
	const char* source = R"(
		struct Builder {
			value : i32;
		}

		fn with_value(b : Builder, v : i32) : Builder {
			var result = b;
			result.value = v;
			return result;
		}

		fn get_builder() : Builder {
			return Builder { 0 };
		}

		fn main() : void {
			var b = get_builder().with_value(42).with_value(100);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}
