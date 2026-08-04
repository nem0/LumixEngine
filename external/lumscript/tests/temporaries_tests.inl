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
