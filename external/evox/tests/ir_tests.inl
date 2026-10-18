// Regression: inlined by-value aggregate parameters have no local alloca.
// Field/index lowering requests an address, which used to assert in IDENTIFIER.
TEST(ir_inline_aggregate_parameter_address) {
	const char* source = R"(
		struct Vec { x : i32; y : i32; }
		struct Nested { value : Vec; }
		var input : i32 = 3;
		fn squared(v : Vec) : i32 { return v.x * v.x + v.y * v.y; }
		fn nested(v : Nested) : i32 { return v.value.x + v.value.y; }
		fn indexed(v : [2]i32) : i32 { return v[0] + v[1]; }
		fn main() : i32 {
			var vector_arg : Vec = {input, 4};
			var nested_arg : Nested = {vector_arg};
			var array_arg : [2]i32 = [input, 4];
			return squared(vector_arg) + nested(nested_arg) + indexed(array_arg);
		}
	)";
	for (bool optimize : {false, true}) {
		CAPI_BEGIN(module, diagnostics);
		EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
		ex_bytecode_compile_options options = {};
		options.optimize = optimize;
		ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, &options);
		EXPECT_TRUE(bytecode != nullptr);
		ex_runtime* runtime = ex_runtime_create(bytecode, &module_host);
		EXPECT_TRUE(runtime != nullptr);
		EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
		EXPECT_EQ(ex_task_to_i32(runtime, -1), 39);
		test_runtime_destroy(runtime);
		ex_bytecode_destroy(bytecode);
		CAPI_END(module);
	}
	return true;
}

TEST(ir_to_bytecode_basic) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { return 2 + 3; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_compare_and_if) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { if 2 < 3 { return 7; } else { return 9; } }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 7);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_locals) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var x : i32 = 2; x += 3; return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_module_call) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn helper(x : i32) : i32 { return x + 1; } fn main() : i32 { return helper(2); }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 3);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_globals) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("var g : i32 = 4; fn main() : i32 { g += 2; return g; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

static void irNativeAdd(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, i32, value);
	EX_RESULT(frame, value + 5);
}

TEST(ir_to_bytecode_while_loop) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var x : i32 = 0; while x < 3 { x += 1; } return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 3);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_loop_control) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var x : i32 = 0; while x < 10 { x += 1; if x == 3 { continue; } if x == 5 { break; } } return x; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 5);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_for_range) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var sum : i32 = 0; for i in 0 .. 4 { sum += i; } return sum; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_access) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; values[1] = 8; return values[1]; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 8);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_bounds_check) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		fn valid() : i32 { var values : [3]i32 = undefined; var index : i64 = 1; values[index] = 8; return values[index]; }
		fn negative() : i32 { var values : [3]i32 = undefined; var index : i64 = -1; return values[index]; }
		fn past_end() : i32 { var values : [3]i32 = undefined; var index : u64 = 3; values[index] = 8; return 0; }
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	test_diagnostics.output_enabled = false;
	EXPECT_EQ(test_call(runtime, toLs("valid")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 8);

	// Runtime errors suspend with EX_DEBUG_PAUSE_ERROR instead of failing the
	// call (see DebugErrorSuspendsWhenEnabled); the host aborts to unwind.
	ex_debug_event event;
	EXPECT_EQ(test_call(runtime, toLs("negative")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);

	EXPECT_EQ(test_call(runtime, toLs("past_end")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);
	CAPI_END(module);
	return true;
}

// Exercises every index width through LOAD_INDEXED/STORE_INDEXED: unsigned
// kinds on the success path (previously only ever seen failing), and narrow
// signed kinds wrapping through i64 to u64 when rejected.
TEST(ir_to_bytecode_array_index_kinds) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		fn load_u8() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var index : u8 = 2; return values[index]; }
		fn load_u16() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var index : u16 = 1; return values[index]; }
		fn load_u32() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var index : u32 = 2; return values[index]; }
		fn store_u8() : i32 { var values : [3]i32 = undefined; var index : u8 = 2; values[index] = 7; return values[index]; }
		fn negative_i8() : i32 { var values : [3]i32 = undefined; var index : i8 = -1; return values[index]; }
		fn negative_i16() : i32 { var values : [3]i32 = undefined; var index : i16 = -1; return values[index]; }
		fn negative_i32() : i32 { var values : [3]i32 = undefined; var index : i32 = -1; return values[index]; }
		fn past_end_u8() : i32 { var values : [3]i32 = undefined; var index : u8 = 3; return values[index]; }
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_EQ(test_call(runtime, toLs("load_u8")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	EXPECT_EQ(test_call(runtime, toLs("load_u16")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 5);
	EXPECT_EQ(test_call(runtime, toLs("load_u32")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	EXPECT_EQ(test_call(runtime, toLs("store_u8")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 7);

	test_diagnostics.output_enabled = false;
	ex_debug_event event;
	EXPECT_EQ(test_call(runtime, toLs("negative_i8")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);

	EXPECT_EQ(test_call(runtime, toLs("negative_i16")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);

	EXPECT_EQ(test_call(runtime, toLs("negative_i32")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);

	EXPECT_EQ(test_call(runtime, toLs("past_end_u8")), EX_CALL_RESULT_INDEX_OUT_OF_BOUNDS);
	EXPECT_TRUE(ex_task_get_state(runtime) == EX_TASK_SUSPENDED);
	EXPECT_TRUE(ex_debug_pause_event(runtime, &event));
	EXPECT_EQ((int)EX_DEBUG_PAUSE_ERROR, (int)event.reason);
	test_abort(runtime);
	EXPECT_TRUE(ex_task_get_state(runtime) != EX_TASK_SUSPENDED);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_undefined_array_and_greater_than) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = undefined; for i in 0..3 { values[i] = i; } if values[2] > 1 { return values[2]; } return 0; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 2);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_array_for) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 1, 2, 3 ]; var sum : i32 = 0; for value in values { sum += value; } return sum; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 6);
	CAPI_END(module);
	return true;
}

TEST(ir_to_bytecode_slice_access) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs("fn main() : i32 { var values : [3]i32 = [ 4, 5, 6 ]; var view : []i32 = values[0:3]; view[1] = 8; return view[1]; }"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(test_call(runtime, toLs("main")), EX_CALL_RESULT_OK);
	EXPECT_EQ(ex_task_to_i32(runtime, -1), 8);
	CAPI_END(module);
	return true;
}

// Calls to small straight-line functions (guards, assignments, one trailing return) are inlined as block
// expressions. The results must match the plain calls with optimization on and off.
static i32 inlineRun(const char* source, ex_call_result expected_result, bool optimize) {
	CAPI_BEGIN(module, diagnostics);
	if (!ex_module_compile(module, toLs(source), makeStringView("inline_test"), nullptr, nullptr)) return -1000;
	ex_bytecode_compile_options options = {};
	options.optimize = optimize;
	ex_bytecode* bytecode = ex_bytecode_compile(module, &module_host, &options);
	if (!bytecode) return -1001;
	ex_runtime* runtime = ex_runtime_create(bytecode, &module_host);
	if (!runtime) return -1002;
	if (expected_result != EX_CALL_RESULT_OK) diagnostics.diagnostics.output_enabled = false;
	const ex_call_result result = test_call(runtime, toLs("main"));
	i32 value = result == expected_result ? 0 : -1003;
	if (result == EX_CALL_RESULT_OK) value = ex_task_to_i32(runtime, -1);
	test_runtime_destroy(runtime);
	ex_bytecode_destroy(bytecode);
	CAPI_END(module);
	return value;
}

TEST(ir_inline_statement_bodies) {
	const char* source = R"(
		var x : i32 = 1000;
		struct Buffer { values : [4]i32; size : i32; }
		fn put(b : *Buffer, i : i32, v : i32) : void {
			if i < 0 or i >= b.size { panic("out of range"); }
			b.values[i] = v;
		}
		fn get(b : *Buffer, i : i32) : i32 {
			if i < 0 or i >= b.size { panic("out of range"); }
			return b.values[i];
		}
		fn global_x(v : i32) : i32 {
			const doubled = v * 2;
			return doubled + x;
		}
		fn recurse(n : i32) : i32 {
			if n <= 0 { return 0; }
			return n + recurse(n - 1);
		}
		fn twice(b : *Buffer, i : i32) : i32 {
			return get(b, i) * 2;
		}
		fn main() : i32 {
			var b : Buffer = zero;
			b.size = 4;
			var y : i32 = 1;
			for i in 0..4 { put(&b, i, i + 10); }
			var sum = 0;
			for i in 0..4 { sum += get(&b, i); }
			// temporaries held by the enclosing expression survive the inlined body
			const mixed = 1000 * get(&b, 1) + 100 * get(&b, 2) + 10 * twice(&b, 3);
			// the callee reads the global x
			return sum + mixed + global_x(3) + y + recurse(4);
		}
	)";
	// sum = 10 + 11 + 12 + 13 = 46, mixed = 11000 + 1200 + 260 = 12460, global_x = 6 + 1000, x = 1, recurse = 10
	for (bool optimize : {false, true}) EXPECT_EQ(46 + 12460 + 1006 + 1 + 10, inlineRun(source, EX_CALL_RESULT_OK, optimize));
	return true;
}

TEST(ir_inline_panic_in_body) {
	const char* source = R"(
		fn get(values : []i32, i : i32) : i32 {
			if i < 0 or i >= values.length as i32 { panic("out of range"); }
			return values[i];
		}
		fn main() : i32 {
			var values : [2]i32 = [1, 2];
			return get(values, 1) + get(values, 2);
		}
	)";
	for (bool optimize : {false, true}) EXPECT_EQ(0, inlineRun(source, EX_CALL_RESULT_PANIC, optimize));
	return true;
}

TEST(ir_inline_early_returns) {
	const char* source = R"(
		fn pick(n : i32, a : i32, b : i32) : i32 {
			if n == 0 { return a; }
			if n == 1 {
				return b;
			} else {
				if n == 2 { return a + b; }
			}
			return a * b;
		}
		var sink : i32 = 0;
		fn store(n : i32, v : i32) : void {
			if n < 0 { return; }
			sink += v;
			if n == 0 { return; }
			sink += v;
		}
		fn main() : i32 {
			store(-1, 100);
			store(0, 10);
			store(1, 1);
			// results used inside a larger expression with live temporaries
			return 1000 * pick(0, 3, 4) + 100 * pick(1, 3, 4) + 10 * pick(2, 3, 4) + pick(3, 3, 4) + sink * 100000;
		}
	)";
	// 3000 + 400 + 70 + 12 + (10 + 2) * 100000
	for (bool optimize : {false, true}) EXPECT_EQ(3000 + 400 + 70 + 12 + 1200000, inlineRun(source, EX_CALL_RESULT_OK, optimize));
	return true;
}

// Inlined parameters read immutable locals (parameters, `const`, range loop variables) in place. A mutable
// `var` argument is still copied, so a callee writing it through a pointer cannot change the parameter.
TEST(ir_inline_parameter_aliasing) {
	const char* source = R"(
		fn poke(p : *i32, a : i32) : i32 {
			p.* = 100;
			return a + 1;
		}
		fn add(a : i32, b : i32) : i32 {
			const c = a + b;
			return c * 2;
		}
		fn twice(v : i32) : i32 {
			return add(v, v);
		}
		fn main() : i32 {
			var x : i32 = 1;
			const first = poke(&x, x);
			const k : i32 = 7;
			var sum = 0;
			for i in 0..4 { sum += add(i, k) + twice(i); }
			return first * 1000000 + x * 1000 + sum;
		}
	)";
	// first = 2 (parameter is a copy of the old x), x = 100, sum = sum over i of (2 * (i + 7) + 4 * i) = 2 * 34 + 4 * 6 = 92
	for (bool optimize : {false, true}) EXPECT_EQ(2 * 1000000 + 100 * 1000 + 92, inlineRun(source, EX_CALL_RESULT_OK, optimize));
	return true;
}

TEST(ir_inline_match_bodies) {
	const char* source = R"(
		var total : i32 = 0;
		fn add(kind : i32, v : i32) : void {
			match kind {
				case 1:
					total += v;
				case 2, 3:
					total += v * 2;
					total += 1;
				case 10..=19:
					total += 1000;
				case:
					total += 100000;
			}
		}
		fn pick(kind : i32) : i32 {
			match kind {
				case 0:
					return 5;
				case 1:
					return 6;
				case:
					return 7;
			}
			return 0;
		}
		fn main() : i32 {
			for k in 0..4 { add(k, 10); }
			add(12, 0);
			add(50, 0);
			return total + 10000000 * pick(0) + 1000000 * pick(1) + 100000000 * pick(2);
		}
	)";
	// k=0 fallback 100000, k=1 10, k=2 21, k=3 21, 12 -> 1000, 50 -> 100000
	for (bool optimize : {false, true}) EXPECT_EQ(100000 + 10 + 21 + 21 + 1000 + 100000 + 50000000 + 6000000 + 700000000, inlineRun(source, EX_CALL_RESULT_OK, optimize));
	return true;
}

// Matches on unions and `any` narrow the subject inside the arms, those bodies stay real calls.
TEST(ir_inline_match_union_not_inlined) {
	const char* source = R"(
		fn describe(v : union { i32, f32 }) : i32 {
			match v {
				case i32:
					return 1;
				case f32:
					return 2;
			}
		}
		fn main() : i32 {
			var a : union { i32, f32 } = 3;
			var b : union { i32, f32 } = 2.5;
			return describe(a) * 10 + describe(b);
		}
	)";
	for (bool optimize : {false, true}) EXPECT_EQ(12, inlineRun(source, EX_CALL_RESULT_OK, optimize));
	return true;
}

// Aggregates, slice stores and inlined results are written straight into their destination. Reads of the
// destination inside the same expression must still see the old values.
TEST(ir_in_place_destinations) {
	const char* source = R"(
		struct Pair { a : i32; b : i32; c : i32; }
		fn make(flag : i32, base : i32) : Pair {
			if flag == 0 { return Pair {base, base + 1, base + 2}; }
			if flag == 1 { return Pair {base * 2, 0, 7}; }
			return Pair {1, 2, 3};
		}
		fn sum(p : Pair) : i32 { return p.a * 100 + p.b * 10 + p.c; }
		fn store(values : []i32, i : i32, p : Pair) : void {
			values[i] = p.a;
			values[i + 1] = p.b;
			values[i + 2] = p.c;
		}
		fn main() : i32 {
			var p : Pair = {1, 2, 3};
			p = Pair {p.c, p.a, p.b};
			const q = make(0, 4);
			const r = make(1, 5);
			var data : [6]i32 = [0, 0, 0, 0, 0, 0];
			store(data, 0, q);
			store(data, 3, r);
			return sum(p) * 1000000 + (data[0] + data[1] + data[2]) * 10000 + data[3] * 100 + data[5] + sum(make(5, 0));
		}
	)";
	// p = {3, 1, 2} -> 312; q = {4, 5, 6} -> sum 15; r = {10, 0, 7}; make(5,0) = {1,2,3} -> 123
	for (bool optimize : {false, true}) EXPECT_EQ(312 * 1000000 + 15 * 10000 + 10 * 100 + 7 + 123, inlineRun(source, EX_CALL_RESULT_OK, optimize));
	return true;
}

// An inlined parameter that holds a slice of a temporary array (an array literal argument) must keep that array
// alive for the whole inlined body: the body's own statements and the calls it makes must not overwrite it.
TEST(ir_inline_slice_of_temporary_array) {
	const char* source = R"(
		fn inner(pts : []const f32) : f32 {
			var big : [64]f32 = undefined;
			for i in 0..64 { big[i] = 100.0; }
			var s : f32 = 0.0;
			for i in 0..(pts.length as i32) { s += pts[i]; }
			return s;
		}
		fn wrap(pts : []const f32) : f32 { return inner(pts); }
		fn main() : i32 {
			return wrap([1.0, 2.0, 3.0]) as i32;
		}
	)";
	for (bool optimize : {false, true}) EXPECT_EQ(6, inlineRun(source, EX_CALL_RESULT_OK, optimize));
	return true;
}
