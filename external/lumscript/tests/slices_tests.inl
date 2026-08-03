// Compiles `source` and returns the frame size of `main`, or 0 if it is absent.
static u32 mainFrameSize(const char* source, const char* test_name) {
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	if (!module) return 0;
	if (!ls_module_compile(module, toLs(source), makeStringView(test_name), nullptr, nullptr)) return 0;
	ls_bytecode* bytecode = ls_bytecode_compile(module, &context.host);
	if (!bytecode) return 0;
	u32 frame_size = 0;
	for (u32 i = 0; i < bytecode->function_count; ++i) {
		if (equalStrings(bytecode->functions[i].name, "main")) {
			frame_size = bytecode->functions[i].frame_size;
			break;
		}
	}
	ls_bytecode_destroy(bytecode);
	return frame_size;
}

TEST(SliceExpressionsDoNotGrowFrame) {
	// Each `arr[:]` stashes its source in an addLocal whose next_local_offset
	// bump is never undone (bytecode_compiler.cpp, compileBoundedRange), so the
	// scratch is promoted to a function-lifetime local. Frame size should not
	// scale with how many slice expressions a function contains.
	const char* one = R"(
		fn take(s : []i32) : i32 { return s[0]; }

		fn main() : i32 {
			var values : [3]i32 = undefined;
			var total : i32 = 0;
			total += take(values[:]);
			return total;
		}
	)";
	const char* many = R"(
		fn take(s : []i32) : i32 { return s[0]; }

		fn main() : i32 {
			var values : [3]i32 = undefined;
			var total : i32 = 0;
			total += take(values[:]);
			total += take(values[:]);
			total += take(values[:]);
			total += take(values[:]);
			total += take(values[:]);
			total += take(values[:]);
			return total;
		}
	)";

	const u32 one_frame = mainFrameSize(one, __func__);
	const u32 many_frame = mainFrameSize(many, __func__);
	EXPECT_TRUE(one_frame != 0);
	EXPECT_TRUE(many_frame != 0);
	// The six-slice version does the same work in the same scope, so it needs no
	// more frame than the one-slice version.
	EXPECT_EQ(one_frame, many_frame);
	return true;
}

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

TEST(ConstSliceTypeSyntaxAndConversion) {
	const char* source = R"(
		fn inspect(values : []const i32) : i32 {
			return values[0] + values.length as i32;
		}

		fn main() : i32 {
			var values : [2]i32 = [4, 5];
			var writable : []i32 = values[:];
			var readable : []const i32 = writable;
			return inspect(readable) + inspect(values);
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

TEST(ConstSliceRejectsElementWrite) {
	const char* source = R"(
		fn main() : void {
			var values : [2]i32 = [1, 2];
			var readable : []const i32 = values[:];
			readable[0] = 3;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstSliceRejectsMutableConversion) {
	const char* source = R"(
		fn mutate(values : []i32) : void {}

		fn main() : void {
			var values : [2]i32 = [1, 2];
			var readable : []const i32 = values[:];
			mutate(readable);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstArrayCanCreateReadOnlySlice) {
	const char* source = R"(
		fn inspect(values : []const i32) : i32 {
			return values[1];
		}

		fn main() : i32 {
			const values : [2]i32 = [7, 9];
			return inspect(values[:]);
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

TEST(ConstSliceReslicePreservesReadOnlyType) {
	const char* source = R"(
		fn main() : void {
			var values : [3]i32 = [2, 4, 6];
			var readable : []const i32 = values[:];
			var subview = readable[1:];
			subview[0] = 8;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstSliceCanBeReturned) {
	const char* source = R"(
		fn view(values : []const i32) : []const i32 {
			return values;
		}

		fn main() : i32 {
			var values : [2]i32 = [8, 9];
			const readable : []const i32 = view(values[:]);
			return readable[1];
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

TEST(ConstArrayRejectsMutableSliceConversion) {
	const char* source = R"(
		fn mutate(values : []i32) : void {}

		fn main() : void {
			const values : [2]i32 = [1, 2];
			mutate(values);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstArrayRejectsMutableSliceInitializer) {
	const char* source = R"(
		fn main() : void {
			const values : [2]i32 = [1, 2];
			var view : []i32 = values;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstArrayRejectsMutableSliceAssignment) {
	const char* source = R"(
		fn main() : void {
			var storage : [2]i32 = [0, 0];
			var view : []i32 = storage;
			const values : [2]i32 = [1, 2];
			view = values;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstArrayRejectsMutableSliceReturn) {
	const char* source = R"(
		fn view() : []i32 {
			const values : [2]i32 = [1, 2];
			return values;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstArrayFieldRejectsMutableSliceConversion) {
	const char* source = R"(
		struct Values {
			items : [2]i32;
		}

		fn main() : void {
			const values = Values { [1, 2] };
			var view : []i32 = values.items;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NestedConstSliceTypechecks) {
	const char* source = R"(
		fn inspect(values : [][]const i32) : i32 {
			return values[0][1];
		}

		fn main() : i32 {
			var values : [1][2]i32 = [[3, 7]];
			var rows : [1][]const i32 = [values[0][:]];
			return inspect(rows[:]);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ScalarSliceViewTypeInferenceAndLength) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 7;
			var view = value[:];
			view[0] = 11;
			return value + view.length as i32;
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

TEST(ConstScalarCanCreateReadOnlySlice) {
	const char* source = R"(
		fn inspect(value : []const i32) : i32 {
			return value[0];
		}

		fn main() : i32 {
			const value : i32 = 7;
			var view = value[:];
			return inspect(view);
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

TEST(ScalarSliceViewSupportsPrimitiveTypes) {
	const char* source = R"(
		fn main() : i32 {
			var signed : i8 = -3;
			var unsigned : u64 = 7;
			var real : f32 = 2.5;
			var signed_view = signed[:];
			var unsigned_view = unsigned[:];
			var real_view = real[:];
			signed_view[0] = 4;
			unsigned_view[0] = 9;
			real_view[0] = 3.5;
			return signed as i32 + unsigned as i32 + real as i32
				+ signed_view.length as i32 + unsigned_view.length as i32 + real_view.length as i32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(19, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ScalarSliceViewWorksForRefParameters) {
	const char* source = R"(
		fn update(value : ref i32) : void {
			var view : []i32 = value[:];
			view[0] += 5;
		}

		fn main() : i32 {
			var value : i32 = 10;
			update(ref value);
			return value;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(15, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ScalarSliceViewWorksForFieldsAndArrayElements) {
	const char* source = R"(
		struct Pair {
			first : i32;
			second : i32;
		}

		fn main() : i32 {
			var pair = Pair { 2, 3 };
			var values : [2]i32 = [4, 5];
			var first = pair.first[:];
			var second = values[1][:];
			first[0] += 10;
			second[0] += 20;
			return pair.first + values[1] + first.length as i32 + second.length as i32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(39, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ScalarSliceViewCanBeReslicedAndCopied) {
	const char* source = R"(
		fn consume(values : []i32) : i32 {
			return values[0];
		}

		fn main() : i32 {
			var value : i32 = 21;
			var view : []i32 = value[:];
			var subview : []i32 = view[:];
			var copy : []i32 = subview;
			copy[0] += 1;
			return consume(subview) + copy.length as i32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(23, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ScalarSliceViewRejectsNonAddressableSources) {
	const char* literal_source = R"(
		fn main() : void {
			var view : []i32 = 4[:];
		}
	)";
	EXPECT_COMPILE_FAIL(literal_source);

	const char* expression_source = R"(
		fn main() : void {
			var value : i32 = 4;
			var view : []i32 = (value + 1)[:];
		}
	)";
	EXPECT_COMPILE_FAIL(expression_source);

	const char* const_source = R"(
		fn main() : void {
			const value : i32 = 4;
			var view : []i32 = value[:];
		}
	)";
	EXPECT_COMPILE_FAIL(const_source);

	const char* parameter_source = R"(
		fn view(value : i32) : []i32 {
			return value[:];
		}
	)";
	EXPECT_COMPILE_FAIL(parameter_source);

	const char* array_parameter_source = R"(
		fn view(values : [2]i32) : []i32 {
			return values[:];
		}
	)";
	EXPECT_COMPILE_FAIL(array_parameter_source);

	const char* const_field_source = R"(
		struct Pair {
			value : i32;
		}

		fn main() : void {
			const pair = Pair { 4 };
			var view : []i32 = pair.value[:];
		}
	)";
	EXPECT_COMPILE_FAIL(const_field_source);
	return true;
}

TEST(SliceImplicitConversionFromTemporaryArray) {
	const char* source = R"(
		fn consume(values : []i32) : i32 {
			return values[0] + values[1];
		}

		fn make() : [2]i32 {
			var values : [2]i32 = undefined;
			values[0] = 17;
			values[1] = 25;
			return values;
		}

		fn main() : i32 {
			return consume(make());
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
			return s.length;
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

TEST(SliceLengthAndIndexRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[0] = 3;
			values[1] = 5;
			values[2] = 7;
			values[3] = 11;
			const slice : []i32 = values[1:3];
			return (slice.length as i32) * 100 + slice[0] * 10 + slice[1];
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
			return (sub.length as i32) * 100 + sub[0] * 10 + sub[1];
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
			return slice.length as i32;
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
			return slice.length as i32;
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
			while i < values.length {
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
	EXPECT_EQ(LS_RESULT_SUSPENDED, ls_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}

TEST(SliceElementWriteThroughParameterRuntime) {
	const char* source = R"(
		fn fill(values : []i32) : void {
			values[0] = 42;
		}

		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[0] = 0;
			fill(values);
			return values[0];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceStructFieldReadRuntime) {
	const char* source = R"(
		struct Body {
			x : i32;
			y : i32;
		}

		fn main() : i32 {
			var bodies : [3]Body = undefined;
			bodies[0] = Body { 7, 3 };
			bodies[1] = Body { 20, 30 };
			var slice : []Body = bodies[:];
			return slice[1].y + slice[0].x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(37, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceStructFieldWriteThroughParameterRuntime) {
	const char* source = R"(
		struct Body {
			x : i32;
			y : i32;
		}

		fn advance(bodies : []Body) : void {
			for i in 0..bodies.length {
				bodies[i].x -= 1;
				bodies[i].y = 100;
			}
		}

		fn main() : i32 {
			var bodies : [2]Body = undefined;
			bodies[0] = Body { 10, 0 };
			bodies[1] = Body { 20, 0 };
			advance(bodies);
			return bodies[0].x + bodies[1].x + bodies[1].y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(128, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceStructFieldRefOutOfBoundsFails) {
	const char* source = R"(
		struct Body {
			x : i32;
		}

		fn main(i : i32) : i32 {
			var bodies : [2]Body = undefined;
			var slice : []Body = bodies[:];
			slice[i].x = 1;
			return slice[0].x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;
	ls_push_i32(runtime, 2);
	EXPECT_EQ(LS_RESULT_SUSPENDED, ls_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualityComparesContentRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var left : [3]i32 = undefined;
			var right : [3]i32 = undefined;
			left[0] = 1; left[1] = 2; left[2] = 3;
			right[0] = 1; right[1] = 2; right[2] = 3;
			var result : i32 = 0;
			if left[:] == right[:] { result += 1; }
			right[2] = 4;
			if left[:] == right[:] { result += 10; }
			if left[:] != right[:] { result += 100; }
			return result;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(101, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualityUnequalLengthsAreUnequal) {
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[0] = 1; values[1] = 2; values[2] = 3; values[3] = 4;
			const two = values[0:2];
			const three = values[0:3];
			if two == three { return 0; }
			if two != three { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualitySameStorageIsEqual) {
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[0] = 7; values[1] = 8; values[2] = 9; values[3] = 10;
			const whole = values[:];
			const same = values[:];
			if whole == same { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualityMixesMutableAndConstViews) {
	const char* source = R"(
		fn main() : i32 {
			var values : [2]i32 = undefined;
			values[0] = 5; values[1] = 6;
			const writable : []i32 = values[:];
			const readable : []const i32 = values[:];
			if writable == readable { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualityEmptyAndNullSlicesAreEqual) {
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			const empty = values[2:2];
			const cleared : []i32 = null;
			if empty == cleared { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualityAcceptsEnumElements) {
	const char* source = R"(
		enum State { Idle, Running }

		fn main() : i32 {
			var left : [2]State = undefined;
			var right : [2]State = undefined;
			left[0] = .Idle; left[1] = .Running;
			right[0] = .Idle; right[1] = .Running;
			if left[:] == right[:] { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualityComparesFloatsNumericallyNotBitwise) {
	// +0.0 and -0.0 are equal but have different bit patterns, so a memcmp
	// implementation would report these slices as different.
	const char* source = R"(
		fn main() : i32 {
			var left : [2]f32 = undefined;
			var right : [2]f32 = undefined;
			left[0] = 0.0; left[1] = 1.5;
			right[0] = -0.0; right[1] = 1.5;
			if left[:] == right[:] { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualityComparesF64Elements) {
	const char* source = R"(
		fn main() : i32 {
			var left : [2]f64 = undefined;
			var right : [2]f64 = undefined;
			left[0] = 0.0; left[1] = 2.25;
			right[0] = -0.0; right[1] = 2.25;
			var result : i32 = 0;
			if left[:] == right[:] { result += 42; }
			right[1] = 2.5;
			if left[:] != right[:] { result += 1; }
			return result;
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

TEST(SliceEqualityRejectsStructElementsFails) {
	const char* source = R"(
		struct Point { x : i32; }

		fn main() : void {
			var left : [2]Point = undefined;
			var right : [2]Point = undefined;
			const equal = left[:] == right[:];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceEqualityRejectsStructElementsWithOperatorFails) {
	// An `operator ==` on the element type does not make slices of it comparable.
	const char* source = R"(
		struct Point { x : i32; }

		operator ==(a : Point, b : Point) : bool {
			return a.x == b.x;
		}

		fn main() : void {
			var left : [2]Point = undefined;
			var right : [2]Point = undefined;
			const equal = left[:] == right[:];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceEqualityRejectsSliceElementsFails) {
	const char* source = R"(
		fn main() : void {
			var values : [2][]i32 = undefined;
			const equal = values[:] == values[:];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceEqualityRejectsDifferentElementTypesFails) {
	const char* source = R"(
		fn main() : void {
			var ints : [4]i32 = undefined;
			var floats : [4]f32 = undefined;
			const equal = ints[:] == floats[:];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceEqualityRejectsSameSizedDifferentElementTypesFails) {
	// i32 and u32 have the same size; equality still requires the same element type.
	const char* source = R"(
		fn main() : void {
			var signed_values : [4]i32 = undefined;
			var unsigned_values : [4]u32 = undefined;
			const equal = signed_values[:] == unsigned_values[:];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceEqualityOfArraysRequiresSlicing) {
	// Static arrays have no built-in equality (see ArrayEqualityFails); slicing
	// them is the spelling that works.
	const char* source = R"(
		fn main() : i32 {
			var left : [3]i32 = undefined;
			var right : [3]i32 = undefined;
			left[0] = 1; left[1] = 2; left[2] = 3;
			right[0] = 1; right[1] = 2; right[2] = 3;
			if left[:] == right[:] { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceEqualityOnFunctionParameters) {
	const char* source = R"(
		fn same(a : []const u8, b : []const u8) : bool {
			return a == b;
		}

		fn main() : i32 {
			if same("lumix", "lumix") and not same("lumix", "script") { return 42; }
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// A comptime slice folds `.length` to a constant; the folded value must still carry a
// concrete type through bytecode generation.
TEST(ComptimeSliceLengthRuntime) {
	const char* source = R"(
		comptime text = "lumix";

		fn main() : i32 {
			return text.length as i32;
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

TEST(ComptimeSliceLengthNarrowedToI32Runtime) {
	const char* source = R"(
		comptime text = "lumix";

		fn main() : i32 {
			var count : i32 = text.length as i32;
			return count * 10;
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
