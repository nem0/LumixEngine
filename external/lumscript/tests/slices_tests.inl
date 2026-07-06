TEST(SliceTypeSyntax) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[:];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SliceCreationFromArray) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			const first : []i32 = values[1:3];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SliceCreationFromArrayShorthandForms) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			const tail = values[1:];
			const head = values[:3];
			const whole = values[:];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SliceCreationFromSliceShorthandForms) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			const whole = values[:];
			const tail = whole[1:];
			const head = whole[:3];
			const full = whole[:];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SliceImplicitConversion) {
	const char* source = R"(
		fn consume(values : []i32) : void {}

		fn main() : void {
			var values : [4]i32 = undefined;
			consume(values);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SliceAssignmentAndReturn) {
	const char* source = R"(
		fn identity(values : []i32) : []i32 {
			const copy : []i32 = values;
			return copy;
		}

		fn main() : void {
			var values : [4]i32 = undefined;
			const slice : []i32 = identity(values[:]);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SliceReversedBoundsFail) {
	const char* prerequisite = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[1:3];
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[3:1];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceOutOfRangeEndFail) {
	const char* prerequisite = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[0:4];
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[0:10];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceNegativeBeginFail) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[-1:4];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceMustHaveColon) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[4];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceNegativeEndFail) {
	const char* prerequisite = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[0:4];
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[1:-1];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceNonIntegerBeginFail) {
	const char* prerequisite = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[0:4];
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values["abc":4];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceNonIntegerEndFail) {
	const char* prerequisite = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[0:4];
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[0:"abc"];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceImplicitConversionLengthRuntime) {
	const char* source = R"(
		fn get_length(s : []i32) : isize {
			return length(s);
		}

		fn main() : i32 {
			var values : [4]i32 = undefined;
			return get_length(values) as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(4, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceIndexMustBeIntegerFail) {
	const char* prerequisite = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[:];
			const x = slice[1];
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[:];
			const x = slice[1.5];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceRequiresSliceOrArrayTypeFail) {
	const char* prerequisite = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			const slice = values[:];
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			const x : i32 = 1;
			const slice = x[:];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceLengthAndIndexRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[0] = 3;
			values[1] = 5;
			values[2] = 7;
			values[3] = 11;
			const slice : []i32 = values[1:3];
			return (length(slice) as i32) * 100 + slice[0] * 10 + slice[1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(257, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceAliasesBackingArrayRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var values : [3]i32 = undefined;
			values[0] = 1;
			values[1] = 2;
			values[2] = 3;
			var slice : []i32 = values[1:3];
			slice[0] = 40;
			return values[1] + slice[1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(43, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceFromSliceRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[0] = 3;
			values[1] = 5;
			values[2] = 7;
			values[3] = 11;
			const slice : []i32 = values[1:4];
			const sub : []i32 = slice[1:];
			return (length(sub) as i32) * 100 + sub[0] * 10 + sub[1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(281, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceOutOfRangeStartFail) {
	const char* prerequisite = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[3:];
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var slice : []i32 = values[5:];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceZeroLengthRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			const slice : []i32 = values[2:2];
			return length(slice) as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceNullInitializationRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var slice : []i32 = null;
			return length(slice) as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceIterationRuntime) {
	const char* source = R"(
		fn sum(values : []i32) : i32 {
			var total : i32 = 0;
			var i : isize = 0;
			while i < length(values) {
				total += values[i];
				i += 1;
			}
			return total;
		}

		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[0] = 1;
			values[1] = 2;
			values[2] = 3;
			values[3] = 4;
			return sum(values[1:3]);
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

TEST(SliceReturnAliasesOriginalRuntime) {
	const char* source = R"(
		fn get_slice(values : []i32) : []i32 {
			return values[1:3];
		}

		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[0] = 10;
			values[1] = 20;
			values[2] = 30;
			values[3] = 40;
			var s : []i32 = get_slice(values[:]);
			s[0] = 99;
			return values[1];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(99, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceRuntimeOutOfBoundsFails) {
	const char* source = R"(
		fn main(i : i32) : i32 {
			var values : [3]i32 = undefined;
			values[0] = 1;
			values[1] = 2;
			values[2] = 3;
			const slice : []i32 = values[1:3];
			return slice[i];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;
	ls_push_i32(runtime, 2);
	EXPECT_TRUE(!ls_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}
