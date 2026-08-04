TEST(PointerParameterTypechecks) {
	const char* source = R"(
		fn increment(v : *i32) : void {
			v.* += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(&x);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(PointerParameterRequiresPointerArgument) {
	const char* source = R"(
		fn increment(v : *i32) : void {
			v.* += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(AddressOfRequiresAddressableStorage) {
	const char* source = R"(
		fn increment(v : *i32) : void {
			v.* += 1;
		}

		fn main() : void {
			increment(&(1 + 2));
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DereferenceRequiresPointer) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 42;
			return value.*;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(PointerParameterRejectsReadOnlyStorage) {
	const char* source = R"(
		fn increment(v : *i32) : void {
			v.* += 1;
		}

		fn main() : void {
			const x : i32 = 10;
			increment(&x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstPointerCanReadPointee) {
	const char* source = R"(
		fn read(value : *const i32) : i32 {
			return value.*;
		}

		fn main() : i32 {
			var value : i32 = 42;
			return read(&value);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ConstPointerCannotWritePointee) {
	const char* source = R"(
		fn clear(value : *const i32) : void {
			value.* = 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MutablePointerConvertsToConstPointer) {
	const char* source = R"(
		fn read(value : *const i32) : i32 {
			return value.*;
		}

		fn main() : i32 {
			var value : i32 = 42;
			var writable : *i32 = &value;
			var readable : *const i32 = writable;
			return read(readable);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ConstPointerCannotConvertToMutablePointer) {
	const char* source = R"(
		fn write(value : *i32) : void {
			value.* = 0;
		}

		fn main() : void {
			var value : i32 = 42;
			var readable : *const i32 = &value;
			write(readable);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstPointerBindingStillAllowsMutablePointee) {
	const char* source = R"(
		fn main() : i32 {
			var value : i32 = 41;
			const pointer : *i32 = &value;
			pointer.* += 1;
			return value;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(PointerParameterTypeMismatchFails) {
	const char* source = R"(
		fn increment(v : *f32) : void {
			v.* += 1.0;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(&x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(PointerParameterAllowsNestedMutableField) {
	const char* source = R"(
		struct Stats { hp : i32; }
		struct Player { stats : Stats; }

		fn bump(v : *i32) : void {
			v.* += 1;
		}

		fn main() : void {
			var p = Player { Stats { 10 } };
			bump(&p.stats.hp);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(BytecodePointerParameterCall) {
	const char* source = R"(
		fn increment(v : *i32) : void {
			v.* += 1;
		}

		fn main() : i32 {
			var x : i32 = 41;
			increment(&x);
			return x;
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

TEST(BytecodePointerParameterForwarding) {
	const char* source = R"(
		fn increment(v : *i32) : void {
			v.* += 1;
		}

		fn forward(v : *i32) : void {
			increment(v);
		}

		fn main() : i32 {
			var x : i32 = 40;
			forward(&x);
			return x;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(41, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UFCSPointerReceiverRuntime) {
	const char* source = R"(
		struct Counter { value : i32; }

		fn increment(counter : *Counter, amount : i32) : void {
			counter.value += amount;
		}

		fn main() : i32 {
			var counter = Counter { 40 };
			var pointer : *Counter = &counter;
			pointer.increment(2);
			return counter.value;
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

TEST(UFCSPointerReceiverRequiresExplicitAddress) {
	const char* source = R"(
		struct Counter { value : i32; }

		fn increment(counter : *Counter) : void {
			counter.value += 1;
		}

		fn main() : void {
			var counter = Counter { 0 };
			counter.increment();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullablePointerRequiresNullCheck) {
	const char* source = R"(
		fn read(pointer : ?*i32) : i32 {
			return pointer.*;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}
