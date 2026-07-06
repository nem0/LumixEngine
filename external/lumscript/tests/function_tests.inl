TEST(ParameterAssignmentFails) {
	const char* source = R"(
		fn foo(a : i32) : void {
			a = 2;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FunctionCallAssignmentFails) {
	const char* source = R"(
		fn foo() : i32 {
			return 0;
		}

		fn main() : void {
			foo() = 42;
		}
	)";
	EXPECT_COMPILE_FAIL(source);

	const char* indexed_source = R"(
		fn foo() : [4]i32 {
			var d : [4]i32 = undefined;
			return d;
		}

		fn main() : void {
			foo()[1] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(indexed_source);
	return true;
}

TEST(FunctionCallWrongArityFails) {
	const char* source = R"(
		fn foo(a : i32) : i32 {
			return a;
		}

		fn main() : i32 {
			return foo();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FirstClassFunctionLiteralInfersTypedVariable) {
	const char* source = R"(
		const foo = fn() : i32 {
			return 1;
		};

		fn main() : i32 {
			const bar : fn() : i32 = foo;
			return bar();
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(GlobalFunctionLiteralCanRecursivelyCallItself) {
	const char* source = R"(
		const sum_to = fn(n : i32) : i32 {
			if n == 0 {
				return 0;
			}
			return n + sum_to(n - 1);
		};

		fn main() : i32 {
			return sum_to(6);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(21, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FirstClassFunctionLiteralCanBePassedAsArgument) {
	const char* source = R"(
		fn call(f : fn() : i32) : i32 {
			return f();
		}

		const foo = fn() : i32 {
			return 1;
		};

		fn main() : i32 {
			return call(foo);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FirstClassFunctionLiteralArityMismatchFails) {
	const char* source = R"(
		const foo = fn() : i32 {
			return 1;
		};

		fn main() : void {
			const bar : fn(i32) : i32 = foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FirstClassFunctionLiteralCanNotBeReassigned) {
	const char* source = R"(
		fn main() : void {
			const foo = fn() : i32 {
				return 1;
			};
			foo = fn() : i32 {
				return 2;
			};
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FirstClassFunctionLiteralSignatureMismatchFails) {
	const char* source = R"(
		fn main() : void {
			const foo = fn() : i32 {
				return 1;
			};
			const bar : fn() : void = foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NullableFirstClassFunctionGlobalCannotBeCalledDirectly) {
	const char* source = R"(
		var maybe_add : ?fn(i32) : i32 = fn(v : i32) : i32 {
			return v + 1;
		};

		fn main() : i32 {
			return maybe_add(41);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FirstClassFunctionLiteralMemberAccessFails) {
	const char* source = R"(
		const foo = fn() : i32 {
			return 1;
		};

		fn main() : void {
			const value = foo.bar;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(MemberAccessOnFailedCallBaseDoesNotCrash) {
	const char* source = R"(
		fn main() : void {
			foo().bar;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FirstClassFunctionSignatureMismatchFails) {
	const char* source = R"(
		fn to_float(a : i32) : f32 {
			return a as f32;
		}

		fn main() : void {
			const f : fn(i32) : i32 = to_float;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FunctionParameterTypeResolutionFailureFails) {
	const char* source = R"(
		fn main() : void {
		}

		fn bad(a : missing_type, b : i32) : i32 {
			return b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(NestedFunctionDeclarationsFail) {
	const char* source = R"(
		fn apply(f : fn(i32, i32) : i32, a : i32, b : i32) : i32 {
			return f(a, b);
		}

		fn main() : i32 {
			fn add(a : i32, b : i32) : i32 {
				return a + b;
			}

			fn mul(a : i32, b : i32) : i32 {
				return a * b;
			}

			const f = add;
			return apply(f, 20, 2) + mul(5, 4);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BytecodeFunctionValueLocal) {
	const char* source = R"(
		fn add_one(v : i32) : i32 {
			return v + 1;
		}

		fn main() : i32 {
			const f : fn(i32) : i32 = add_one;
			return f(41);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

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

TEST(BytecodeIndirectFunctionCall) {
	const char* source = R"(
		fn add_one(v : i32) : i32 {
			return v + 1;
		}

		fn apply(f : fn(i32) : i32, value : i32) : i32 {
			return f(value);
		}

		fn main() : i32 {
			return apply(add_one, 41);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

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

TEST(BytecodeIndirectFunctionCallWithAggregateArgument) {
	const char* source = R"(
		struct Pair {
			first : i32;
			second : i32;
		}

		fn sum(value : Pair) : i32 {
			return value.first + value.second;
		}

		fn main() : i32 {
			const f : fn(Pair) : i32 = sum;
			return f(Pair { 20, 22 });
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

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



TEST(BytecodeGlobalFunctionLiteral) {
   const char* source = R"(
           const foo = fn() : i32 {
                   return 1;
           };

           fn main() : i32 {
                   return foo();
           }
   )";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

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

TEST(BytecodeGlobalFunctionVariable) {
   const char* source = R"(
           var foo = fn() : i32 {
                   return 1;
           };

           fn main() : i32 {
                   return foo();
           }
   )";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

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

TEST(testNativeFunctionCall) {
	const char* source = R"(
		extern fn native_add(a : i32, b : i32) : i32;

		fn main() : i32 {
			return native_add(20, 22);
		}
	)";

	CAPI_BEGIN(module, diagnostics);

	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	const i32 native_add = ls_module_get_native_function_index(module, toLs("native_add"));
	EXPECT_TRUE(ls_runtime_set_native_function_callback(runtime, native_add, &nativeAddC) == LS_RESULT_OK);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Indirect call whose return (8 bytes) is wider than the consumed 4-byte
// function-value slot it is delivered into, nested inside an expression with
// live temps below the arg window. Exercises the overlapping result
// relocation in the indirect-call runtime path.
TEST(IndirectCallWideReturnInNestedExpression) {
	const char* source = R"(
		fn big(a : i64, b : i64) : i64 {
			return a * 1000 + b;
		}

		fn apply(f : fn(i64, i64) : i64, a : i64, b : i64) : i64 {
			return 1 + f(a, b);
		}

		fn main() : i64 {
			return apply(big, 4, 2);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(4003 == ls_to_i64(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FunctionNamedSinCompilesAndRuns) {
	const char* source = R"(
		fn sin() : i32 {
			return 41;
		}

		fn main() : i32 {
			return sin() + 1;
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

TEST(ExternSinDoesNotAutoBindBuiltin) {
	const char* source = R"(
		extern fn sin(v : f32) : f32;

		fn main() : f32 {
			return sin(0.5);
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(!ls_call(runtime, toLs("main")));
	CAPI_END(module);
	return true;
}
