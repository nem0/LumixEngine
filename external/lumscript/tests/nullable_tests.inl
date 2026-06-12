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

		fn main() : void {
			var state : ?State = .Idle;
			const value = state.Idle;
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

TEST(RefArgumentRejectsNullableTargetType) {
	const char* source = R"(
		fn clear(v : ref ?i32) : void {
			v = null;
		}

		fn main() : void {
			var x : ?i32 = 10;
			clear(ref x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

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

TEST(BytecodeNullableReturnNull) {
	const char* source = R"(
		fn main() : ?i32 {
			return null;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_bool(runtime, -2));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	ls_runtime_destroy(runtime);

	ls_bytecode_destroy(bytecode);
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
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_bool(runtime, -2));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	ls_runtime_destroy(runtime);

	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

