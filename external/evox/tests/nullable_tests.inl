TEST(NullablePromotionTypechecks) {
	const char* source = R"(
		struct Vec3 {
			x : f32;
			y : f32;
			z : f32;
		}

		fn length_if_present(v : ?Vec3) : f32 {
			if v != null {
				return v.x;
			}
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeNullableNarrowing) {
	const char* source = R"(
		fn read(v : ?i32) : i32 {
			if v != null { return v; }
			return 0;
		}
		comptime value : ?i32 = 42;
		comptime result = read(value);
		fn main() : void {}

	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeNullableStructFieldNarrowing) {
	const char* source = R"(
		struct Value { number : i32; }
		fn read(v : ?Value) : i32 {
			if v != null { return v.number; }
			return 0;
		}
		comptime value : ?Value = Value { 42 };
		comptime result = read(value);
		fn main() : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeNullableStructFieldRequiresNullCheck) {
	const char* source = R"(
		struct Value { number : i32; }
		fn read(v : ?Value) : i32 {
			return v.number;
		}
		comptime value : ?Value = Value { 42 };
		comptime result = read(value);
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableNarrowingPreservesConst) {
	const char* source = R"(
		fn main() : void {
			const value : ?i32 = 1;
			if value != null {
				value = 2;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableUseWithoutCheckFails) {
	const char* source = R"(
		struct Vec3 {
			x : f32;
			y : f32;
			z : f32;
		}

		fn bad(v : ?Vec3) : f32 {
			return v.x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableStructUseWithCheck) {
	const char* source = R"(
		struct Vec3 {
			x : f32;
			y : f32;
			z : f32;
		}

		fn bad(v : ?Vec3) : f32 {
			if v != null {
				return v.x;
			}
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableGuardClauseNarrowing) {
	const char* source = R"(
		struct Vec3 {
			x : f32;
		}

		fn get_x(v : ?Vec3) : f32 {
			if v == null { return 0; }
			return v.x;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableGlobalGuardClauseNarrowing) {
	const char* source = R"(
		struct Vec3 {
			x : f32;
		}

		var player : ?Vec3 = null;

		fn get_x() : f32 {
			if player == null { return 0; }
			return player.x;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableElseReturnNarrowing) {
	const char* source = R"(
		struct Vec3 {
			x : f32;
		}

		fn get_x(v : ?Vec3) : f32 {
			if v != null {
			} else {
				return 0;
			}
			return v.x;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableElseReturnDeclaration) {
	const char* source = R"(
		struct Value { number : i32; }

		fn consume(v : ?Value) : void {
			var value : Value = v else return;
			const number = value.number;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableElseReturnDeclarationWithNullInitializer) {
	const char* source = R"(
		fn main() : void {
			var value : ?i32 = null;
			var unwrapped : i32 = value else return;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableElseReturnScalar) {
	const char* source = R"(
		fn unwrap(v : ?i32) : void {
			var value : i32 = v else return;
			const doubled = value * 2;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableElseReturnPointer) {
	const char* source = R"(
		fn write(v : ?*i32) : void {
			var ptr : *i32 = v else return;
			ptr.* = 42;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableElseReturnCallExpression) {
	const char* source = R"(
		fn find() : ?i32 { return 42; }

		fn consume() : void {
			var value : i32 = find() else return;
			const result = value + 1;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(BytecodeNullableElseReturn) {
	const char* source = R"(
		var result : i32 = -1;
		var calls : i32 = 0;

		fn find(present : bool) : ?i32 {
			calls += 1;
			if present { return 42; }
			return null;
		}

		fn consume(present : bool) : void {
			var value : i32 = find(present) else return;
			result = value;
		}

		fn main() : i32 {
			consume(true);
			const successful = result;
			const successful_calls = calls;
			consume(false);
			return successful * 100 + successful_calls * 10 + calls;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(4212, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(NullableElseReturnNonVoidFunctionFails) {
	const char* source = R"(
		fn unwrap(v : ?i32) : i32 {
			var value : i32 = v else return;
			return value;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableElseReturnInfersTarget) {
	const char* source = R"(
		fn consume(v : ?i32) : void {
			var value = v else return;
			const doubled = value * 2;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NullableComparisonRequiresNullCheckFails) {
	const char* source = R"(
		fn main() : void {
			var x : ?i32 = 1;
			if x < 2 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableEqualityRequiresNullCheckFails) {
	const char* source = R"(
		fn main() : void {
			var x : ?i32 = 1;
			if x == 1 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableQualifiedTypeRetainsNullableWrapperFails) {
	const char* source = R"(
		struct Vec2 {
			x : f32;
			y : f32;
		}

		fn main() : void {
			var x : ?Vec2 = null;
			var y : Vec2 = x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableEnumMemberAccessRequiresNullCheckFails) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}

		fn use(s : State) : void {}

		fn main() : void {
			var state : ?State = .Idle;
			use(state);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableUnaryRequiresNullCheckFails) {
	const char* source = R"(
		fn main() : void {
			var x : ?i32 = 1;
			const y = not x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullablePointerParameterTypechecks) {
	const char* source = R"(
		fn clear(v : ?*i32) : void {
			if v != null { v.* = 0; }
		}

		fn main() : void {
			var x : i32 = 10;
			var p : ?*i32 = &x;
			clear(p);
		}
	)";
	EXPECT_COMPILE(source);
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

TEST(NullWithoutTypeAnnotationFails) {
	const char* source = R"(
		fn main() : void {
			var a = null;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}



TEST(BytecodeNullableLocalNullCheck) {
	const char* source = R"(
		fn main() : i32 {
			var value : ?i32 = null;
			if value == null {
				return 42;
			}
			return 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNullableStructComparison) {
	const char* source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		}
		fn main() : i32 {
			var v : ?Vec2 = null;
			if v != null {
				return v.x + v.y;
			}
			return 42;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNullableReturnNull) {
	const char* source = R"(
		fn main() : ?i32 {
			return null;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	// A nullable is a flag byte followed by the packed payload.
	u32 result_size = 0;
	const u8* result = (const u8*)ex_call_result(runtime, &result_size);
	EXPECT_TRUE(result != nullptr);
	EXPECT_EQ(5u, result_size);
	EXPECT_EQ(0, result[0]);
	i32 payload = -1;
	memcpy(&payload, result + 1, sizeof(payload));
	EXPECT_EQ(0, payload);
	ex_runtime_destroy(runtime);

	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNullableReturnValue) {
	const char* source = R"(
		fn main() : ?i32 {
			return 7;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	// A nullable is a flag byte followed by the packed payload.
	u32 result_size = 0;
	const u8* result = (const u8*)ex_call_result(runtime, &result_size);
	EXPECT_TRUE(result != nullptr);
	EXPECT_EQ(5u, result_size);
	EXPECT_EQ(1, result[0]);
	i32 payload = 0;
	memcpy(&payload, result + 1, sizeof(payload));
	EXPECT_EQ(7, payload);
	ex_runtime_destroy(runtime);

	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeNullableThreeBytePayloadNullReturnHasFullSize) {
	const char* source = R"(
		struct Packed {
			a : bool;
			b : u16;
		}

		fn none() : ?Packed {
			return null;
		}

		fn main() : i32 {
			var poison : bool = true;
			var pad0 : bool = false;
			var pad1 : bool = false;
			if none() == null {
				return 42;
			}
			if poison or pad0 or pad1 {
				return 1;
			}
			return 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, nullptr);
	EXPECT_TRUE(bytecode != nullptr);

	ex_runtime* runtime = ex_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));

	ex_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}
