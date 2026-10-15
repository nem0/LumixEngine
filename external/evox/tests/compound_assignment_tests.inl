// Compound assignment must evaluate its left-hand side exactly once:
// `a[f()] += 1` calls f once. These tests count the calls through a side effect.
// They fail until the IR builder materializes the target address once instead
// of emitting the address expression for both the load and the store.
// (runBitwiseCases is a generic "call zero-argument i64 functions" runner.)

TEST(CompoundAssignmentEvaluatesIndexOnce) {
	const char* source = R"(
		fn bump(counter : *i32) : i32 {
			counter.* += 1;
			return 0;
		}

		fn add_assign() : i64 {
			var arr : [2]i32 = [10, 20];
			var calls : i32 = 0;
			arr[bump(&calls)] += 5;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, arr[0] == 15
		}
		fn sub_assign() : i64 {
			var arr : [2]i32 = [10, 20];
			var calls : i32 = 0;
			arr[bump(&calls)] -= 4;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 6
		}
		fn mul_assign() : i64 {
			var arr : [2]i32 = [10, 20];
			var calls : i32 = 0;
			arr[bump(&calls)] *= 3;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 30
		}
		fn div_assign() : i64 {
			var arr : [2]i32 = [10, 20];
			var calls : i32 = 0;
			arr[bump(&calls)] /= 2;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 5
		}
		fn and_assign() : i64 {
			var arr : [2]u32 = [0xFF, 0];
			var calls : i32 = 0;
			arr[bump(&calls)] &= 0x0F;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 15
		}
		fn or_assign() : i64 {
			var arr : [2]u32 = [1, 0];
			var calls : i32 = 0;
			arr[bump(&calls)] |= 0x10;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 17
		}
		fn xor_assign() : i64 {
			var arr : [2]u32 = [0xFF, 0];
			var calls : i32 = 0;
			arr[bump(&calls)] ^= 0x0F;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 240
		}
		fn shl_assign() : i64 {
			var arr : [2]u32 = [1, 0];
			var calls : i32 = 0;
			arr[bump(&calls)] <<= 4;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 16
		}
		fn shr_assign() : i64 {
			var arr : [2]u32 = [256, 0];
			var calls : i32 = 0;
			arr[bump(&calls)] >>= 4;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 16
		}
		fn plain_assign() : i64 {
			var arr : [2]i32 = [10, 20];
			var calls : i32 = 0;
			arr[bump(&calls)] = 7;
			return calls as i64 * 1000 + arr[0] as i64;       // 1 call, 7 (already correct)
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "add_assign", 1015 },
		{ "sub_assign", 1006 },
		{ "mul_assign", 1030 },
		{ "div_assign", 1005 },
		{ "and_assign", 1015 },
		{ "or_assign", 1017 },
		{ "xor_assign", 1240 },
		{ "shl_assign", 1016 },
		{ "shr_assign", 1016 },
		{ "plain_assign", 1007 },
	});
}

TEST(CompoundAssignmentTargetIsFixedBeforeRightHandSide) {
	// the right-hand side changes `i`; the read and the write must still use the same element
	const char* source = R"(
		fn bump(i : *i32) : i32 {
			i.* += 1;
			return 100;
		}

		fn index_changed_by_rhs() : i64 {
			var arr : [3]i32 = [1, 2, 3];
			var i : i32 = 0;
			arr[i] += bump(&i);
			return arr[0] as i64 * 1000 + arr[1] as i64;      // arr[0] == 101, arr[1] untouched
		}
		fn index_changed_by_rhs_bitwise() : i64 {
			var arr : [3]u32 = [1, 2, 3];
			var i : i32 = 0;
			arr[i] |= bump(&i) as u32;
			return arr[0] as i64 * 1000 + arr[1] as i64;      // arr[0] == 101, arr[1] untouched
		}
		fn pointer_changed_by_rhs() : i64 {
			var values : [2]i32 = [10, 20];
			var p = &values[0];
			p.* += swing(&p, &values[1]);
			return values[0] as i64 * 1000 + values[1] as i64; // values[0] += 5 through the old p
		}
		fn swing(p : **i32, next : *i32) : i32 {
			p.* = next;
			return 5;
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "index_changed_by_rhs", 101002 },
		{ "index_changed_by_rhs_bitwise", 101002 },
		{ "pointer_changed_by_rhs", 15020 },
	});
}

TEST(CompoundAssignmentEvaluatesPointerExpressionOnce) {
	const char* source = R"(
		fn target(counter : *i32, p : *i32) : *i32 {
			counter.* += 1;
			return p;
		}

		fn target_u32(counter : *i32, p : *u32) : *u32 {
			counter.* += 1;
			return p;
		}

		fn through_call() : i64 {
			var value : i32 = 10;
			var calls : i32 = 0;
			target(&calls, &value).* += 5;
			return calls as i64 * 1000 + value as i64;        // 1 call, 15
		}
		fn through_call_bitwise() : i64 {
			var value : u32 = 1;
			var calls : i32 = 0;
			target_u32(&calls, &value).* <<= 3;
			return calls as i64 * 1000 + value as i64;        // 1 call, 8
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "through_call", 1015 },
		{ "through_call_bitwise", 1008 },
	});
}

TEST(CompoundAssignmentEvaluatesNestedIndicesOnce) {
	const char* source = R"(
		fn tick(counter : *i32, value : i32) : i32 {
			counter.* += 1;
			return value;
		}

		fn nested() : i64 {
			var grid : [2][2]i32 = [[1, 2], [3, 4]];
			var calls : i32 = 0;
			grid[tick(&calls, 1)][tick(&calls, 0)] += 10;
			return calls as i64 * 1000 + grid[1][0] as i64;   // 2 calls, 13
		}
		fn field_of_indexed() : i64 {
			var items : [2]Pair = [Pair { 1, 2 }, Pair { 3, 4 }];
			var calls : i32 = 0;
			items[tick(&calls, 1)].b *= 5;
			return calls as i64 * 1000 + items[1].b as i64;   // 1 call, 20
		}
		struct Pair { a : i32; b : i32; }
	)";
	return runBitwiseCases(__func__, source, {
		{ "nested", 2013 },
		{ "field_of_indexed", 1020 },
	});
}

TEST(CompoundAssignmentWithOperatorOverloadEvaluatesTargetOnce) {
	const char* source = R"(
		struct Vec2 { x : i32; y : i32; }

		operator +(a : Vec2, b : Vec2) : Vec2 { return Vec2 { a.x + b.x, a.y + b.y }; }

		fn bump(counter : *i32) : i32 {
			counter.* += 1;
			return 0;
		}

		fn overloaded_add() : i64 {
			var values : [2]Vec2 = [Vec2 { 1, 2 }, Vec2 { 10, 20 }];
			var calls : i32 = 0;
			values[bump(&calls)] += Vec2 { 5, 5 };
			return calls as i64 * 1000 + values[0].x as i64;  // 1 call, 6
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "overloaded_add", 1006 },
	});
}

TEST(CompoundAssignmentEvaluatesSliceIndexOnce) {
	const char* source = R"(
		fn bump(counter : *i32) : i32 {
			counter.* += 1;
			return 1;
		}

		fn slice_element() : i64 {
			var storage : [3]i32 = [1, 2, 3];
			var view : []i32 = storage[:];
			var calls : i32 = 0;
			view[bump(&calls)] += 40;
			return calls as i64 * 1000 + storage[1] as i64;   // 1 call, 42
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "slice_element", 1042 },
	});
}
