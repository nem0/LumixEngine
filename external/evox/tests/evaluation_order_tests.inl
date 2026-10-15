// Order of evaluation: operands, arguments and elements are evaluated left to right, each
// exactly once, and an assignment target is evaluated before its right-hand side.
//
// `tick(&c, n)` appends the digit n to the counter `c`, so the final value of `c` is the
// order in which the calls ran: 123 means "1, then 2, then 3".

TEST(EvaluationOrderIsLeftToRight) {
	const char* source = R"(
		struct Pair { a : i32; b : i32; }

		fn tick(c : *i32, v : i32) : i32 {
			c.* = c.* * 10 + v;
			return v;
		}
		fn two(a : i32, b : i32) : i32 { return a; }
		fn three(a : i32, b : i32, d : i32) : i32 { return a; }

		fn binary_arithmetic() : i64 {
			var c : i32 = 0;
			_ = tick(&c, 1) - tick(&c, 2);
			return c as i64;
		}
		fn binary_bitwise() : i64 {
			var c : i32 = 0;
			_ = tick(&c, 1) | tick(&c, 2);
			return c as i64;
		}
		fn binary_shift() : i64 {
			var c : i32 = 0;
			_ = tick(&c, 1) << tick(&c, 2);
			return c as i64;
		}
		fn comparison() : i64 {
			var c : i32 = 0;
			_ = tick(&c, 1) < tick(&c, 2);
			return c as i64;
		}
		fn nested_binary() : i64 {
			var c : i32 = 0;
			_ = (tick(&c, 1) + tick(&c, 2)) * (tick(&c, 3) + tick(&c, 4));
			return c as i64;
		}
		fn call_arguments() : i64 {
			var c : i32 = 0;
			_ = three(tick(&c, 1), tick(&c, 2), tick(&c, 3));
			return c as i64;
		}
		fn nested_call_arguments() : i64 {
			var c : i32 = 0;
			_ = two(tick(&c, 1), two(tick(&c, 2), tick(&c, 3)));
			return c as i64;
		}
		fn struct_literal() : i64 {
			var c : i32 = 0;
			var p : Pair = Pair { tick(&c, 1), tick(&c, 2) };
			return c as i64;
		}
		fn array_literal() : i64 {
			var c : i32 = 0;
			var a : [3]i32 = [tick(&c, 1), tick(&c, 2), tick(&c, 3)];
			return c as i64;
		}
		fn ternary_taken() : i64 {
			var c : i32 = 0;
			_ = tick(&c, 1) > 0 ? tick(&c, 2) : tick(&c, 3);
			return c as i64;
		}
		fn and_short_circuits() : i64 {
			var c : i32 = 0;
			_ = tick(&c, 1) > 5 and tick(&c, 2) > 0;
			return c as i64;
		}
		fn or_short_circuits() : i64 {
			var c : i32 = 0;
			_ = tick(&c, 1) > 0 or tick(&c, 2) > 0;
			return c as i64;
		}
		fn and_continues() : i64 {
			var c : i32 = 0;
			_ = tick(&c, 1) > 0 and tick(&c, 2) > 0;
			return c as i64;
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "binary_arithmetic", 12 },
		{ "binary_bitwise", 12 },
		{ "binary_shift", 12 },
		{ "comparison", 12 },
		{ "nested_binary", 1234 },
		{ "call_arguments", 123 },
		{ "nested_call_arguments", 123 },
		{ "struct_literal", 12 },
		{ "array_literal", 123 },
		{ "ternary_taken", 12 },
		{ "and_short_circuits", 1 },
		{ "or_short_circuits", 1 },
		{ "and_continues", 12 },
	});
}

TEST(AssignmentTargetIsEvaluatedBeforeRightHandSide) {
	const char* source = R"(
		fn tick(c : *i32, v : i32) : i32 {
			c.* = c.* * 10 + v;
			return v;
		}
		fn pick(c : *i32, v : i32, p : *i32) : *i32 {
			c.* = c.* * 10 + v;
			return p;
		}

		fn index_assign() : i64 {
			var c : i32 = 0;
			var a : [4]i32 = [0, 0, 0, 0];
			a[tick(&c, 1)] = tick(&c, 2);
			return c as i64;
		}
		fn index_assign_lands_in_target() : i64 {
			var c : i32 = 0;
			var a : [4]i32 = [0, 0, 0, 0];
			a[tick(&c, 1)] = 40 + tick(&c, 2);
			return a[1] as i64;
		}
		fn index_compound() : i64 {
			var c : i32 = 0;
			var a : [4]i32 = [0, 0, 0, 0];
			a[tick(&c, 1)] += tick(&c, 2);
			return c as i64;
		}
		fn pointer_assign() : i64 {
			var c : i32 = 0;
			var value : i32 = 0;
			pick(&c, 1, &value).* = tick(&c, 2);
			return c as i64 * 1000 + value as i64;
		}
		fn pointer_compound() : i64 {
			var c : i32 = 0;
			var value : i32 = 5;
			pick(&c, 1, &value).* *= tick(&c, 2);
			return c as i64 * 1000 + value as i64;
		}
		fn target_index_read_once() : i64 {
			var c : i32 = 0;
			var a : [4]i32 = [10, 20, 30, 40];
			a[tick(&c, 3)] -= tick(&c, 1);
			return c as i64 * 1000 + a[3] as i64;
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "index_assign", 12 },
		{ "index_assign_lands_in_target", 42 },
		{ "index_compound", 12 },
		{ "pointer_assign", 12000 + 2 },
		{ "pointer_compound", 12000 + 10 },
		{ "target_index_read_once", 31000 + 39 },
	});
}

TEST(EvaluationOrderOfBasesCalleesAndSubscripts) {
	const char* source = R"(
		struct Pair { a : i32; b : i32; }

		fn tick(c : *i32, v : i32) : i32 {
			c.* = c.* * 10 + v;
			return v;
		}
		fn add_one(x : i32) : i32 { return x + 1; }
		fn use_two(a : i32, b : i32) : i32 { return a + b; }
		fn sum(p : Pair, x : i32) : i32 { return p.a + x; }

		fn arr_at(c : *i32, v : i32) : [3]i32 {
			c.* = c.* * 10 + v;
			return [10, 20, 30];
		}
		fn slice_at(c : *i32, v : i32, s : []i32) : []i32 {
			c.* = c.* * 10 + v;
			return s;
		}
		fn pair_at(c : *i32, v : i32) : Pair {
			c.* = c.* * 10 + v;
			return Pair { 1, 2 };
		}
		fn ptr_at(c : *i32, v : i32, p : *i32) : *i32 {
			c.* = c.* * 10 + v;
			return p;
		}
		fn callee_at(c : *i32, v : i32) : fn(i32) : i32 {
			c.* = c.* * 10 + v;
			return add_one;
		}
		fn callee2_at(c : *i32, v : i32) : fn(i32, i32) : i32 {
			c.* = c.* * 10 + v;
			return use_two;
		}

		fn array_base_then_index() : i64 {
			var c : i32 = 0;
			_ = arr_at(&c, 1)[tick(&c, 2)];
			return c as i64;
		}
		fn slice_base_then_index() : i64 {
			var c : i32 = 0;
			var storage : [3]i32 = [1, 2, 3];
			_ = slice_at(&c, 1, storage[:])[tick(&c, 2)];
			return c as i64;
		}
		fn subscript_of_subscript() : i64 {
			var c : i32 = 0;
			var grid : [2][2]i32 = [[1, 2], [3, 4]];
			_ = grid[tick(&c, 1)][tick(&c, 0)];
			return c as i64;                       // digits 1 then 0
		}
		fn slice_expression_bounds() : i64 {
			var c : i32 = 0;
			var a : [4]i32 = [1, 2, 3, 4];
			var s : []i32 = a[tick(&c, 1):tick(&c, 2)];
			return c as i64;
		}
		fn member_of_call_then_operand() : i64 {
			var c : i32 = 0;
			_ = pair_at(&c, 1).a + tick(&c, 2);
			return c as i64;
		}
		fn dereference_of_call_then_operand() : i64 {
			var c : i32 = 0;
			var x : i32 = 7;
			_ = ptr_at(&c, 1, &x).* + tick(&c, 2);
			return c as i64;
		}
		fn callee_then_argument() : i64 {
			var c : i32 = 0;
			_ = callee_at(&c, 1)(tick(&c, 2));
			return c as i64;
		}
		fn callee_then_arguments() : i64 {
			var c : i32 = 0;
			_ = callee2_at(&c, 1)(tick(&c, 2), tick(&c, 3));
			return c as i64;
		}
		fn receiver_then_arguments() : i64 {
			var c : i32 = 0;
			_ = pair_at(&c, 1).sum(tick(&c, 2));
			return c as i64;
		}
		fn member_in_argument_list() : i64 {
			var c : i32 = 0;
			_ = use_two(pair_at(&c, 1).a, tick(&c, 2));
			return c as i64;
		}
	)";
	return runBitwiseCases(__func__, source, {
		{ "array_base_then_index", 12 },
		{ "slice_base_then_index", 12 },
		{ "subscript_of_subscript", 10 },
		{ "slice_expression_bounds", 12 },
		{ "member_of_call_then_operand", 12 },
		{ "dereference_of_call_then_operand", 12 },
		{ "callee_then_argument", 12 },
		{ "callee_then_arguments", 123 },
		{ "receiver_then_arguments", 12 },
		{ "member_in_argument_list", 12 },
	});
}
