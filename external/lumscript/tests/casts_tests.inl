TEST(BytecodeExplicitCastNumeric) {
	const char* source = R"(
		fn main() : f32 {
			return 1 as f32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(1.0f, ls_to_f32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(BytecodeExplicitCastEnumToInteger) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
		fn main() : i32 {
			const s : State = .Running;
			return s as i32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(RuntimeCasts) {
	const char* source = R"(
		fn to_f32() : f32 {
			const x : i32 = 10;
			return x as f32;
		}

		fn to_i32() : i32 {
			const x : f32 = 12.75;
			return x as i32;
		}

		fn to_bool() : bool {
			const x : i32 = 1;
			return x as bool;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("to_f32")));
	EXPECT_FLOAT_EQ(10, ls_to_f32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("to_i32")));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("to_bool")));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(IntegerToEnumCastAllowsAnyIntegerRuntime) {
	const char* source = R"(
		enum State {
			Idle,
			Running
		}
		fn to_state(v : i32) : State {
			return v as State;
		}

		fn to_i32(v : i32) : i32 {
			const s : State = v as State;
			return s as i32;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	ls_push_i32(runtime, 123);
	EXPECT_TRUE(ls_call(runtime, toLs("to_state")));
	EXPECT_TRUE(ls_call(runtime, toLs("to_i32")));
	EXPECT_EQ(123, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(IntegerSignExtensionRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var i : i8 = -128;
			return i as i32;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(-128, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(RuntimeCastOverflowBoundaries) {
	const char* source = R"(
		fn i8_from_127() : i32 { return 127 as i8 as i32; }
		fn i8_from_128() : i32 { return 128 as i8 as i32; }
		fn i8_from_255() : i32 { return 255 as i8 as i32; }

		fn u8_from_neg1() : i32 { return (-1 as u8) as i32; }
		fn u8_from_255() : i32 { return 255 as u8 as i32; }
		fn u8_from_256() : i32 { return 256 as u8 as i32; }

		fn i16_from_32767() : i32 { return 32767 as i16 as i32; }
		fn i16_from_32768() : i32 { return 32768 as i16 as i32; }
		fn i16_from_65535() : i32 { return 65535 as i16 as i32; }

		fn u16_from_neg1() : i32 { return (-1 as u16) as i32; }
		fn u16_from_65535() : i32 { return 65535 as u16 as i32; }
		fn u16_from_65536() : i32 { return 65536 as u16 as i32; }

		fn i32_from_2147483647() : i32 { return 2147483647 as i32; }
		fn i32_from_2147483648() : i32 { return 2147483648 as i32; }
		fn u32_from_neg1() : u64 { return (-1 as u32) as u64; }
		fn u32_from_4294967295() : u64 { return 4294967295 as u32 as u64; }
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("i8_from_127")));
	EXPECT_EQ(127, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("i8_from_128")));
	EXPECT_EQ(-128, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("i8_from_255")));
	EXPECT_EQ(-1, ls_to_i32(runtime, -1));

	EXPECT_TRUE(ls_call(runtime, toLs("u8_from_neg1")));
	EXPECT_EQ(255, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("u8_from_255")));
	EXPECT_EQ(255, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("u8_from_256")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	EXPECT_TRUE(ls_call(runtime, toLs("i16_from_32767")));
	EXPECT_EQ(32767, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("i16_from_32768")));
	EXPECT_EQ(-32768, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("i16_from_65535")));
	EXPECT_EQ(-1, ls_to_i32(runtime, -1));

	EXPECT_TRUE(ls_call(runtime, toLs("u16_from_neg1")));
	EXPECT_EQ(65535, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("u16_from_65535")));
	EXPECT_EQ(65535, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("u16_from_65536")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	EXPECT_TRUE(ls_call(runtime, toLs("i32_from_2147483647")));
	EXPECT_EQ(2147483647, ls_to_i32(runtime, -1));
	EXPECT_TRUE(ls_call(runtime, toLs("i32_from_2147483648")));
	EXPECT_TRUE(ls_to_i32(runtime, -1) == (i32)0x80000000u);

	EXPECT_TRUE(ls_call(runtime, toLs("u32_from_neg1")));
	EXPECT_TRUE(ls_to_u64(runtime, -1) == 4294967295ull);
	EXPECT_TRUE(ls_call(runtime, toLs("u32_from_4294967295")));
	EXPECT_TRUE(ls_to_u64(runtime, -1) == 4294967295ull);
	CAPI_END(module);
	return true;
}
