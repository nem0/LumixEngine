// These tests describe the proposed non-owning `any` type. They are expected to
// fail until any support is implemented.

TEST(AnyPrimitiveMatch) {
	EXPECT_COMPILE(R"(
		fn inspect(value : any) : i32 {
			match value {
				case i32:
					return value;
				case:
					return -1;
			}
		}
	)");
	return true;
}

TEST(AnyStructMatch) {
	EXPECT_COMPILE(R"(
		struct Vec2 { x : f32; y : f32; }
		fn inspect(value : any) : f32 {
			match value {
				case Vec2:
					return value.x + value.y;
				case:
					return -1;
			}
		}
	)");
	return true;
}

TEST(AnyArrayMatch) {
	EXPECT_COMPILE(R"(
		fn inspect(value : any) : i32 {
			match value {
				case [3]i32:
					return value[0] + value[1] + value[2];
				case:
					return -1;
			}
		}
	)");
	return true;
}

TEST(AnySliceMatch) {
	EXPECT_COMPILE(R"(
		fn inspect(value : any) : i32 {
			match value {
				case []const u8:
					return value.length as i32;
				case:
					return -1;
			}
		}
	)");
	return true;
}

TEST(AnyNullableMatch) {
	EXPECT_COMPILE(R"(
		fn inspect(value : any) : i32 {
			match value {
				case ?i32:
					if const unwrapped = value { return unwrapped; }
					return -1;
				case:
					return -2;
			}
		}
	)");
	return true;
}

TEST(AnyFunctionMatch) {
	EXPECT_COMPILE(R"(
		fn add(value : i32) : i32 { return value + 1; }
		fn inspect(value : any) : i32 {
			match value {
				case fn(i32) : i32:
					return value(4);
				case:
					return -1;
			}
		}
	)");
	return true;
}

TEST(AnyLiteralMaterializesTemporary) {
	EXPECT_COMPILE(R"(
		fn inspect(value : any) : i32 {
			match value {
				case i32: return value;
				case: return -1;
			}
		}
		fn main() : i32 {
			return inspect(42);
		}
	)");
	return true;
}

TEST(AnyStructRvalueMaterializesTemporary) {
	EXPECT_COMPILE(R"(
		struct Pair { a : i32; b : i32; }
		fn inspect(value : any) : i32 {
			match value {
				case Pair: return value.a + value.b;
				case: return -1;
			}
		}
		fn main() : i32 { return inspect(Pair { 10, 32 }); }
	)");
	return true;
}

TEST(AnyReferencesOriginalLvalue) {
	EXPECT_COMPILE(R"(
		fn inspect(value : any) : i32 {
			match value { case i32: return value; case: return -1; }
		}
		fn main() : i32 {
			var value : i32 = 7;
			var erased : any = value;
			value = 9;
			return inspect(erased);
		}
	)");
	return true;
}

TEST(AnyMultipleCasesAndFallback) {
	EXPECT_COMPILE(R"(
		struct S { value : i32; }
		fn inspect(value : any) : i32 {
			match value {
				case i32, u32: return 1;
				case S: return 2;
				case: return 3;
			}
		}
	)");
	return true;
}

TEST(AnyCasePromotesOnlyInsideArm) {
	EXPECT_COMPILE(R"(
		fn inspect(value : any) : i32 {
			match value {
				case i32: return value + 1;
				case: return 0;
			}
		}
	)");
	return true;
}

TEST(AnyMatchRequiresFallback) {
	EXPECT_COMPILE_FAIL(R"(
		fn inspect(value : any) : i32 {
			match value { case i32: return value; }
		}
	)");
	return true;
}

TEST(AnyRejectsDirectMemberAccess) {
	EXPECT_COMPILE_FAIL(R"(
		struct S { value : i32; }
		fn inspect(value : any) : i32 { return value.value; }
	)");
	return true;
}

TEST(AnyRejectsDirectArithmetic) {
	EXPECT_COMPILE_FAIL(R"(
		fn inspect(value : any) : i32 { return value + 1; }
	)");
	return true;
}

TEST(AnyRejectsIsAndAs) {
	EXPECT_COMPILE_FAIL(R"(
		fn inspect(value : any) : i32 {
			if value is i32 { return value as i32; }
			return 0;
		}
	)");
	return true;
}

TEST(AnyRejectsComptimeOnlyValue) {
	EXPECT_COMPILE_FAIL(R"(
		comptime T = i32;
		fn inspect() : any { return T; }
	)");
	return true;
}

TEST(AnyRejectsDanglingLocalEscape) {
	EXPECT_COMPILE_FAIL(R"(
		fn invalid() : any {
			var value : i32 = 42;
			return value;
		}
	)");
	return true;
}

TEST(AnyRejectsDanglingTemporaryGlobal) {
	EXPECT_COMPILE_FAIL(R"(
		var global : any = 42;
	)");
	return true;
}

TEST(AnyExactTypeMatch) {
	EXPECT_COMPILE(R"(
		fn inspect(value : any) : i32 {
			match value {
				case i32: return 1;
				case i64: return 2;
				case: return 3;
			}
		}
	)");
	return true;
}

TEST(AnyRuntimePrimitiveMatch) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		fn main() : i32 {
			var value : any = 42;
			match value {
				case i32: return value + 1;
				case: return -1;
			}
		}
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(43, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(AnyRuntimeFallbackMatch) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		fn main() : i32 {
			var value : any = true;
			match value {
				case i32: return 1;
				case: return 2;
			}
		}
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(2, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(AnyRuntimeStructMatch) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		struct Pair { first : i32; second : i32; }
		fn main() : i32 {
			var value : any = Pair { 10, 32 };
			match value {
				case Pair: return value.first + value.second;
				case: return -1;
			}
		}
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(42, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(AnyRuntimeDistinguishesStructTypes) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		struct First { value : i32; }
		struct Second { value : i32; }
		fn classify(value : any) : i32 {
			match value {
				case First: return 1;
				case Second: return 2;
				case: return 3;
			}
		}
		fn main() : i32 {
			return classify(First { 10 }) * 10 + classify(Second { 20 });
		}
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(12, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(AnyRuntimeRvalueArgumentMatch) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		fn inspect(value : any) : i32 {
			match value {
				case i32: return value;
				case: return -1;
			}
		}
		fn main() : i32 { return inspect(123); }
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(123, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(AnyRuntimeOriginalLvalueMatch) {
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(R"(
		fn main() : i32 {
			var original : i32 = 7;
			var value : any = original;
			original = 9;
			match value {
				case i32: return value;
				case: return -1;
			}
		}
	)"), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_EQ(EX_RESULT_OK, ex_call(runtime, toLs("main")));
	EXPECT_EQ(9, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
