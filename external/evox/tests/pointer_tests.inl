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

TEST(ReturnPointerThroughLocalSliceToCallerStorage) {
	const char* source = R"(
		fn first(values : []i32) : *i32 {
			var local_view : []i32 = values;
			return &local_view[0];
		}

		fn main() : i32 {
			var values : [2]i32 = [10, 32];
			const ptr = first(values);
			ptr.* = 42;
			return values[0];
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

TEST(PointerParameterAllowsRuntimeIndexedNestedField) {
	const char* source = R"(
		struct Stats { hp : i32; }
		struct Player { stats : Stats; }

		fn bump(v : *i32) : void {
			v.* += 1;
		}

		fn main() : void {
			var players : [2]Player = undefined;
			var i : i32 = 1;
			bump(&players[i].stats.hp);
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(OptimizedPointerParameterWrite) {
	const char* source = R"(
		fn change(v : *i32) : void { v.* = 3; }
		fn main() : i32 { var value : i32 = 0; change(&value); return value; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	ex_bytecode_compile_options options = {};
	options.optimize = true;
	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, &options);
	EXPECT_TRUE(bytecode != nullptr);
	ex_runtime* runtime = ex_runtime_create(bytecode, &module_host);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(3, ex_task_to_i32(runtime, -1));
	test_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(OptimizedVertexMapPointerCallback) {
	const char* source = R"(
		struct Vec3 { x : f32; y : f32; z : f32; }
		struct Vec2 { x : f32; y : f32; }
		struct Vertex { position : Vec3; normal : Vec3; tangent : Vec3; uv : Vec2; }
		fn modify(p : *Vec3, n : *Vec3, t : *Vec3, uv : *Vec2) : void {
			p.* = {p.x + 1.0, p.y + 2.0, p.z + 3.0};
			n.* = {0.0, 1.0, 0.0};
			t.* = {0.0, 0.0, 1.0};
			uv.* = {uv.x * 2.0, uv.y + 0.75};
		}
		fn map(vertices : []Vertex, callback : fn(*Vec3, *Vec3, *Vec3, *Vec2) : void) : void {
			for i in 0..vertices.length {
				callback(&vertices[i].position, &vertices[i].normal, &vertices[i].tangent, &vertices[i].uv);
			}
		}
		fn main() : i32 {
			var vertices : [2]Vertex = [
				{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0}},
				{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0}}
			];
			map(vertices[:], modify);
			if vertices[0].position.x != 1.0 or vertices[0].normal.y != 1.0
				or vertices[1].tangent.z != 1.0 or vertices[1].uv.y != 0.75 { return 0; }
			return 42;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	ex_bytecode_compile_options options = {};
	options.optimize = true;
	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, &options);
	EXPECT_TRUE(bytecode != nullptr);
	ex_runtime* runtime = ex_runtime_create(bytecode, &module_host);
	EXPECT_TRUE(runtime != nullptr);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
	test_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(41, ex_task_to_i32(runtime, -1));
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
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_task_to_i32(runtime, -1));
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
