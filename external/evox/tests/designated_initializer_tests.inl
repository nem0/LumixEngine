TEST(DesignatedInitializerExplicitTypeUsesFieldNames) {
	const char* source = R"(
		struct Record {
			first : i32;
			second : i32;
			enabled : bool;
		}

		fn main() : i32 {
			const value = Record {
				.enabled = true,
				.second = 32,
				.first = 10
			};
			if not value.enabled { return 0; }
			return value.first + value.second;
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

TEST(DesignatedInitializerUsesExpectedTypeInAllValueContexts) {
	const char* source = R"(
		struct Pair {
			left : i32;
			right : i32;
		}

		fn make_pair() : Pair {
			return { .right = 30, .left = 12 };
		}

		fn sum(value : Pair) : i32 {
			return value.left + value.right;
		}

		fn main() : i32 {
			var local : Pair = { .right = 2, .left = 1 };
			local = { .left = 10, .right = 20 };
			return sum({ .right = 2, .left = 1 })
				+ sum(local)
				+ sum(make_pair());
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(75, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DesignatedInitializerSupportsNestedContextualLiterals) {
	const char* source = R"(
		struct Point {
			x : i32;
			y : i32;
		}

		struct Segment {
			start : Point;
			end : Point;
			weight : i32;
		}

		fn main() : i32 {
			const segment = Segment {
				.weight = 2,
				.end = { .y = 20, .x = 10 },
				.start = Point { .x = 3, .y = 7 }
			};
			return segment.start.x + segment.start.y
				+ segment.end.x + segment.end.y + segment.weight;
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

TEST(DesignatedInitializerFieldsUseTheirDeclaredTypes) {
	EXPECT_COMPILE(R"(
		struct Values {
			optional : ?i32;
			items : []i32;
			small : u8;
			decimal : f64;
		}

		fn main() : void {
			var items : [2]i32 = [1, 2];
			const values = Values {
				.decimal = 2.5,
				.small = 255,
				.items = items,
				.optional = 1
			};
		}
	)");
	return true;
}

TEST(DesignatedInitializerWorksForGlobalsAndComptimeValues) {
	const char* source = R"(
		struct Pair {
			x : i32;
			y : i32;
		}

		var global : Pair = { .y = 40, .x = 2 };
		comptime constant = Pair { .y = 20, .x = 22 };

		fn main() : i32 {
			if constant.x + constant.y != 42 { return 0; }
			return global.x + global.y;
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

TEST(DesignatedInitializerExpressionsEvaluateInSourceOrder) {
	const char* source = R"(
		struct Triple {
			a : i32;
			b : i32;
			c : i32;
		}

		var order : i32 = 0;
		fn first() : i32 { order = order * 10 + 1; return 1; }
		fn second() : i32 { order = order * 10 + 2; return 2; }
		fn third() : i32 { order = order * 10 + 3; return 3; }

		fn main() : i32 {
			const value = Triple {
				.c = first(),
				.a = second(),
				.b = third()
			};
			return order * 1000 + value.a * 100 + value.b * 10 + value.c;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_CALL_RESULT_OK, test_call(runtime, toLs("main")));
	EXPECT_EQ(123231, ex_task_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(DesignatedInitializerMissingFieldFails) {
	EXPECT_COMPILE_FAIL(R"(
		struct Pair { x : i32; y : i32; }
		fn main() : void {
			const value = Pair { .x = 1 };
		}
	)");
	return true;
}

TEST(DesignatedInitializerDuplicateFieldFails) {
	EXPECT_COMPILE_FAIL(R"(
		struct Pair { x : i32; y : i32; }
		fn main() : void {
			const value = Pair { .x = 1, .y = 2, .x = 3 };
		}
	)");
	return true;
}

TEST(DesignatedInitializerUnknownFieldFails) {
	EXPECT_COMPILE_FAIL(R"(
		struct Pair { x : i32; y : i32; }
		fn main() : void {
			const value = Pair { .x = 1, .y = 2, .z = 3 };
		}
	)");
	return true;
}

TEST(DesignatedInitializerCannotMixWithPositionalEntries) {
	EXPECT_COMPILE_FAIL(R"(
		struct Pair { x : i32; y : i32; }
		fn main() : void {
			const value = Pair { 1, .y = 2 };
		}
	)");
	EXPECT_COMPILE_FAIL(R"(
		struct Pair { x : i32; y : i32; }
		fn main() : void {
			const value = Pair { .x = 1, 2 };
		}
	)");
	return true;
}

TEST(DesignatedInitializerValueTypeMismatchFails) {
	EXPECT_COMPILE_FAIL(R"(
		struct Values { number : i32; enabled : bool; }
		fn main() : void {
			const value = Values { .enabled = 1, .number = true };
		}
	)");
	return true;
}

TEST(ContextualDesignatedInitializerRequiresExpectedStructType) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value = { .x = 1 };
		}
	)");
	return true;
}

TEST(DesignatedInitializerRejectsNonStructTargets) {
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : [2]i32 = { .first = 1, .second = 2 };
		}
	)");
	EXPECT_COMPILE_FAIL(R"(
		fn main() : void {
			const value : tuple { i32, i32 } = { .first = 1, .second = 2 };
		}
	)");
	return true;
}
