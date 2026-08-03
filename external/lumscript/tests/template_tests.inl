TEST(TemplateFunctionInferredThroughUFCS) {
	const char* source = R"(
		struct Value {
			data : i32;
		}

		fn identity(a : $T) : T {
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

TEST(TemplateFunctionUFCSReceiverInferenceMismatchFails) {
	const char* source = R"(
		struct Value {
			data : i32;
		}

		fn identity(a : [1]$T) : T {
			return a[0];
		}

		fn main() : void {
			const value = Value { 42 };
			value.identity();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionIdentityF32) {
	const char* source = R"(
		fn identity(a : $T) : T {
			return a;
		}

		fn main() : f32 {
			return identity(1.5 as f32);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionRefArgumentRejectsUnassignableExpression) {
	const char* source = R"(
		fn increment(v : ref $T) : void {
		}

		fn main() : void {
			increment(ref 1);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionNullableRefParameterFailsDuringInstantiation) {
	const char* source = R"(
		fn clear(v : ref ?$T) : void {
		}

		fn main() : void {
			var value : ?i32 = 10;
			clear(ref value);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionBodyCanUseTemplateParam) {
	const char* source = R"(
		fn identity(a : $T) : T {
			var values : [1]T = undefined;
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
		fn wrap(a : $T) : T {
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

		fn twice(value : $T) : T {
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

TEST(TemplateFunctionNullableReturnContextSuccess) {
	const char* source = R"(
		fn identity(a : $T) : T {
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
		fn maybe(value : $T) : ?T {
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
		fn identity(a : $T) : T {
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
		fn first(a : $A, b : $B) : A {
			return a;
		}

		fn main() : i32 {
			return first(42, true);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// The specialization key must follow declared generic-parameter order, not the
// order in which argument and return-context inference discovered each binding.
TEST(TemplateFunctionSpecializationKeyIsCanonical) {
	const char* source = R"(
		fn second(a : $A, b : $B) : B {
			return b;
		}

		fn main() : i32 {
			var first_a : i32 = 1;
			var first_b : f32 = 2.0;
			const warmup = second(first_a, first_b);

			var second_a : f32 = warmup;
			var second_b : i32 = 42;
			return second(second_a, second_b);
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

TEST(TemplateFunctionMultipleParamsMismatchFails) {
	const char* source = R"(
		fn first(a : $A, b : $B) : A {
			return a;
		}

		fn main() : bool {
			return first(42, true);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionRepeatedTypeParamMismatchFailsDuringInference) {
	const char* source = R"(
		fn same_type(a : $T, b : T) : T {
			return a;
		}

		fn main() : void {
			const value : f32 = 1.5;
			same_type(42, value);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionSwapMismatchedTypesFails) {
	const char* source = R"(
		fn swap(a : ref $T, b : ref T) : void {
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
		fn swap(a : ref $T, b : ref T) : void {
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

TEST(TemplateFunctionRefArgumentRejectsNonRefUnaryFails) {
	const char* source = R"(
		fn increment(v : ref $T) : void {
		}

		fn main() : void {
			var x : i32 = 1;
			increment(-x);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionRefArgumentMustBeWritableFails) {
	const char* source = R"(
		fn swap(a : ref $T, b : ref T) : void {
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
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn main() : void {
			var p : Pair(i32) = Pair(i32) { 1, 2.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructTwoInstantiations) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn main() : void {
			var a : Pair(i32) = Pair(i32) { 1, 2 };
			var b : Pair(f32) = Pair(f32) { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateStructInstantiationMismatchFails) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn main() : void {
			var a : Pair(i32) = Pair(f32) { 1.0, 2.0 };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructPassedToFunctionMismatchFails) {
	const char* source = R"(
		fn Optional(T : type) : type { return struct { value : T; present : bool; }; }

		fn get_value(opt : Optional(i32)) : i32 {
			return opt.value;
		}

		fn main() : i32 {
			var opt : Optional(f32) = Optional(f32) { 1.0, true };
			return get_value(opt);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateDuplicateTypeParamNameFails) {
	const char* source = R"(
		fn bad(a : $T, b : $T) : T {
			return a;
		}

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateConflictingComptimeArgumentFails) {
	const char* source = R"(
		fn choose(value : $T, T : comptime i32) : T {
			return value;
		}

		fn main() : void {
			choose(42, 7);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionUnresolvedTypeParamFails) {
	const char* source = R"(
		fn make(T : comptime type) : T {
			return undefined;
		}

		fn main() : void {
			make();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateTypeArgumentCannotBeUsedAsComptimeValueFails) {
	const char* source = R"(
		fn bad(T : comptime type, value : comptime i32) : void {}

		fn main() : void {
			bad(i32, T);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Uninstantiated template used as a function value - T is unknown, not allowed.
TEST(TemplateFunctionUninstantiatedAsFirstClassValueFails) {
	const char* source = R"(
		fn identity(a : $T) : T {
			return a;
		}

		fn main() : void {
			const f = identity;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionInstantiatedAsFirstClassValueRuntime) {
	const char* source = R"(
		fn identity(a : $T) : T {
			return a;
		}

		fn main() : i32 {
			const f : fn(i32) : i32 = identity;
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

TEST(TemplateFunctionFirstClassValueParameterInferenceMismatchFails) {
	const char* source = R"(
		fn same_type(a : $T, b : T) : T {
			return a;
		}

		fn get_same_type() : fn(i32, f32) : i32 {
			return same_type;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionTargetHasTooManyParametersFails) {
	const char* source = R"(
		fn identity(a : $T) : T {
			return a;
		}

		fn main() : void {
			const f : fn(i32, i32) : i32 = identity;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionWithComptimeParameterAsFirstClassValueFails) {
	const char* source = R"(
		fn identity(T : comptime type, value : T) : T {
			return value;
		}

		fn main() : void {
			const f : fn(i32) : i32 = identity;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionInstantiatedPassedAsArgumentRuntime) {
	const char* source = R"(
		fn identity(a : $T) : T {
			return a;
		}

		fn apply(f : fn(i32) : i32, v : i32) : i32 {
			return f(v);
		}

		fn main() : i32 {
			return apply(identity, 42);
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
		fn add_one(a : $T) : i32 {
			return a + 1;
		}

		fn main() : void {
			const f : fn(f32) : f32 = add_one;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionFailedInstantiationAsFirstClassValueFails) {
	const char* source = R"(
		fn bad(a : $T) : T {
			return a + "invalid";
		}

		fn main() : void {
			const f : fn(i32) : i32 = bad;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateRecursiveStructFails) {
	const char* source = R"(
		fn Node(T : type) : type { return struct { value : T; next : Node(T); }; }

		fn main() : void {
			var node : Node(i32) = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateIndirectRecursiveStructFails) {
	const char* source = R"(
		fn A(T : type) : type { return struct { b : B(T); }; }
		fn B(T : type) : type { return struct { a : A(T); }; }

		fn main() : void {
			var value : A(i32) = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateStructRecursionThroughSliceCompiles) {
	const char* source = R"(
		fn Node(T : type) : type { return struct { value : T; children : []Node(T); }; }

		fn main() : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateStructRecursionThroughNullableFails) {
	const char* source = R"(
		fn Node(T : type) : type { return struct { value : T; next : ?Node(T); }; }

		fn main() : void {
			var node : Node(i32) = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateOperatorOverloadFails) {
	const char* source = R"(
		fn Box(T : type) : type { return struct { value : T; }; }

		operator +(a : Box($T), b : Box(T)) : Box(T) {
			return Box(T) { a.value + b.value };
		}

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// The correct pattern: non-templated operator on a concrete instantiation.
TEST(OperatorOnConcreteTemplateStructRuntime) {
	const char* source = R"(
		fn Box(T : type) : type { return struct { value : T; }; }

		operator +(a : Box(i32), b : Box(i32)) : Box(i32) {
			return Box(i32) { a.value + b.value };
		}

		fn main() : i32 {
			const a = Box(i32) { 1 };
			const b = Box(i32) { 41 };
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
		fn Box(T : type) : type { return struct { value : T; }; }

		operator +(a : Box(i32), b : Box(i32)) : Box(i32) {
			return Box(i32) { a.value + b.value };
		}

		fn main() : void {
			const a = Box(i32) { 1 };
			const b = Box(f32) { 2.0 };
			const c = a + b;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateNestedGenericFieldTypeMismatchFails) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }
		fn Box(T : type) : type { return struct { value : T; }; }

		fn main() : void {
			var p : Box(Pair(i32)) = Box(Pair(f32)) { Pair(f32) { 1.0, 2.0 } };
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
			const b : f32 = lib.identity(1.5 as f32);
			return b;
		}
	)";
	const char* lib_source = R"(
		fn identity(a : $T) : T {
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
		fn identity(a : $T) : T {
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
		fn identity(a : $T) : T {
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
			var p : lib.Pair(i32) = lib.Pair(i32) { 1, 41 };
			return p.first + p.second;
		}
	)";
	const char* lib_source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }
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

// Explicit arguments are written in the importing module and must be resolved
// there, while the instantiated template's field declarations use the owner unit.
TEST(TemplateStructImportedAcceptsCallerTypeArgument) {
	const char* main_source = R"(
		import "lib" as lib

		struct LocalValue {
			value : i32;
		}

		fn main() : i32 {
			var box = lib.Box(LocalValue) { LocalValue { 42 } };
			return box.value.value;
		}
	)";
	const char* lib_source = R"(
		fn Box(T : type) : type { return struct { value : T; }; }
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

// An instantiation that failed once is cached as failed. Asking for it again from another
// unit must report the failure again instead of handing back the broken instance.
TEST(FailedImportedFactoryInstanceRemainsInvalid) {
	const char* first_source = R"(
		fn Invalid(N : comptime i32) : type {
			return struct { values : [N]i32; };
		}

		fn first() : void {
			var value : Invalid(-1) = undefined;
		}
	)";
	const char* second_source = R"(
		import "lib"

		fn second() : void {
			var value : Invalid(-1) = undefined;
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

TEST(TemplateStructImportedFieldUsesDeclarationUnit) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			var box : lib.Box(i32) = lib.Box(i32) { lib.Tag { 40 }, 2 };
			return box.tag.value + box.value;
		}
	)";
	const char* lib_source = R"(
		struct Tag {
			value : i32;
		}

		fn Box(T : type) : type { return struct {
			// Tag is declared in this imported unit. Instantiating Box from the
			// caller must resolve non-template field names in the declaration unit.
			tag : Tag;
			value : T;
		}; }
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
			var p : lib.Pair(i32) = lib.Pair(i32) { 1, 2.0 };
		}
	)";
	const char* lib_source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_FAIL_WITH_IMPORTS(main_source, files);
	return true;
}

// Two imported libs each with their own template - exercises that instantiation
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
		fn double_it(a : $T) : T {
			return a + a;
		}
	)";
	const char* libb_source = R"(
		fn negate(a : $T) : T {
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
		fn identity(a : $T) : T {
			return a;
		}
	)";
	const char* libb_source = R"(
		fn identity(a : $T) : T {
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
		fn identity(a : $T) : T {
			return a;
		}
	)";
	const char* libb_source = R"(
		fn identity(a : $T) : T {
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

TEST(TemplateFunctionImportCallDisambiguatesAtCheckTime) {
	const char* main_source = R"(
		import "liba" as liba
		import "libb" as libb

		struct x {
			value : i32;
		}

		fn main() : i32 {
			const index = 0;
			liba.a[index] = liba.add_one;
			return liba.a[index](40) + libb.a(1);
		}
	)";
	const char* liba_source = R"(
		fn add_one(v : i32) : i32 {
			return v + 1;
		}

		var a : [1](fn(i32) : i32) = undefined;
	)";
	const char* libb_source = R"(
		fn a(v : $T) : T {
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
			var a : [1](fn(i32) : i32) = undefined;
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

		var a : [1](fn(i32) : i32) = undefined;
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("liba"), toLs(liba_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

TEST(TemplateImportedFunctionCallUserTypeName) {
	const char* main_source = R"(
		import "libb" as libb

		struct x {
			value : i32;
		}

		fn main() : i32 {
			return libb.a(42);
		}
	)";
	const char* libb_source = R"(
		fn a(v : $T) : T {
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
		fn double_it(a : $T) : T {
			return a + a;
		}

		fn quad(a : $T) : T {
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
		fn wrap(a : $T) : T {
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
		fn identity(a : $T) : T {
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
		fn swap(a : ref $T, b : ref T) : void {
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
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn main() : i32 {
			var p : Pair(i32) = Pair(i32) { 1, 41 };
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
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }
		fn Box(T : type) : type { return struct { value : T; }; }

		fn main() : i32 {
			var p : Box(Pair(i32)) = Box(Pair(i32)) { Pair(i32) { 1, 41 } };
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
		fn double_it(a : $T) : T {
			return a + a;
		}

		fn quad(a : $T) : T {
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
		fn identity(a : $T) : T {
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
		fn Map(K : type, V : type) : type { return struct { key : K; value : V; }; }

		fn main() : i32 {
			var m : Map(bool, i32) = Map(bool, i32) { true, 42 };
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
		fn Optional(T : type) : type { return struct { value : T; present : bool; }; }

		fn get_value(opt : Optional(i32)) : i32 {
			return opt.value;
		}

		fn main() : i32 {
			var opt : Optional(i32) = Optional(i32) { 42, true };
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
		fn identity(a : $T) : T {
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

TEST(TemplateFunctionInferredInstantiationRuntime) {
	const char* source = R"(
		fn identity(a : $T) : T {
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

TEST(TemplateFunctionComptimeTypeParamUserTypeName) {
	const char* source = R"(
		struct Box {
			value : i32;
		}

		fn make(T : comptime type) : T {
			return undefined;
		}

		fn main() : i32 {
			const b = make(Box);
			return b.value;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionInferredInstantiationMultipleParamsRuntime) {
	const char* source = R"(
		fn first(a : $A, b : $B) : A {
			return a;
		}

		fn main() : i32 {
			return first(42, true);
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

// Missing value argument: the second type parameter cannot be inferred.
TEST(TemplateFunctionMissingArgumentFails) {
	const char* source = R"(
		fn first(a : $A, b : $B) : A {
			return a;
		}

		fn main() : i32 {
			return first(42);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Explicit comptime type parameter required: no value arguments to infer from.
TEST(TemplateFunctionComptimeTypeParamNoArgs) {
	const char* source = R"(
		fn zero(T : comptime type) : T {
			return undefined;
		}

		fn main() : i32 {
			return zero(i32);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TemplateFunctionEmptyTemplateParamListFails) {
	const char* source = R"(
		fn identity(a : $) : i32 {
			return 42;
		}

		fn main() : i32 {
			return identity(0);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(TemplateFunctionBracketSyntaxFails) {
	const char* source = R"(
		fn identity[T](a : T) : T {
			return a;
		}

		fn main() : void {}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// A template has no type of its own until it is instantiated, so naming one where a type
// is expected must be diagnosed instead of dereferencing its missing resolved type.
TEST(TemplateNameAsTypeAnnotationFails) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn main() : void {
			var p : Pair = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Brackets in type position index a compile-time sequence. Applying type arguments with
// them must be rejected, not silently treated as an index.
TEST(TemplateStructBracketSyntaxFails) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn main() : void {
			var p : Pair[i32] = undefined;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// A generic parameter name must not rewrite an import qualifier in a separate
// type annotation when the function specialization is cloned.
TEST(TemplateFunctionGenericNameDoesNotShadowTypeImportQualifier) {
	const char* main_source = R"(
		import "lib" as lib

		fn read(value : $lib, box : lib.Box) : i32 {
			return box.value;
		}

		fn main() : i32 {
			return read(42, lib.Box { 7 });
		}
	)";
	const char* lib_source = R"(
		struct Box {
			value : i32;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ toLs("lib"), toLs(lib_source) },
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	EXPECT_COMPILE_WITH_IMPORTS(main_source, files);
	return true;
}

// Without a comptime type argument and no value args to infer from, inference fails.
TEST(TemplateFunctionNoArgsUnresolvedFails) {
	const char* source = R"(
		fn zero(T : comptime type) : T {
			return undefined;
		}

		fn main() : i32 {
			return zero();
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Explicit comptime type parameter drives the untyped literal, same as passing to fn identity(v : f32).
TEST(TemplateFunctionComptimeTypeParamDrivesLiteralTypeRuntime) {
	const char* source = R"(
		fn identity(T : comptime type, a : T) : T {
			return a;
		}

		fn main() : f32 {
			return identity(f32, 42);
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

// Too many function arguments.
TEST(TemplateFunctionWrongNumberOfArgsFails) {
	const char* source = R"(
		fn identity(T : comptime type, a : T) : T {
			return a;
		}

		fn main() : i32 {
			return identity(i32, f32, 42);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(TemplateFunctionComptimeTypeParamImportedRuntime) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			return lib.identity(i32, 42);
		}
	)";
	const char* lib_source = R"(
		fn identity(T : comptime type, a : T) : T {
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

// Box(Pair(i32, f32)) - outer has one type parameter, inner has two distinct ones.
TEST(TemplateNestedGenericTwoParamInnerRuntime) {
	const char* source = R"(
		fn Pair(A : type, B : type) : type { return struct { first : A; second : B; }; }
		fn Box(T : type) : type { return struct { value : T; }; }

		fn main() : i32 {
			var p : Box(Pair(i32, f32)) = Box(Pair(i32, f32)) { Pair(i32, f32) { 42, 1.5 } };
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
		fn Pair(A : type, B : type) : type { return struct { first : A; second : B; }; }
		fn Box(T : type) : type { return struct { value : T; }; }

		fn main() : void {
			var p : Box(Pair(i32, f32)) = Box(Pair(f32, i32)) { Pair(f32, i32) { 1.0, 42 } };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Box(Box(i32)) - two levels of the same generic wrapper.
TEST(TemplateNestedGenericSameStructTwiceRuntime) {
	const char* source = R"(
		fn Box(T : type) : type { return struct { value : T; }; }

		fn main() : i32 {
			var b : Box(Box(i32)) = Box(Box(i32)) { Box(i32) { 42 } };
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

// Map(bool, Pair(i32)) - multi-parameter outer wrapping a nested generic as one argument.
TEST(TemplateNestedGenericAsSecondTypeArgRuntime) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }
		fn Map(K : type, V : type) : type { return struct { key : K; value : V; }; }

		fn main() : i32 {
			var m : Map(bool, Pair(i32)) = Map(bool, Pair(i32)) { true, Pair(i32) { 1, 41 } };
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

// Three levels deep: Box(Box(Pair(i32))).
TEST(TemplateNestedGenericThreeLevelsRuntime) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }
		fn Box(T : type) : type { return struct { value : T; }; }

		fn main() : i32 {
			var b : Box(Box(Pair(i32))) = Box(Box(Pair(i32))) { Box(Pair(i32)) { Pair(i32) { 1, 41 } } };
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
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }
		fn Box(T : type) : type { return struct { value : T; }; }

		fn unwrap(b : Box(Pair($T))) : T {
			return b.value.first;
		}

		fn main() : i32 {
			const b = Box(Pair(i32)) { Pair(i32) { 42, 0 } };
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

TEST(TemplateStructSizeofUsesTypeBinding) {
	const char* source = R"(
		fn Storage(T : type) : type { return struct { bytes : [sizeof(T)]byte; }; }

		fn main() : void {
			var storage : Storage(i64) = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeFunctionParameterBasicRuntime) {
	const char* source = R"(
		fn choose(value : i32, n : comptime i32) : i32 {
			return n;
		}

		fn main() : i32 {
			return choose(0, 7) + choose(0, 8);
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

TEST(ComptimeFunctionParameterFloatToIntFails) {
	const char* source = R"(
		fn choose(n : comptime i32) : i32 {
			return n;
		}

		fn main() : void {
			choose(1.5);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeFunctionParameterWrongTypeFails) {
	const char* source = R"(
		fn choose(n : comptime i32) : i32 {
			return n;
		}

		fn main() : void {
			choose(true);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}


TEST(ComptimeFloatBinaryOp) {
	const char* source = R"(
		fn choose(n : comptime f32) : f32 {
			return n;
		}

		fn main() : f32 {
			return choose(2.5 + 3.2);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeIntBinaryOp) {
	const char* source = R"(
		fn choose(n : comptime i32) : i32 {
			return n;
		}

		fn main() : i32 {
			return choose(2 + 2);
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

TEST(ComptimeFunctionParameterMultipleTypesRuntime) {
	const char* source = R"(
		fn mixed(a : i32, n : comptime i32, b : f32, m : comptime i32) : i32 {
			return n + m;
		}

		fn main() : i32 {
			return mixed(1, 2, 1.0, 3) + mixed(1, 4, 1.0, 5);
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

TEST(ComptimeFunctionParameterBoolRuntime) {
	const char* source = R"(
		fn choose(n : comptime bool) : bool {
			return n;
		}

		fn main() : bool {
			return choose(true);
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

TEST(ComptimeFunctionParameterF32Runtime) {
	const char* source = R"(
		fn choose(n : comptime f32) : f32 {
			return n;
		}

		fn main() : f32 {
			return choose(1.5);
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

TEST(ComptimeFunctionParameterI64Runtime) {
	const char* source = R"(
		fn choose(n : comptime i64) : i64 {
			return n;
		}

		fn main() : i64 {
			return choose(2147483648);
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

TEST(ComptimeFunctionParameterF64Runtime) {
	const char* source = R"(
		fn choose(n : comptime f64) : f64 {
			return n;
		}

		fn main() : f64 {
			return choose(1.5);
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

TEST(ComptimeFunctionParameterStringRuntime) {
	const char* source = R"(
		fn choose(s : comptime []const u8) : []const u8 {
			return s;
		}

		fn main() : []const u8 {
			return choose("hello");
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

TEST(ComptimeFunctionParameterImportedRuntime) {
	const char* main_source = R"(
		import "lib" as lib

		fn main() : i32 {
			return lib.choose(42, 7);
		}
	)";
	const char* lib_source = R"(
		fn choose(value : i32, n : comptime i32) : i32 {
			return n;
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

// Function explicitly returning a concrete factory-produced struct type.
TEST(TemplateFunctionReturnsConcreteTemplateStructRuntime) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn make_pair() : Pair(i32) {
			return Pair(i32) { 1, 41 };
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

// Template function whose return type is a derived factory-produced struct over T.
TEST(TemplateFunctionReturnsDerivedTemplateStructRuntime) {
	const char* source = R"(
		fn Box(T : type) : type { return struct { value : T; }; }

		fn wrap(v : $T) : Box(T) {
			return Box(T) { v };
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

// Non-template struct containing a factory-produced struct field.
TEST(NonTemplateStructWithTemplateStructFieldRuntime) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		struct Wrapper {
			p : Pair(i32);
			x : i32;
		}

		fn main() : i32 {
			var w : Wrapper = Wrapper { Pair(i32) { 1, 41 }, 0 };
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
		fn identity(a : $T) : T {
			return a;
		}

		fn get_identity() : fn(i32) : i32 {
			return identity;
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

TEST(TemplateFunctionFirstClassValueNullableParameterMismatchFails) {
	const char* source = R"(
		fn identity(a : $T) : T {
			return a;
		}

		fn get_identity() : fn(?i32) : i32 {
			return identity;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Reject a template function with more runtime parameters than its target function type.
TEST(TemplateFunctionTooManyParametersForTarget) {
	const char* source = R"(
		fn first(a : $T, b : $U) : T {
			return a;
		}

		fn get_first() : fn(i32) : i32 {
			return first;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Inferred type argument on a ref-parameter template function.
TEST(TemplateFunctionInferredInstantiationRefParamsRuntime) {
	const char* source = R"(
		fn swap(a : ref $T, b : ref T) : void {
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

// Nullable of a factory-produced struct.
TEST(NullableTemplateStructInstantiationRuntime) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn main() : i32 {
			var x : ?Pair(i32) = null;
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
		fn first(values : []$T) : T {
			return values[0];
		}

		fn main() : i32 {
			var arr : [3]i32 = undefined;
			arr[0] = 42;
			arr[1] = 1;
			arr[2] = 2;
			const s : []i32 = arr[:];
			return first(s);
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

// An untyped undefined value cannot provide an inferred template argument type.
TEST(TemplateFunctionUndefinedArgumentFails) {
	const char* source = R"(
		fn identity(value : $T) : T {
			return value;
		}

		fn main() : void {
			identity(undefined);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Comptime type-factory and function bindings verified at runtime.
TEST(ComptimeGenericStructBindingRuntime) {
	const char* source = R"(
		fn Pair(T : type) : type { return struct { first : T; second : T; }; }

		fn main() : i32 {
			const p = Pair(i32) { 20, 22 };
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
		comptime identity = fn(value : $T) : T {
			return value;
		};

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

TEST(ComptimeFunctionParameterRuntime) {
	const char* source = R"(
		fn repeat(text : []const u8, count : comptime i32) : i32 {
			return count;
		}

		fn main() : i32 {
			return repeat("hi", 5);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), {}, nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(5, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeFunctionParameterDependentArraySizeRuntime) {
	const char* source = R"(
		fn splat(value : f32, n : comptime i32) : [n]f32 {
			var result : [n]f32 = undefined;
			for i in 0..(n - 1) {
				result[i] = value;
			}
			return result;
		}

		fn main() : i32 {
			const arr = splat(1.0, 4);
			return 42;
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

TEST(VarGlobalAsComptimeFunctionParameterFails) {
	const char* source = R"(
		fn repeat(text : []const u8, count : comptime i32) : void {}
		
		var n : i32 = 5;
		fn main() : void {
			repeat("hi", n);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeFunctionParameterNonConstantArgumentFails) {
	const char* source = R"(
		fn repeat(text : []const u8, count : comptime i32) : void {}

		fn main() : void {
			var n : i32 = 5;
			repeat("hi", n);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DuplicatedTemplateTypeArgFails) {
	const char* source = R"(
		fn repeat(a : $T, b : $T) : void {}

		fn main() : void {
			repeat(i32, f32);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(DuplicatedTemplateValueArgFails) {
	const char* source = R"(
		fn repeat(count : comptime i32, count : comptime i32) : void {}

		fn main() : void {
			repeat(4, 5);
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

// Instantiation clones the body; an if without else has a null else_branch.
TEST(TemplateFunctionBodyIfWithoutElse) {
	const char* source = R"(
		fn clamp_negative(a : $T) : T {
			var result : T = a;
			if a < 0 {
				result = 0;
			}
			return result;
		}

		fn main() : i32 {
			return clamp_negative(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// Instantiation clones the body; a bare return has a null expression.
TEST(TemplateFunctionBodyBareReturn) {
	const char* source = R"(
		fn touch(a : $T) : void {
			return;
		}

		fn main() : void {
			touch(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// Instantiation clones the body; a slice with default bounds has null begin/end.
TEST(TemplateFunctionBodySliceDefaultBounds) {
	const char* source = R"(
		fn first(values : []$T) : T {
			const all = values[:];
			return all[0];
		}

		fn main() : i32 {
			var arr : [1]i32 = undefined;
			arr[0] = 42;
			const s : []i32 = arr[:];
			return first(s);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// Instantiation clones the body; a single-value match pattern has a null range end.
TEST(TemplateFunctionBodyMatchSingleValuePattern) {
	const char* source = R"(
		fn classify(v : $T) : i32 {
			match v {
				case 0:
					return 1;
				case:
					return 2;
			}
		}

		fn main() : i32 {
			return classify(42);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}



