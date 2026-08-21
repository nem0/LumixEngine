TEST(BreakContinueTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			while i < 10 {
				i += 1;
				if i == 3 {
					continue;
				}
				if i == 8 {
					break;
				}
			}
			return i;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ForLoopCompiles) {
	const char* source = R"(
		fn main() : void {
			for i in 0..9 {
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ForLoopOldRangeSyntaxFails) {
	const char* source = R"(
		fn main() : void {
			for i = 0..9 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// The i32 passed to bound checking is a hint for untyped literals, not a
// requirement: matching concrete integer bounds of any width are valid.
TEST(ForLoopI64Bounds) {
	const char* source = R"(
		fn main() : void {
			var from : i64 = 0;
			var to : i64 = 9;
			for i in from..to {
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ForLoopRangeBoundsMustMatchTypeFails) {
	const char* source = R"(
		fn main() : void {
			var from : i32 = 0;
			var to : f32 = 1.5;
			for i in from..to {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ForLoopRangeRequiresNumericBoundsFails) {
	const char* source = R"(
		fn main() : void {
			for i in 0.."9" {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ForLoopVariableIsImmutable) {
	const char* source = R"(
		fn main() : void {
			for i in 0..9 {
				i = 1;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BreakContinueOutsideLoopFail) {
	const char* source = R"(
		fn main() : void {
			break;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(WhileConditionMustBeBoolFails) {
	const char* source = R"(
		fn main() : void {
			while 1 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NamedLabelTypecheck) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			outer: while i < 10 {
				i += 1;
				var j : i32 = 0;
				while j < 10 {
					j += 1;
					if j == 2 {
						continue outer;
					}
				}
			}
			return i;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(NamedLabelUnknownFails) {
	const char* source = R"(
		fn main() : void {
			while true {
				break missing;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DuplicateNamedLabelFails) {
	const char* source = R"(
		fn main() : void {
			outer: while true {
				outer: while true {
					break;
				}
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(SequentialNamedLabelReuseCompiles) {
	const char* source = R"(
		fn main() : void {
			outer: while true {
				break;
			}
			outer: while true {
				break;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(DuplicateLoopLabelFails) {
	const char* source = R"(
		fn main() : void {
			outer: while true {
				break;
			}
			outer: for i in 0..1 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BreakContinueRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			var sum : i32 = 0;
			while i < 10 {
				i += 1;
				if i == 3 {
					continue;
				}
				if i == 8 {
					break;
				}
				sum += i;
			}
			return sum;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(25, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ForLoopRuntime) {
	const char* source = R"(
		var bound_hits : i32 = 0;

		fn lower() : i32 {
			bound_hits += 1;
			return 0;
		}

		fn upper() : i32 {
			bound_hits += 1;
			return 3;
		}

		fn main() : i32 {
			var sum : i32 = 0;
			for i in lower()..upper() {
				sum += i;
			}
			return bound_hits * 100 + sum;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(203, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ForLoopRangeEvaluatedOnce) {
	const char* source = R"(
		var range_hits : i32 = 0;

		fn lower() : i32 {
			range_hits += 1;
			return 0;
		}

		fn upper() : i32 {
			range_hits += 1;
			return 2;
		}

		fn main() : i32 {
			var sum : i32 = 0;
			for i in lower()..upper() {
				sum += i;
			}
			return range_hits * 100 + sum;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(201, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(NamedLabelBreakContinueRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			var hits : i32 = 0;
			outer: while i < 5 {
				i += 1;
				var j : i32 = 0;
				while j < 5 {
					j += 1;
					if i < 5 and j == 2 {
						continue outer;
					}
					if i == 5 and j == 4 {
						break outer;
					}
					hits += 1;
				}
			}
			return i + hits;
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

TEST(ForLoopUntypedBoundAdoptsEndTypeRuntime) {
	const char* source = R"(
		fn count(values : []i32) : i32 {
			var total : i32 = 0;
			for i in 0..values.length {
				total += values[i];
			}
			return total;
		}

		fn main() : i32 {
			var values : [3]i32 = undefined;
			values[0] = 1;
			values[1] = 2;
			values[2] = 39;
			return count(values);
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

TEST(ForLoopMismatchedConcreteBoundsFail) {
	const char* source = R"(
		fn main() : i32 {
			const from : i32 = 0;
			const to : i64 = 10;
			var total : i32 = 0;
			for i in from..to {
				total += 1;
			}
			return total;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ForInValueOverArrayCompiles) {
	const char* source = R"(
		fn main() : void {
			var arr : [3]i32 = undefined;
			for v in arr {
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ForInIndexValueOverArrayCompiles) {
	const char* source = R"(
		fn main() : void {
			var arr : [3]i32 = undefined;
			for i, v in arr {
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ForInValueOverSliceRuntime) {
	const char* source = R"(
		fn sum(values : []i32) : i32 {
			var total : i32 = 0;
			for v in values {
				total += v;
			}
			return total;
		}

		fn main() : i32 {
			var values : [3]i32 = undefined;
			values[0] = 1;
			values[1] = 2;
			values[2] = 39;
			return sum(values);
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

TEST(ForInIndexValueOverSliceRuntime) {
	const char* source = R"(
		fn sum(values : []i32) : i32 {
			var total : i32 = 0;
			for i, v in values {
				total += i as i32 * 100 + v;
			}
			return total;
		}

		fn main() : i32 {
			var values : [3]i32 = undefined;
			values[0] = 1;
			values[1] = 2;
			values[2] = 3;
			return sum(values);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(306, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ForInValueOverSubSliceRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var arr : [5]i32 = undefined;
			arr[0] = 100;
			arr[1] = 1;
			arr[2] = 2;
			arr[3] = 3;
			arr[4] = 200;

			var middle : []i32 = arr[1:4];
			var total : i32 = 0;
			for v in middle {
				total += v;
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(6, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ForInIndexValueOverSubSliceRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var arr : [5]i32 = undefined;
			arr[0] = 100;
			arr[1] = 10;
			arr[2] = 20;
			arr[3] = 30;
			arr[4] = 200;

			var middle : []i32 = arr[1:4];
			var total : i32 = 0;
			for i, v in middle {
				total += i as i32 * 1000 + v;
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	// slice-relative indices 0,1,2 (not the backing array's 1,2,3): 10+1020+2030 = 3060
	EXPECT_EQ(3060, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ForInIndexTypeIsIsize) {
	const char* source = R"(
		fn main() : void {
			var arr : [3]i32 = undefined;
			for i, v in arr {
				const check : isize = i;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ForInValueVariableIsImmutable) {
	const char* source = R"(
		fn main() : void {
			var arr : [3]i32 = undefined;
			for v in arr {
				v = 1;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ForInIndexVariableIsImmutable) {
	const char* source = R"(
		fn main() : void {
			var arr : [3]i32 = undefined;
			for i, v in arr {
				i = 1;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ForInEmptySliceDoesNotExecuteRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var hits : i32 = 0;
			var arr : [3]i32 = undefined;
			var empty : []i32 = arr[0:0];
			for v in empty {
				hits += 1;
			}
			return hits;
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

TEST(ForRangeZeroIterationsDoesNotExecuteBody) {
	const char* source = R"(
		fn main(end : i32) : i32 {
			var hits : i32 = 0;
			for i in end + 5..end {
				hits += 1;
			}
			for i in 0..end {
				hits += 10;
			}
			return hits;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	ls_push_i32(runtime, 0);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(NestedForInRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var outer : [2]i32 = undefined;
			outer[0] = 10;
			outer[1] = 20;

			var inner : [3]i32 = undefined;
			inner[0] = 1;
			inner[1] = 2;
			inner[2] = 3;

			var total : i32 = 0;
			for oi, ov in outer {
				for iv in inner {
					total += ov + iv + oi as i32;
				}
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	// outer index 0, value 10: (10+1+0)+(10+2+0)+(10+3+0) = 36
	// outer index 1, value 20: (20+1+1)+(20+2+1)+(20+3+1) = 69
	EXPECT_EQ(105, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(NestedForInNoIndexRuntime) {
	const char* source = R"(
		fn main() : i32 {
			var outer : [2]i32 = undefined;
			outer[0] = 10;
			outer[1] = 20;

			var inner : [3]i32 = undefined;
			inner[0] = 1;
			inner[1] = 2;
			inner[2] = 3;

			var total : i32 = 0;
			for ov in outer {
				for iv in inner {
					total += ov + iv;
				}
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	// outer value 10: (10+1)+(10+2)+(10+3) = 36
	// outer value 20: (20+1)+(20+2)+(20+3) = 66
	EXPECT_EQ(102, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
