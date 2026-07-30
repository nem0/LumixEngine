TEST(SizeofProducesComptimeInt) {
	const char* source = R"(
		fn main() : void {
			var d : [sizeof(i32)]i32 = undefined;
			d[0] = 1;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(AlignofProducesComptimeInt) {
	const char* source = R"(
		fn main() : void {
			var d : [alignof(i32)]i32 = undefined;
			d[0] = 1;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SizeofLocalComptimeType) {
	const char* source = R"(
		fn main() : void {
			comptime T = i32;
			const s = sizeof(T);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(SizeofLocalComptimeValueFails) {
	const char* source = R"(
		fn main() : void {
			comptime T : i32 = 42;
			const s = sizeof(T);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SizeofRequiresTypeNotValueFails) {
	const char* source = R"(
		fn main() : void {
			const x : i32 = 4;
			const s = sizeof(x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SizeofAndAlignofRejectUntypedNumerics) {
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1;
		comptime size = sizeof(value);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1.5;
		comptime size = sizeof(value);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1;
		comptime alignment = alignof(value);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1.5;
		comptime alignment = alignof(value);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime size = sizeof(typeof(1));
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime alignment = alignof(typeof(1.5));
	)");
	return true;
}

TEST(IsizeVariableTypechecks) {
	const char* source = R"(
		fn main() : void {
			var n : isize = 0;
			n += 1;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IsizeIsSignedAllowsNegative) {
	const char* source = R"(
		fn main() : void {
			var n : isize = -1;
			n -= 1;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IsizeNoImplicitConversionFromI32Fails) {
	const char* source = R"(
		fn main() : void {
			var a : i32 = 1;
			var n : isize = a;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LengthReturnsIsize) {
	const char* source = R"(
		fn count(s : []i32) : isize {
			return length(s);
		}

		fn main() : void {
			var values : [4]i32 = undefined;
			const n : isize = count(values);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IsizeAllocArgumentTypechecks) {
	const char* source = R"(
		import "std:mem" as mem

		fn main() : void {
			var size : isize = 16;
			var align : isize = 4;
			var raw : []byte = mem.alloc(size, align);
			mem.free(raw);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IsizeIndexTypechecks) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var i : isize = 2;
			values[i] = 1;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IsizeIndexRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			values[2] = 42;
			var i : isize = 2;
			return values[i];
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

TEST(ByteSliceParameterTypechecks) {
	const char* source = R"(
		fn consume(memory : []byte) : void {}

		fn main() : void {
			var values : [4]byte = undefined;
			consume(values);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ByteIsDistinctFromU8) {
	const char* source = R"(
		fn main() : void {
			var raw : [4]byte = undefined;
			var numbers : []u8 = raw;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ByteAdditionFails) {
	const char* prerequisite = R"(
		fn main() : void {
			var a : byte = undefined;
			var b : byte = undefined;
		}
	)";
	EXPECT_COMPILE(prerequisite);

	const char* source = R"(
		fn main() : void {
			var a : byte = undefined;
			var b : byte = undefined;
			var c = a + b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ByteSubtractionFails) {
	const char* source = R"(
		fn main() : void {
			var a : byte = undefined;
			var b : byte = undefined;
			var c = a - b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ByteMultiplicationFails) {
	const char* source = R"(
		fn main() : void {
			var a : byte = undefined;
			var b : byte = undefined;
			var c = a * b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ByteCompoundAddFails) {
	const char* source = R"(
		fn main() : void {
			var a : byte = undefined;
			var b : byte = undefined;
			a += b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ByteLiteralArithmeticFails) {
	const char* source = R"(
		fn main() : void {
			var a : byte = undefined;
			var c = a + 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AllocReturnsByteSlice) {
	const char* source = R"(
		import "std:mem" as mem

		fn main() : void {
			var raw : []byte = mem.alloc(16, 4);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FreeTakesByteSlice) {
	const char* source = R"(
		import "std:mem" as mem

		fn main() : void {
			var raw : []byte = mem.alloc(16, 4);
			mem.free(raw);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FreeRejectsTypedSliceFails) {
	const char* source = R"(
		import "std:mem" as mem

		fn main() : void {
			var values : [4]i32 = undefined;
			mem.free(values);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AllocWithSizeofAndAlignof) {
	const char* source = R"(
		import "std:mem" as mem

		fn main() : void {
			var raw : []byte = mem.alloc(4 * sizeof(i32), alignof(i32));
			mem.free(raw);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ByteSliceReinterpretAsTypedSlice) {
	const char* source = R"(
		import "std:mem" as mem

		fn main() : void {
			var raw : []byte = mem.alloc(4 * sizeof(i32), alignof(i32));
			var ints : []i32 = raw as []i32;
			ints[0] = 42;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypedSliceReinterpretAsByteSlice) {
	const char* source = R"(
		fn main() : void {
			var values : [4]i32 = undefined;
			var bytes : []byte = values[:] as []byte;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ReinterpretBetweenTypedSlicesFails) {
	const char* source = R"(
		fn main() : void {
			var values : [4]f32 = undefined;
			var ints : []i32 = values[:] as []i32;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SliceReinterpretToBytesLengthRuntime) {
	// Reinterpreting typed elements as bytes exposes their raw byte width.
	const char* source = R"(
		fn main() : i32 {
			var values : [4]i32 = undefined;
			var bytes : []byte = values[:] as []byte;
			return length(bytes) as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(16, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(SliceReinterpretRoundTripLengthRuntime) {
	// Round-trip reinterpretation preserves length.
	const char* source = R"(
		fn main() : i32 {
			var arr : [4]i32 = undefined;
			var slice : []i32 = arr[:];
			var bytes : []byte = slice as []byte;
			var back : []i32 = bytes as []i32;
			return length(back) as i32;
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

TEST(AllocWriteReadFreeRuntime) {
	const char* source = R"(
		import "std:mem" as mem

		fn main() : i32 {
			var raw : []byte = mem.alloc(4 * sizeof(i32), alignof(i32));
			var ints : []i32 = raw as []i32;
			ints[0] = 20;
			ints[1] = 22;
			const result = ints[0] + ints[1];
			mem.free(raw);
			return result;
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

TEST(ByteSliceReinterpretLengthRuntime) {
	const char* source = R"(
		import "std:mem" as mem

		fn main() : i32 {
			var raw : []byte = mem.alloc(4 * sizeof(i32), alignof(i32));
			var ints : []i32 = raw as []i32;
			const count = length(ints) as i32;
			mem.free(raw);
			return count;
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

TEST(MemberNullableAssignmentRuntime) {
	// Issue #2: member = with nullable RHS should use compileExpressionAsType.
	const char* source = R"(
		struct S {
			opt : ?i32;
		}

		fn main() : ?i32 {
			var s = S { null };
			s.opt = 42;
			return s.opt;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	// A nullable is a flag byte followed by the packed payload.
	u32 result_size = 0;
	const u8* result = (const u8*)ls_call_result(runtime, &result_size);
	EXPECT_TRUE(result != nullptr);
	EXPECT_EQ(5u, result_size);
	EXPECT_EQ(1, result[0]);
	i32 payload = 0;
	memcpy(&payload, result + 1, sizeof(payload));
	EXPECT_EQ(42, payload);
	CAPI_END(module);
	return true;
}

TEST(MemberArrayToSliceAssignmentRuntime) {
	const char* source = R"(
		struct S {
			items : []i32;
		}

		fn main() : i32 {
			var s : S = undefined;
			var arr : [3]i32 = undefined;
			arr[0] = 1;
			arr[1] = 2;
			arr[2] = 3;
			s.items = arr;
			return (length(s.items) as i32) + s.items[1];
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
