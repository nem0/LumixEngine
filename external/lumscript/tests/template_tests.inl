TEST(TemplateFunctionInferredThroughUFCS) {
	const char* source = R"(
		struct Value {
			data : i32;
		}

		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : i32 {
			const value = Value { 42 };
			const result : Value = value.identity();
			return result.data;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionIdentityF32) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : f32 {
			return identity(1.5);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionBodyCanUseTemplateParam) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			var values : T[1] = undefined;
			values[0] = a;
			return values[0];
		}

		fn main() : i32 {
			return identity(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionRecursiveInstantiationCompiles) {
	const char* source = R"(
		fn wrap[T](a : T) : T {
			return wrap(a);
		}

		fn main() : i32 {
			return wrap(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionLoopBodyIsClonedPerInstantiation) {
	const char* source = R"(
		struct Number {
			low : i32;
			high : i32;
		}

		operator +(a : Number, b : Number) : Number {
			return Number { a.low + b.low, a.high + b.high };
		}

		fn twice[T](value : T) : T {
			var result : T = value;
			var i : i32 = 0;
			while i < 1 {
				var copy : T = value;
				result = result + copy;
				i += 1;
			}
			return result;
		}

		fn main() : i32 {
			// The later i32 instantiation must not overwrite semantic annotations in
			// the loop body of the earlier, wider Number instantiation.
			const number_result = twice(Number { 20, 1 });
			const integer_result = twice(1);
			return number_result.low + number_result.high;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionNullableReturnContextCompiles) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : ?i32 {
			return identity(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionNullableReturnInfersFromContext) {
	const char* source = R"(
		fn maybe[T](value : T) : ?T {
			return value;
		}

		fn main() : ?i32 {
			// The expected ?i32 return type should infer T as i32, while preserving
			// the nullable wrapper when matching the template's ?T return type.
			return maybe(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionTypeMismatchFails) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : i32 {
			return identity(1.5);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionMultipleParams) {
	const char* source = R"(
		fn first[A, B](a : A, b : B) : A {
			return a;
		}

		fn main() : i32 {
			return first(42, true);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionMultipleParamsMismatchFails) {
	const char* source = R"(
		fn first[A, B](a : A, b : B) : A {
			return a;
		}

		fn main() : bool {
			return first(42, true);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionSwapMismatchedTypesFails) {
	const char* source = R"(
		fn swap[T](a : ref T, b : ref T) : void {
			const tmp = a;
			a = b;
			b = tmp;
		}

		fn main() : void {
			var x : i32 = 1;
			var y : f32 = 2.0;
			swap(ref x, ref y);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionRefArgumentRequiresRefSyntaxFails) {
	const char* source = R"(
		fn swap[T](a : ref T, b : ref T) : void {
		}

		fn main() : void {
			var x : i32 = 1;
			var y : i32 = 2;
			swap(x, ref y);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionRefArgumentMustBeWritableFails) {
	const char* source = R"(
		fn swap[T](a : ref T, b : ref T) : void {
		}

		fn main() : void {
			const x : i32 = 1;
			var y : i32 = 2;
			swap(ref x, ref y);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructFieldTypeMismatchFails) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		fn main() : void {
			var p : Pair[i32] = Pair[i32] { 1, 2.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructTwoInstantiations) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		fn main() : void {
			var a : Pair[i32] = Pair[i32] { 1, 2 };
			var b : Pair[f32] = Pair[f32] { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateStructInstantiationMismatchFails) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		fn main() : void {
			var a : Pair[i32] = Pair[f32] { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructPassedToFunctionMismatchFails) {
	const char* source = R"(
		struct Optional[T] {
			value   : T;
			present : bool;
		}

		fn get_value(opt : Optional[i32]) : i32 {
			return opt.value;
		}

		fn main() : i32 {
			var opt : Optional[f32] = Optional[f32] { 1.0, true };
			return get_value(opt);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateDuplicateTypeParamNameFails) {
	const char* source = R"(
		fn bad[T, T](a : T) : T {
			return a;
		}

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructDuplicateTypeParamNameFails) {
	const char* source = R"(
		struct Bad[T, T] {
			a : T;
		}

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionUnresolvedTypeParamFails) {
	const char* source = R"(
		fn make[T]() : T {
			return undefined;
		}

		fn main() : void {
			make();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Uninstantiated template used as a function value — T is unknown, not allowed.
TEST(TemplateFunctionUninstantiatedAsFirstClassValueFails) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : void {
			const f : fn(i32) : i32 = identity;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionInstantiatedAsFirstClassValueRuntime) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : i32 {
			const f : fn(i32) : i32 = identity[i32];
			return f(42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionInstantiatedPassedAsArgumentRuntime) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn apply(f : fn(i32) : i32, v : i32) : i32 {
			return f(v);
		}

		fn main() : i32 {
			return apply(identity[i32], 42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionInstantiatedTypeMismatchFails) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : void {
			const f : fn(f32) : f32 = identity[i32];
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateRecursiveStructFails) {
	const char* source = R"(
		struct Node[T] {
			value : T;
			next  : Node[T];
		}

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateIndirectRecursiveStructFails) {
	const char* source = R"(
		struct A[T] {
			b : B[T];
		}

		struct B[T] {
			a : A[T];
		}

		fn main() : void {
			var value : A[i32] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructRecursionThroughSliceCompiles) {
	const char* source = R"(
		struct Node[T] {
			value : T;
			children : Node[T][];
		}

		fn main() : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateStructRecursionThroughNullableFails) {
	const char* source = R"(
		struct Node[T] {
			value : T;
			next : ?Node[T];
		}

		fn main() : void {
			var node : Node[i32] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FailedImportedTemplateStructInstanceRemainsInvalid) {
	const char* first_source = R"(
		struct Invalid[N : i32] {
			values : i32[N];
		}

		fn first() : void {
			var value : Invalid[-1] = undefined;
		}
	)";
	const char* second_source = R"(
		import "lib"

		fn second() : void {
			var value : Invalid[-1] = undefined;
		}
	)";
	TestContext context;
	ls_module* module = ls_module_create(&context.host);
	EXPECT_TRUE(module != nullptr);
	context.diagnostics.output_enabled = false;
	EXPECT_TRUE(ls_module_parse(module, toLs(first_source), toLs("lib")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_module_typecheck(module) == LS_RESULT_FAILURE);
	EXPECT_TRUE(ls_module_parse(module, toLs(second_source), toLs("main")) == LS_RESULT_OK);
	EXPECT_TRUE(ls_module_typecheck(module) == LS_RESULT_FAILURE);
	ls_module_destroy(module);
	return true;
}

TEST(TemplateOperatorOverloadFails) {
	const char* source = R"(
		struct Box[T] {
			value : T;
		}

		operator +[T](a : Box[T], b : Box[T]) : Box[T] {
			return Box[T] { a.value + b.value };
		}

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// The correct pattern: non-templated operator on a concrete instantiation.
TEST(OperatorOnConcreteTemplateStructRuntime) {
	const char* source = R"(
		struct Box[T] {
			value : T;
		}

		operator +(a : Box[i32], b : Box[i32]) : Box[i32] {
			return Box[i32] { a.value + b.value };
		}

		fn main() : i32 {
			const a = Box[i32] { 1 };
			const b = Box[i32] { 41 };
			const c = a + b;
			return c.value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(OperatorOnConcreteTemplateStructTypeMismatchFails) {
	const char* source = R"(
		struct Box[T] {
			value : T;
		}

		operator +(a : Box[i32], b : Box[i32]) : Box[i32] {
			return Box[i32] { a.value + b.value };
		}

		fn main() : void {
			const a = Box[i32] { 1 };
			const b = Box[f32] { 2.0 };
			const c = a + b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateNestedGenericFieldTypeMismatchFails) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		struct Box[T] {
			value : T;
		}

		fn main() : void {
			var p : Box[Pair[i32]] = Box[Pair[f32]] { Pair[f32] { 1.0, 2.0 } };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionImportedTwoInstantiations) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : f32 {
			const a = lib.identity(42);
			const b : f32 = lib.identity(1.5);
			return b;
		}
	)";
	const char* lib_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(TemplateImportedFunctionTypeMismatchFails) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			return lib.identity(1.5);
		}
	)";
	const char* lib_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(TemplateImportedFunctionTwoInstantiationsRuntime) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			const a = lib.identity(40);
			const b = lib.identity(2);
			return a + b;
		}
	)";
	const char* lib_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(TemplateStructImportedAndInstantiatedRuntime) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			var p : lib.Pair[i32] = lib.Pair[i32] { 1, 41 };
			return p.first + p.second;
		}
	)";
	const char* lib_source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(TemplateStructImportedFieldUsesDeclarationUnit) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			var box : lib.Box[i32] = lib.Box[i32] { lib.Tag { 40 }, 2 };
			return box.tag.value + box.value;
		}
	)";
	const char* lib_source = R"(
		struct Tag {
			value : i32;
		}

		struct Box[T] {
			// Tag is declared in this imported unit. Instantiating Box from the
			// caller must resolve non-template field names in the declaration unit.
			tag : Tag;
			value : T;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(TemplateStructImportedFieldTypeMismatchFails) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : void {
			var p : lib.Pair[i32] = lib.Pair[i32] { 1, 2.0 };
		}
	)";
	const char* lib_source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

// Two imported libs each with their own template — exercises that instantiation
// tables for distinct units don't collide.
TEST(TemplateFunctionTwoLibsEachWithTemplateRuntime) {
	const char* main_source = R"(
		import "liba" as liba
		import "libb" as libb

		fn main() : i32 {
			return liba.double_it(21) + libb.negate(-21) + 42;
		}
	)";
	const char* liba_source = R"(
		fn double_it[T](a : T) : T {
			return a + a;
		}
	)";
	const char* libb_source = R"(
		fn negate[T](a : T) : T {
			return -a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("liba"), toLs(liba_source) },
		{ toLs("libb"), toLs(libb_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(105, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(TemplateFunctionTwoLibsSameTemplateNameRuntime) {
	const char* main_source = R"(
		import "liba" as liba
		import "libb" as libb

		fn main() : i32 {
			return liba.identity(40) + libb.identity(2);
		}
	)";
	const char* liba_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	const char* libb_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("liba"), toLs(liba_source) },
		{ toLs("libb"), toLs(libb_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(TemplateFunctionTwoUnaliasedLibsSameTemplateNameFails) {
	const char* main_source = R"(
		import "liba"
		import "libb"

		fn main() : i32 {
			return identity(42);
		}
	)";
	const char* liba_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	const char* libb_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("liba"), toLs(liba_source) },
		{ toLs("libb"), toLs(libb_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(TemplateFunctionImportBracketCallDisambiguatesAtCheckTime) {
	const char* main_source = R"(
		import "liba" as liba
		import "libb" as libb

		struct x {
			value : i32;
		}

		fn main() : i32 {
			const index = 0;
			liba.a[index] = liba.add_one;
			return liba.a[index](40) + libb.a[i32](1);
		}
	)";
	const char* liba_source = R"(
		fn add_one(v : i32) : i32 {
			return v + 1;
		}

		var a : (fn(i32) : i32)[1] = undefined;
	)";
	const char* libb_source = R"(
		fn a[T](v : T) : T {
			return v;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("liba"), toLs(liba_source) },
		{ toLs("libb"), toLs(libb_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(TemplateFunctionArrayElementCall) {
	const char* source = R"(
		fn add_one(v : i32) : i32 {
			return v + 1;
		}

		fn main() : i32 {
			var a : (fn(i32) : i32)[1] = undefined;
			const x = 0;
			a[x] = add_one;
			return a[x](41);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateImportedFunctionArrayElementCall) {
	const char* main_source = R"(
		import "liba" as liba

		fn main() : i32 {
			const x = 0;
			liba.a[x] = liba.add_one;
			return liba.a[x](41);
		}
	)";
	const char* liba_source = R"(
		fn add_one(v : i32) : i32 {
			return v + 1;
		}

		var a : (fn(i32) : i32)[1] = undefined;
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("liba"), toLs(liba_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(TemplateImportedBracketCallUserTypeName) {
	const char* main_source = R"(
		import "libb" as libb

		struct x {
			value : i32;
		}

		fn main() : i32 {
			return libb.a[i32](42);
		}
	)";
	const char* libb_source = R"(
		fn a[T](v : T) : T {
			return v;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("libb"), toLs(libb_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

// Template in lib calls another template in the same lib.
TEST(TemplateImportedChainedTemplateCallsRuntime) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			return lib.quad(10) + 2;
		}
	)";
	const char* lib_source = R"(
		fn double_it[T](a : T) : T {
			return a + a;
		}

		fn quad[T](a : T) : T {
			return double_it(double_it(a));
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	);
	return true;
}

// Runtime check for second-unit template with leading non-template functions.
TEST(TemplateFunctionInSecondUnitWithLeadingFunctionsRuntime) {
	const char* main_source = R"(
		import "util" as util
		import "lib" as lib

		fn main() : i32 {
			return lib.wrap(util.add(1, 41));
		}
	)";
	const char* util_source = R"(
		fn add(a : i32, b : i32) : i32 {
			return a + b;
		}
	)";
	const char* lib_source = R"(
		fn wrap[T](a : T) : T {
			return a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("util"), toLs(util_source) },
		{ toLs("lib"),  toLs(lib_source)  },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(TemplateFunctionIdentityI32Runtime) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : i32 {
			return identity(42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionSwapRuntime) {
	const char* source = R"(
		fn swap[T](a : ref T, b : ref T) : void {
			const tmp = a;
			a = b;
			b = tmp;
		}

		fn main() : i32 {
			var x : i32 = 1;
			var y : i32 = 41;
			swap(ref x, ref y);
			return x + y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateStructInstantiationRuntime) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		fn main() : i32 {
			var p : Pair[i32] = Pair[i32] { 1, 41 };
			return p.first + p.second;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateNestedGenericRuntime) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		struct Box[T] {
			value : T;
		}

		fn main() : i32 {
			var p : Box[Pair[i32]] = Box[Pair[i32]] { Pair[i32] { 1, 41 } };
			return p.value.first + p.value.second;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionCallingTemplateFunctionRuntime) {
	const char* source = R"(
		fn double_it[T](a : T) : T {
			return a + a;
		}

		fn quad[T](a : T) : T {
			return double_it(double_it(a));
		}

		fn main() : i32 {
			return quad(10) + 2;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionTwoInstantiationsSameCallSite) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : i32 {
			const a = identity(40);
			const b = identity(2);
			return a + b;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateStructMultipleTypeParamsRuntime) {
	const char* source = R"(
		struct Map[K, V] {
			key   : K;
			value : V;
		}

		fn main() : i32 {
			var m : Map[bool, i32] = Map[bool, i32] { true, 42 };
			return m.value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateStructPassedToFunctionRuntime) {
	const char* source = R"(
		struct Optional[T] {
			value   : T;
			present : bool;
		}

		fn get_value(opt : Optional[i32]) : i32 {
			return opt.value;
		}

		fn main() : i32 {
			var opt : Optional[i32] = Optional[i32] { 42, true };
			return get_value(opt);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateImportedFunctionRuntime) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			return lib.identity(42);
		}
	)";
	const char* lib_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(TemplateFunctionExplicitInstantiationRuntime) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : i32 {
			return identity[i32](42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionExplicitInstantiationUserTypeName) {
	const char* source = R"(
		struct Box {
			value : i32;
		}

		fn make[T]() : T {
			return undefined;
		}

		fn main() : i32 {
			const b = make[Box]();
			return b.value;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionExplicitInstantiationMultipleParamsRuntime) {
	const char* source = R"(
		fn first[A, B](a : A, b : B) : A {
			return a;
		}

		fn main() : i32 {
			return first[i32, bool](42, true);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Partial explicit type args (one of two supplied) — must be all-or-nothing.
TEST(TemplateFunctionPartialExplicitTypeArgsFails) {
	const char* source = R"(
		fn first[A, B](a : A, b : B) : A {
			return a;
		}

		fn main() : i32 {
			return first[i32](42, true);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Explicit instantiation required: no value arguments to infer from.
TEST(TemplateFunctionExplicitInstantiationNoArgs) {
	const char* source = R"(
		fn zero[T]() : T {
			return undefined;
		}

		fn main() : i32 {
			return zero[i32]();
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionEmptyTemplateParamListFails) {
	const char* source = R"(
		fn identity[]() : i32 {
			return 42;
		}

		fn main() : i32 {
			return identity();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Without explicit args and no value args to infer from, inference fails.
TEST(TemplateFunctionNoArgsUnresolvedFails) {
	const char* source = R"(
		fn zero[T]() : T {
			return undefined;
		}

		fn main() : i32 {
			return zero();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Explicit type arg drives the untyped literal, same as passing to fn identity(v : f32).
TEST(TemplateFunctionExplicitInstantiationDrivesLiteralTypeRuntime) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : f32 {
			return identity[f32](42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_FLOAT_EQ(42.0f, ls_to_f32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Too many explicit type args.
TEST(TemplateFunctionWrongNumberOfTypeArgsFails) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : i32 {
			return identity[i32, f32](42);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(TemplateFunctionExplicitInstantiationImportedRuntime) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			return lib.identity[i32](42);
		}
	)";
	const char* lib_source = R"(
		fn identity[T](a : T) : T {
			return a;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(42, ls_to_i32(runtime, -1));
	);
	return true;
}

TEST(TemplateStructWrongNumberOfTypeArgsFails) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		fn main() : void {
			var p : Pair[i32, f32] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Box[Pair[i32, f32]] — outer has one type param, inner has two distinct ones.
TEST(TemplateNestedGenericTwoParamInnerRuntime) {
	const char* source = R"(
		struct Pair[A, B] {
			first  : A;
			second : B;
		}

		struct Box[T] {
			value : T;
		}

		fn main() : i32 {
			var p : Box[Pair[i32, f32]] = Box[Pair[i32, f32]] { Pair[i32, f32] { 42, 1.5 } };
			return p.value.first;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateNestedGenericTwoParamInnerTypeMismatchFails) {
	const char* source = R"(
		struct Pair[A, B] {
			first  : A;
			second : B;
		}

		struct Box[T] {
			value : T;
		}

		fn main() : void {
			var p : Box[Pair[i32, f32]] = Box[Pair[f32, i32]] { Pair[f32, i32] { 1.0, 42 } };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Box[Box[i32]] — two levels of the same generic wrapper.
TEST(TemplateNestedGenericSameStructTwiceRuntime) {
	const char* source = R"(
		struct Box[T] {
			value : T;
		}

		fn main() : i32 {
			var b : Box[Box[i32]] = Box[Box[i32]] { Box[i32] { 42 } };
			return b.value.value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Map[K, Pair[V]] — multi-param outer wrapping a nested generic as one of its args.
TEST(TemplateNestedGenericAsSecondTypeArgRuntime) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		struct Map[K, V] {
			key   : K;
			value : V;
		}

		fn main() : i32 {
			var m : Map[bool, Pair[i32]] = Map[bool, Pair[i32]] { true, Pair[i32] { 1, 41 } };
			return m.value.first + m.value.second;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Three levels deep: Box[Box[Pair[i32]]].
TEST(TemplateNestedGenericThreeLevelsRuntime) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		struct Box[T] {
			value : T;
		}

		fn main() : i32 {
			var b : Box[Box[Pair[i32]]] = Box[Box[Pair[i32]]] { Box[Pair[i32]] { Pair[i32] { 1, 41 } } };
			return b.value.value.first + b.value.value.second;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Template function taking a nested generic as a parameter.
TEST(TemplateFunctionWithNestedGenericParamRuntime) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		struct Box[T] {
			value : T;
		}

		fn unwrap[T](b : Box[Pair[T]]) : T {
			return b.value.first;
		}

		fn main() : i32 {
			const b = Box[Pair[i32]] { Pair[i32] { 42, 0 } };
			return unwrap(b);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeGenericStructWithValueParamTypechecks) {
	const char* source = R"(
		comptime StaticArray = struct[T, N : i32] {
			values : T[N];
		}

		fn main() : void {
			var values : StaticArray[i32, 4] = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimePrimitiveValueCanBeTemplateValueArg) {
	const char* source = R"(
		comptime N = 4;

		comptime StaticArray = struct[T, Count : i32] {
			values : T[Count];
		}

		fn main() : void {
			var values : StaticArray[i32, N] = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateStructWithValueParamTypechecks) {
	const char* source = R"(
		struct StaticArray[T, N : i32] {
			values : T[N];
		}

		fn main() : void {
			var values : StaticArray[i32, 4] = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateStructMixedTypeAndValueParams) {
	const char* source = R"(
		struct Mixed[A, N : i32, B, M : i32] {
			first : A[N];
			second : B[M];
		}

		fn main() : void {
			var a : Mixed[i32, 2, f32, 3] = undefined;
			var b : Mixed[f32, 4, i32, 5] = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateStructValueParamWrongTypeFails) {
	const char* source = R"(
		struct StaticArray[T, N : i32] {
			values : T[N];
		}

		fn main() : void {
			var values : StaticArray[i32, true] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructValueParamMissingArgFails) {
	const char* source = R"(
		struct StaticArray[T, N : i32] {
			values : T[N];
		}

		fn main() : void {
			var values : StaticArray[i32] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructValueParamNonComptimeExprFails) {
	const char* source = R"(
		struct StaticArray[T, N : i32] {
			values : T[N];
		}

		fn main() : void {
			var n : i32 = 4;
			var values : StaticArray[i32, n] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructValueParamWrongCountFails) {
	const char* source = R"(
		struct StaticArray[T, N : i32] {
			values : T[N];
		}

		fn main() : void {
			var values : StaticArray[i32, 4, 8] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructValueParamNegativeSizeFails) {
	const char* source = R"(
		struct StaticArray[T, N : i32] {
			values : T[N];
		}

		fn main() : void {
			var values : StaticArray[i32, -1] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionValueParamRuntime) {
	const char* source = R"(
		fn choose[T, N : i32](value : T) : i32 {
			return N;
		}

		fn main() : i32 {
			return choose[i32, 7](42) + choose[i32, 8](42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(15, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionMixedTypeAndValueParams) {
	const char* source = R"(
		fn mixed[A, N : i32, B, M : i32](a : A, b : B) : i32 {
			return N + M;
		}

		fn main() : i32 {
			return mixed[i32, 2, f32, 3](1, 1.0)
				+ mixed[f32, 4, i32, 5](1.0, 1);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(14, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionValueParamInferred) {
	const char* source = R"(
		struct StaticArray[T, N : i32] {
			values : T[N];
		}

		fn size[T, N : i32](values : StaticArray[T, N]) : i32 {
			return N;
		}

		fn main() : i32 {
			var values : StaticArray[i32, 4] = undefined;
			return size(values);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(4, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionBoolValueArg) {
	const char* source = R"(
		fn choose[N : bool]() : bool {
			return N;
		}

		fn main() : bool {
			return choose[true]();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_to_bool(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionFloatValueArg) {
	const char* source = R"(
		fn choose[N : f32]() : f32 {
			return N;
		}

		fn main() : f32 {
			return choose[1.5]();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_to_f32(runtime, -1) == 1.5f);
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionI64ValueArg) {
	const char* source = R"(
		fn choose[N : i64]() : i64 {
			return N;
		}

		fn main() : i64 {
			return choose[2147483648]();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_to_i64(runtime, -1) == 2147483648ll);
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionF64ValueArg) {
	const char* source = R"(
		fn choose[N : f64]() : f64 {
			return N;
		}

		fn main() : f64 {
			return choose[1.5]();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(ls_to_f64(runtime, -1) == 1.5);
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionStringValueArg) {
	const char* source = R"(
		fn choose[S : string]() : string {
			return S;
		}

		fn main() : string {
			return choose["hello"]();
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_TRUE(equalStrings(ls_to_string(runtime, -1), toLs("hello")));
	CAPI_END(module);
	return true;
}

TEST(TemplateFunctionValueParamImportedRuntime) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			return lib.choose[i32, 7](42);
		}
	)";
	const char* lib_source = R"(
		fn choose[T, N : i32](value : T) : i32 {
			return N;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_RUNTIME_WITH_IMPORTS(main_source, files, runtime,
		EXPECT_TRUE(ls_call(runtime, toLs("main")));
		EXPECT_EQ(7, ls_to_i32(runtime, -1));
	);
	return true;
}

// Function explicitly returning a concrete template struct type.
TEST(TemplateFunctionReturnsConcreteTemplateStructRuntime) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		fn make_pair() : Pair[i32] {
			return Pair[i32] { 1, 41 };
		}

		fn main() : i32 {
			const p = make_pair();
			return p.first + p.second;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Template function whose return type is a derived template struct over T.
TEST(TemplateFunctionReturnsDerivedTemplateStructRuntime) {
	const char* source = R"(
		struct Box[T] {
			value : T;
		}

		fn wrap[T](v : T) : Box[T] {
			return Box[T] { v };
		}

		fn main() : i32 {
			const b = wrap(42);
			return b.value;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Non-template struct containing a fully-instantiated template struct field.
TEST(NonTemplateStructWithTemplateStructFieldRuntime) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		struct Wrapper {
			p : Pair[i32];
			x : i32;
		}

		fn main() : i32 {
			var w : Wrapper = Wrapper { Pair[i32] { 1, 41 }, 0 };
			return w.p.first + w.p.second + w.x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Returning a fully-instantiated template function value from a function.
TEST(TemplateFunctionReturnedAsFirstClassValueRuntime) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn get_identity() : fn(i32) : i32 {
			return identity[i32];
		}

		fn main() : i32 {
			const f = get_identity();
			return f(42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Explicit type argument on a ref-parameter template function.
TEST(TemplateFunctionExplicitInstantiationRefParamsRuntime) {
	const char* source = R"(
		fn swap[T](a : ref T, b : ref T) : void {
			const tmp = a;
			a = b;
			b = tmp;
		}

		fn main() : i32 {
			var x : i32 = 1;
			var y : i32 = 41;
			swap[i32](ref x, ref y);
			return x + y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Nullable of a fully-instantiated template struct.
TEST(NullableTemplateStructInstantiationRuntime) {
	const char* source = R"(
		struct Pair[T] {
			first  : T;
			second : T;
		}

		fn main() : i32 {
			var x : ?Pair[i32] = null;
			if x == null {
				return 42;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Template function taking T[] (slice of T) as a parameter.
TEST(TemplateFunctionSliceOfTParamRuntime) {
	const char* source = R"(
		fn first[T](values : T[]) : T {
			return values[0];
		}

		fn main() : i32 {
			var arr : i32[3] = undefined;
			arr[0] = 42;
			arr[1] = 1;
			arr[2] = 2;
			return first(arr[:]);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

// Comptime template struct and function bindings verified at runtime.
TEST(ComptimeGenericStructBindingRuntime) {
	const char* source = R"(
		comptime Pair = struct[T] {
			first  : T;
			second : T;
		}

		fn main() : i32 {
			const p = Pair[i32] { 20, 22 };
			return p.first + p.second;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeGenericFunctionBindingRuntime) {
	const char* source = R"(
		comptime identity = fn[T](value : T) : T {
			return value;
		}

		fn main() : i32 {
			return identity[i32](42);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeGenericStructWithValueParamRuntime) {
	const char* source = R"(
		comptime StaticArray = struct[T, N : i32] {
			values : T[N];
		}

		fn main() : i32 {
			var arr : StaticArray[i32, 4] = undefined;
			arr.values[0] = 42;
			return arr.values[0];
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(42, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}
