TEST(FirstClassFunctionLiteralShadowsFunctionName) {
	const char* source = R"(
		fn foo() : i32 {
			return 7;
		}

		const foo = fn() : i32 {
			return 1;
		};

		fn main() : i32 {
			return foo();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalVariableShadowsFunctionName) {
	const char* source = R"(
		fn foo() : i32 {
			return 7;
		}

		fn main() : i32 {
			var foo : i32 = 42;
			return foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalFunctionTypedVariableShadowsFunctionName) {
	const char* source = R"(
		fn foo(v : bool) : i32 {
			return 7;
		}

		fn bar(v : i32) : i32 {
			return v + 1;
		}

		fn main() : i32 {
			const foo : fn(i32) : i32 = bar;
			return foo(41);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(BytecodeLocalVariableShadowsFunctionName) {
	const char* source = R"(
		fn foo() : i32 {
			return 7;
		}

		fn main() : i32 {
			var foo : i32 = 42;
			return foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeShadowsPrimitiveTypeFails) {
	const char* source = R"(
		comptime i32 = 1;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeBindingCanNotBeShadowedByLocalFails) {
	const char* source = R"(
		comptime N = 32;

		fn main() : i32 {
			var N : i32 = 1;
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeBindingCanNotShadowGlobalFails) {
	const char* source = R"(
		var N : i32 = 1;
		comptime N = 32;

		fn main() : i32 {
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ExternFnDuplicateSameFile) {
	const char* source = R"(
		extern fn foo() : i32;	
		extern fn foo() : i32;	
		fn main() : void {
			foo();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ExternGlobalCollision) {
	const char* source = R"(
		extern fn foo() : i32;	
		const foo = 1;
		fn main() : void {
			foo();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ExternFnCollision) {
	const char* source = R"(
		extern fn foo() : i32;	
		fn foo() : void {}
		fn main() : void {
			foo();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalVarShadowsGlobalVarFails) {
	const char* source = R"(
		var x : i32 = 1;

		fn main() : i32 {
			var x : i32 = 2;
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalVarShadowsGlobalConstFails) {
	const char* source = R"(
		const x : i32 = 1;

		fn main() : i32 {
			var x : i32 = 2;
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalConstShadowsGlobalVarFails) {
	const char* source = R"(
		var x : i32 = 1;

		fn main() : i32 {
			const x : i32 = 2;
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalConstShadowsGlobalConstFails) {
	const char* source = R"(
		const x : i32 = 1;

		fn main() : i32 {
			const x : i32 = 2;
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalConstShadowsComptimeFails) {
	const char* source = R"(
		comptime N = 32;

		fn main() : i32 {
			const N : i32 = 1;
			return N;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeShadowsGlobalConstFails) {
	const char* source = R"(
		const x : i32 = 1;
		comptime x = 32;

		fn main() : i32 {
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeShadowsStructFails) {
	const char* source = R"(
		struct Foo { x : i32; }
		comptime Foo = 1;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeShadowsEnumFails) {
	const char* source = R"(
		enum Foo { A, B }
		comptime Foo = 1;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructShadowsFnFails) {
	const char* source = R"(
		fn Foo() : i32 { return 0; }
		struct Foo { x : i32; }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumShadowsFnFails) {
	const char* source = R"(
		fn Foo() : i32 { return 0; }
		enum Foo { A, B }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FnShadowsStructFails) {
	const char* source = R"(
		struct Foo { x : i32; }
		fn Foo() : i32 { return 0; }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FnShadowsEnumFails) {
	const char* source = R"(
		enum Foo { A, B }
		fn Foo() : i32 { return 0; }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructShadowsComptimeFails) {
	const char* source = R"(
		comptime Foo = 1;
		struct Foo { x : i32; }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumShadowsComptimeFails) {
	const char* source = R"(
		comptime Foo = 1;
		enum Foo { A, B }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(VarShadowsStructFails) {
	const char* source = R"(
		struct Foo { x : i32; }
		var Foo : i32 = 1;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(VarShadowsEnumFails) {
	const char* source = R"(
		enum Foo { A, B }
		var Foo : i32 = 1;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstShadowsStructFails) {
	const char* source = R"(
		struct Foo { x : i32; }
		const Foo : i32 = 1;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstShadowsEnumFails) {
	const char* source = R"(
		enum Foo { A, B }
		const Foo : i32 = 1;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructShadowsVarFails) {
	const char* source = R"(
		var Foo : i32 = 1;
		struct Foo { x : i32; }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructShadowsConstFails) {
	const char* source = R"(
		const Foo : i32 = 1;
		struct Foo { x : i32; }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumShadowsVarFails) {
	const char* source = R"(
		var Foo : i32 = 1;
		enum Foo { A, B }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumShadowsConstFails) {
	const char* source = R"(
		const Foo : i32 = 1;
		enum Foo { A, B }

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalVarShadowsStructFails) {
	const char* source = R"(
		struct Foo { x : i32; }

		fn main() : i32 {
			var Foo : i32 = 1;
			return Foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalVarShadowsEnumFails) {
	const char* source = R"(
		enum Foo { A, B }

		fn main() : i32 {
			var Foo : i32 = 1;
			return Foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalConstShadowsStructFails) {
	const char* source = R"(
		struct Foo { x : i32; }

		fn main() : i32 {
			const Foo : i32 = 1;
			return Foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalConstShadowsEnumFails) {
	const char* source = R"(
		enum Foo { A, B }

		fn main() : i32 {
			const Foo : i32 = 1;
			return Foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// --- Global-level mutual collisions ---

TEST(VarShadowsFnFails) {
	const char* source = R"(
		fn foo() : i32 { return 0; }
		var foo : i32 = 1;

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstShadowsFnFails) {
	const char* source = R"(
		fn foo() : i32 { return 0; }
		const foo : i32 = 1;

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FnShadowsVarFails) {
	const char* source = R"(
		var foo : i32 = 1;
		fn foo() : i32 { return 0; }

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FnShadowsConstFails) {
	const char* source = R"(
		const foo : i32 = 1;
		fn foo() : i32 { return 0; }

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(VarShadowsConstFails) {
	const char* source = R"(
		const x : i32 = 1;
		var x : i32 = 2;

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstShadowsVarFails) {
	const char* source = R"(
		var x : i32 = 1;
		const x : i32 = 2;

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructShadowsEnumFails) {
	const char* source = R"(
		enum Foo { A, B }
		struct Foo { x : i32; }

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumShadowsStructFails) {
	const char* source = R"(
		struct Foo { x : i32; }
		enum Foo { A, B }

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeShadowsFnFails) {
	const char* source = R"(
		fn foo() : i32 { return 0; }
		comptime foo = 1;

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FnShadowsComptimeFails) {
	const char* source = R"(
		comptime N = 1;
		fn N() : i32 { return 0; }

		fn main() : i32 { return 0; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// --- extern fn global collisions ---

TEST(ExternFnShadowsVarFails) {
	const char* source = R"(
		var foo : i32 = 1;
		extern fn foo() : i32;

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ExternFnShadowsConstFails) {
	const char* source = R"(
		const foo : i32 = 1;
		extern fn foo() : i32;

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ExternFnShadowsStructFails) {
	const char* source = R"(
		struct foo { x : i32; }
		extern fn foo() : i32;

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ExternFnShadowsEnumFails) {
	const char* source = R"(
		enum foo { A, B }
		extern fn foo() : i32;

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ExternFnShadowsComptimeFails) {
	const char* source = R"(
		comptime foo = 1;
		extern fn foo() : i32;

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(VarShadowsExternFnFails) {
	const char* source = R"(
		extern fn foo() : i32;
		var foo : i32 = 1;

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ConstShadowsExternFnFails) {
	const char* source = R"(
		extern fn foo() : i32;
		const foo : i32 = 1;

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructShadowsExternFnFails) {
	const char* source = R"(
		extern fn foo() : i32;
		struct foo { x : i32; }

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(EnumShadowsExternFnFails) {
	const char* source = R"(
		extern fn foo() : i32;
		enum foo { A, B }

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeShadowsExternFnFails) {
	const char* source = R"(
		extern fn foo() : i32;
		comptime foo = 1;

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// --- Local shadows comptime fn / struct / enum ---

TEST(LocalConstShadowsComptimeFnFails) {
	const char* source = R"(
		fn foo() : i32 { return 0; }

		fn main() : i32 {
			const foo : i32 = 1;
			return foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalVarShadowsExternFnFails) {
	const char* source = R"(
		extern fn foo() : i32;

		fn main() : i32 {
			var foo : i32 = 1;
			return foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalConstShadowsExternFnFails) {
	const char* source = R"(
		extern fn foo() : i32;

		fn main() : i32 {
			const foo : i32 = 1;
			return foo;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// --- Function param shadows global ---

TEST(ParamShadowsGlobalVarFails) {
	const char* source = R"(
		var x : i32 = 1;

		fn foo(x : i32) : i32 { return x; }

		fn main() : i32 { return foo(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ParamShadowsGlobalConstFails) {
	const char* source = R"(
		const x : i32 = 1;

		fn foo(x : i32) : i32 { return x; }

		fn main() : i32 { return foo(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ParamShadowsGlobalComptimeFails) {
	const char* source = R"(
		comptime N = 1;

		fn foo(N : i32) : i32 { return N; }

		fn main() : i32 { return foo(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ParamShadowsGlobalFnFails) {
	const char* source = R"(
		fn bar() : i32 { return 0; }

		fn foo(bar : i32) : i32 { return bar; }

		fn main() : i32 { return foo(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ParamShadowsStructFails) {
	const char* source = R"(
		struct Foo { x : i32; }

		fn bar(Foo : i32) : i32 { return Foo; }

		fn main() : i32 { return bar(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ParamShadowsEnumFails) {
	const char* source = R"(
		enum Foo { A, B }

		fn bar(Foo : i32) : i32 { return Foo; }

		fn main() : i32 { return bar(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ParamShadowsExternFnFails) {
	const char* source = R"(
		extern fn baz() : i32;

		fn foo(baz : i32) : i32 { return baz; }

		fn main() : i32 { return foo(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// --- Local shadows function parameter ---

TEST(LocalVarShadowsParamFails) {
	const char* source = R"(
		fn foo(x : i32) : i32 {
			var x : i32 = 2;
			return x;
		}

		fn main() : i32 { return foo(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(LocalConstShadowsParamFails) {
	const char* source = R"(
		fn foo(x : i32) : i32 {
			const x : i32 = 2;
			return x;
		}

		fn main() : i32 { return foo(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// --- Inner block shadows outer local ---

TEST(InnerVarShadowsOuterVarFails) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 1;
			{
				var x : i32 = 2;
			}
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(InnerConstShadowsOuterVarFails) {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 1;
			{
				const x : i32 = 2;
			}
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(InnerVarShadowsOuterConstFails) {
	const char* source = R"(
		fn main() : i32 {
			const x : i32 = 1;
			{
				var x : i32 = 2;
			}
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(InnerConstShadowsOuterConstFails) {
	const char* source = R"(
		fn main() : i32 {
			const x : i32 = 1;
			{
				const x : i32 = 2;
			}
			return x;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(InnerVarShadowsParamFails) {
	const char* source = R"(
		fn foo(x : i32) : i32 {
			{
				var x : i32 = 2;
			}
			return x;
		}

		fn main() : i32 { return foo(0); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// --- import alias collisions ---

TEST(ImportAliasShadowsGlobalVarFails) {
	const char* main_source = R"(
		import "a" as x
		var x : i32 = 1;

		fn main() : i32 { return 0; }
	)";
	const char* a_source = R"(
		fn foo() : i32 { return 0; }
	)";
	LumScriptImportFile files_storage[] = { { toLs("a"), toLs(a_source) } };
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(GlobalVarShadowsImportAliasFails) {
	const char* main_source = R"(
		var x : i32 = 1;
		import "a" as x

		fn main() : i32 { return 0; }
	)";
	const char* a_source = R"(
		fn foo() : i32 { return 0; }
	)";
	LumScriptImportFile files_storage[] = { { toLs("a"), toLs(a_source) } };
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}
