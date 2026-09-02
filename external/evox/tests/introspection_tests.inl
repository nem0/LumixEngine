TEST(FunctionTypeIntrospectionParamsAndReturn) {
	const char* source = R"(
		comptime Handler = fn(i32, f32) : bool;
		comptime params = Handler::params;
		comptime result = Handler::ret;
		comptime count = params.length;

		fn main() : i32 {
			return count;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(TypeMembersAcceptValueReceivers) {
	const char* source = R"(
		struct Value { field : i32; }
		fn main() : i32 {
			var value : Value = undefined;
			comptime name = value::name;
			if name == "Value" { return 0; }
			return 1;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(0, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(FunctionTypeIntrospectionRejectsNonFunction) {
	const char* source = R"(
		comptime params = i32::params;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FunctionTypeIntrospectionVoidReturn) {
	const char* source = R"(
		comptime Handler = fn(i32) : void;
		comptime result = Handler::ret;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FunctionTypeIntrospectionBindsAndIndexesParams) {
	const char* source = R"(
		comptime Handler = fn(i32, f32) : bool;
		comptime params = Handler::params;
		comptime First = params[0].type;
		fn main(value : First) : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FunctionTypeIntrospectionParamsRequireInference) {
	const char* source = R"(
		comptime Handler = fn(i32, f32) : bool;
		comptime params : []type = Handler::params;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(FunctionTypeIntrospectionNamedParams) {
	const char* source = R"(
		comptime F = fn(a : i32, b : f32) : void;
		comptime p0 = F::params[0];
		comptime p1 = F::params[1];
		comptime n0 = p0.name;
		comptime n1 = p1.name;
		comptime t0 = p0.type;
		comptime t1 = p1.type;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FunctionTypeIntrospectionNamedParamsUnnamedGetsEmptyString) {
	const char* source = R"(
		comptime F = fn(i32, f32) : void;
		comptime p0 = F::params[0];
		comptime n0 = p0.name;

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FunctionTypeIntrospectionMixedNamedUnnamed) {
	const char* source = R"(
		comptime F = fn(a : i32, f32, c : bool) : void;
		comptime p0 = F::params[0];
		comptime p1 = F::params[1];
		comptime p2 = F::params[2];

		fn main() : i32 {
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(FunctionTypeIntrospectionParamsStayComptimeOnly) {
	const char* source = R"(
		comptime Handler = fn(i32) : void;
		fn main() : void {
			var params = Handler::params;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnrollForStructTypeFields) {
	const char* source = R"(
		struct Value { i : i32; f : f32; }
		fn main() : void {
			unroll for field in Value::fields {
				if field.type == i32 { var name : []const u8 = field.name; }
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForStructValueFieldsRejected) {
	const char* source = R"(
		struct Value { i : i32; f : f32; }
		fn main() : i32 {
			var value : Value = Value { 1, 2.0 };
			unroll for field in value { if field.type == i32 { field.value = 7; } }
			return value.i;
		}
	)";
		EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnrollForEnumValues) {
	const char* source = R"(
		enum State { Idle, Running, Done }
		fn main() : i32 {
			var count : i32 = 0;
			unroll for i, state in State::values { count += i; }
			return count;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForUnionTypes) {
	const char* source = R"(
		comptime U = i32 | f32;
		fn main() : void {
			unroll for M in U::types { if M == i32 { var value : i32 = 1; } }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForComptimeSequenceRuntimeForRejected) {
	const char* source = R"(
		comptime U = i32 | f32;
		comptime values : []type = U::types;
		fn main() : void { for value in values { } }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnrollForBindingIsImmutable) {
	const char* source = R"(
		fn main() : void { unroll for i in 0..2 { i = 1; } }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnrollForBoundSequenceCanBeReused) {
	const char* source = R"(
		comptime U = i32 | f32;
		comptime members : []type = U::types;
		comptime first = members[0];
		fn main() : void {
			unroll for M in members { if M == first { var value : i32 = 1; } }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForFieldTypeCursorHasNoValue) {
	const char* source = R"(
		struct Value { x : i32; }
		fn main() : void {
			unroll for field in Value::fields { field.value = 1; }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(RuntimeForStructValueRejected) {
	const char* source = R"(
		struct Value { x : i32; y : f32; }
		fn main() : void {
			var value : Value = Value { 1, 2.0 };
			for field in value { }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnrollForRuntimeRangeBoundRejected) {
	const char* source = R"(
		fn main(limit : i32) : void { unroll for i in 0..limit { } }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnrollForStructValueOperandRejected) {
	const char* source = R"(
		struct Value { x : i32; }
		fn main() : i32 {
			var value : Value = Value { 3 };
			unroll for value in value { if value.type == i32 { return value.value; } }
			return 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionStructFieldsCanBeIndexed) {
	const char* source = R"(
		struct S { x : i32; y : f32; }
		comptime fields = S::fields;
		comptime first = fields[0];
		fn main() : void { var name : []const u8 = first.name; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(StructFieldIndexByLiteral) {
	const char* source = R"(
		struct Value { x : i32; y : f32; }
		fn main() : i32 {
			var value : Value = Value { 0, 0.0 };
			value["x"] = 7;
			return value["x"];
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(StructFieldIndexByComptimeStringAndCall) {
	const char* source = R"(
		struct Value { x : i32; }
		fn field_name() : []const u8 { return "x"; }
		comptime name = "x";
		fn main() : void {
			var value : Value = undefined;
			value[name] = 1;
			value[field_name()] = 2;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(StructFieldIndexFromUnrolledFieldName) {
	const char* source = R"(
		struct Value { x : i32; y : f32; }
		fn main() : i32 {
			var value : Value = Value { 0, 2.0 };
			unroll for field in Value::fields {
				if field.type == i32 { value[field.name] = 7; }
			}
			return value.x;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(StructFieldIndexRejectsRuntimeString) {
	const char* source = R"(
		struct Value { x : i32; }
		fn main(name : []const u8) : void {
			var value : Value = undefined;
			value[name] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructFieldIndexRejectsNonStringComptimeValue) {
	const char* source = R"(
		struct Value { x : i32; }
		comptime field = 0;
		fn main() : void {
			var value : Value = undefined;
			value[field] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructFieldIndexRejectsMissingField) {
	const char* source = R"(
		struct Value { x : i32; }
		fn main() : void {
			var value : Value = undefined;
			value["missing"] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(StructFieldIndexRejectsRuntimeNonStruct) {
	const char* source = R"(
		fn main() : void {
			var value : i32 = 0;
			value["field"] = 1;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionKindMemberGuardRejectsWrongKind) {
	const char* source = R"(
		comptime bad = i32::fields;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionUnknownMemberRejected) {
	const char* source = R"(
		comptime bad = i32::MAX;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionParamsRuntimeUseRejected) {
	const char* source = R"(
		comptime F = fn(i32) : void;
		fn main() : void { for p in F::params { } }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionTypeValueRuntimeUseRejected) {
	const char* source = R"(
		comptime U = i32 | f32;
		fn main() : void { var value = U::types[0]; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionTypeValueCanBePassedToNative) {
	const char* source = R"(
		struct Settings { enabled : bool; }
		extern fn getEvoxData(t : type) : []const byte;
		fn main() : []const byte { return getEvoxData(Settings); }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionComptimeTypeCanBeForwardedToNative) {
	const char* source = R"(
		extern fn getEvoxDataRaw(t : type) : []const byte;
		fn getEvoxData(T : comptime type) : []T {
			var raw = getEvoxDataRaw(T);
			return raw as []T;
		}
		fn main() : []i32 { return getEvoxData(i32); }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionMultipleTypeValuesCanBePassedToNative) {
	const char* source = R"(
		extern fn sameType(a : type, b : type) : bool;
		fn main() : bool { return sameType(i32, f32); }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ExternFunctionComptimeParameterRejected) {
	const char* source = R"(
		extern fn invalid(T : comptime type) : void;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionStructValueIterationRejected) {
	const char* source = R"(
		struct S { x : i32; }
		fn main() : void {
			var value : S = S { 1 };
			unroll for f in value { f.value = 2; }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionEnumSequenceIndexAndMaterialization) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime values = State::values;
		comptime first = values[0];
		fn main() : i32 { return first.value as i32; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionUnionSequenceIndex) {
	const char* source = R"(
		comptime U = i32 | f32;
		comptime members = U::types;
		comptime First = members[0];
		fn main(value : First) : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionUninstantiatedTemplateTypeMember) {
	const char* source = R"(
		fn inspect(value : $T) : void {
			comptime fields = T::fields;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionUnknownKind) {
	const char* source = R"(
		fn inspect(T : comptime type) : void {
			var fields = T::fields;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionUnknownKindRejectsIncompatibleInstantiation) {
	const char* source = R"(
		enum State { Idle }
		fn inspect(T : comptime type) : void {
			comptime fields = T::fields;
		}
		fn main() : void { inspect(State); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionKindProvingBranchAllowsFields) {
	const char* source = R"(
		fn inspect(T : comptime type) : void {
			if T::kind == .Struct {
				unroll for f in T::fields { var name : []const u8 = f.name; }
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionSequenceLength) {
	const char* source = R"(
		comptime U = i32 | f32;
		comptime count = U::types.length;
		fn main() : i32 { return count; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionEmptySequenceLength) {
	const char* source = R"(
		comptime empty : []type = null;
		comptime count = empty.length;
		fn main() : i32 { return count; }
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(0, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(ComptimeTypeKindMatchPrunesUnselectedCase) {
	const char* source = R"(
		fn inspect(T : comptime type) : i32 {
			match T::kind {
				case .I32:
					return 1;
				case .Struct:
					var bad : MissingType = undefined;
				case:
					return 0;
			}
		}

		fn main() : i32 {
			return inspect(i32);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeofProducesExpressionType) {
	const char* source = R"(
		comptime Result = typeof((1 + 2) as i32);
		fn accept(value : Result) : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeofTypeMembersProducesExpressionTypes) {
	const char* source = R"(
		comptime Kind = typeof(i32::kind);
		comptime NestedKind = typeof(typeof(i32::kind));
		comptime Name = typeof(i32::name);
		comptime Child = typeof(?i32::child);
		comptime Length = typeof([3]i32::length as isize);

		fn acceptKind(value : Kind) : void {}
		fn acceptNestedKind(value : NestedKind) : void {}
		fn acceptName(value : Name) : void {}
		fn acceptChild(value : Child) : void {}
		fn acceptLength(value : Length) : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeofRejectsTypeOperand) {
	const char* source = R"(
		comptime T = typeof(i32);
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionTypeofRejectsUntypedNumerics) {
	EXPECT_COMPILE_FAIL(R"(
		comptime T = typeof(1 + 2);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime T = typeof(1.5);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1;
		comptime T = typeof(value);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime value = 1.5;
		comptime T = typeof(value);
	)");
	return true;
}

TEST(IntrospectionTypeofObservesNullablePromotion) {
	const char* source = R"(
		fn inspect(value : ?i32) : void {
			if value != null {
				comptime T = typeof(value);
				if T == i32 { } else { var bad : MissingType = undefined; }
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeofStaysComptimeOnly) {
	const char* source = R"(
		fn main() : void { var T = typeof(1 as i32); }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionKindClassifiesEveryTypeForm) {
	const char* source = R"(
		struct S { value : i32; }
		enum E { Value }
		comptime U = i32 | f32;
		comptime N = ?i32;
		comptime Sl = []i32;
		comptime Ar = [2]i32;
		comptime F = fn(i32) : void;
		comptime b = bool::kind;
		comptime n = N::kind;
		comptime s = Sl::kind;
		comptime a = Ar::kind;
		comptime e = E::kind;
		comptime r = S::kind;
		comptime u = U::kind;
		comptime f = F::kind;
		fn main() : void {
			if b == .Bool and n == .Nullable and s == .Slice and a == .Array and e == .Enum and r == .Struct and u == .Union and f == .Fn { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionParenthesizedCompoundTypeMemberRejected) {
	const char* source = R"(
		comptime name = (?i32)::name;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionKindStaysComptimeOnly) {
	const char* source = R"(
		fn main() : void { var kind = i32::kind; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionChildHandlesAllUnaryTypeConstructors) {
	const char* source = R"(
		comptime Nullable = ?i32;
		comptime Pointer = *i32;
		comptime Slice = []i32;
		comptime Array = [2]i32;
		comptime NullableChild = Nullable::child;
		comptime PointerChild = Pointer::child;
		comptime SliceChild = Slice::child;
		comptime ArrayChild = Array::child;
		fn nullable(value : NullableChild) : void {}
		fn pointer(value : PointerChild) : void {}
		fn slice(value : SliceChild) : void {}
		fn array(value : ArrayChild) : void {}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionPointerTypeKindAndName) {
	const char* source = R"(
		comptime Pointer = *i32;
		comptime NullablePointer = ?*i32;
		comptime pointer_kind = Pointer::kind;
		comptime nullable_pointer_kind = NullablePointer::kind;
		comptime pointer_name = Pointer::name;
		comptime nullable_pointer_name = NullablePointer::name;
		fn main() : void {
			if pointer_kind == .Pointer and nullable_pointer_kind == .Nullable
				and pointer_name == "*i32" and nullable_pointer_name == "?*i32" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionPointerValueUsesTypeof) {
	const char* source = R"(
		fn main() : void {
			var value : i32 = 0;
			var pointer : *i32 = &value;
			comptime Kind = typeof(pointer)::kind;
			comptime Child = typeof(pointer)::child;
			if Kind == .Pointer and Child == i32 { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionChildAndLengthRejectInvalidKinds) {
	const char* source = R"(
		comptime Slice = []i32;
		comptime child = i32::child;
		comptime count = Slice::length;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionArrayLengthIsComptimeInteger) {
	const char* source = R"(
		comptime Array = [3]i32;
		comptime count = Array::length;
		fn main() : i32 {
			unroll for i in 0..count { }
			return count;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNamesCoverConstructedTypes) {
	const char* source = R"(
		struct S { value : i32; }
		comptime Nullable = ?S;
		comptime Slice = []i32;
		comptime Array = [2]i32;
		comptime Function = fn(i32, f32) : bool;
		comptime nullable = Nullable::name;
		comptime slice = Slice::name;
		comptime array = Array::name;
		comptime function = Function::name;
		fn main() : void {
			if nullable == "?S" and slice == "[]i32" and array == "[2]i32" and function == "fn(i32, f32) : bool" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNameMaterializes) {
	const char* source = R"(
		comptime name = i32::name;
		fn main() : []const u8 { return name; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionEnumReflectionNamespaceIsSeparate) {
	const char* source = R"(
		enum State { kind, name, values }
		comptime reflected_kind = State::kind;
		comptime reflected_name = State::name;
		comptime reflected_values = State::values;
		fn main() : State { return State.values; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeEqualityUsesIdentity) {
	const char* source = R"(
		struct A { value : i32; }
		struct B { value : i32; }
		comptime U1 = i32 | f32;
		comptime U2 = f32 | i32;
		fn main() : void {
			if U1 == U2 and A != B { } else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionUnknownKindBranchGuardsChildAndRet) {
	const char* source = R"(
		fn inspect(T : comptime type) : void {
			match T::kind {
				case .Nullable: { comptime Child = T::child; }
				case .Fn: { comptime Result = T::ret; }
				case: { }
			}
		}
		fn main() : void { inspect(?i32); inspect(fn(i32) : void); }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionChildAndArrayLength) {
	const char* source = R"(
		comptime NullableChild = ?i32::child;
		comptime SliceChild = []f32::child;
		comptime ArrayChild = [3]i64::child;
		comptime ArrayLength = [3]i64::length;
		fn main() : ArrayChild { return undefined; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionEnumAndUnionValuesAreNotIterable) {
	const char* source = R"(
		enum E { A, B }
		comptime U = i32 | f32;
		fn main(e : E, u : U) : void {
			unroll for value in e { }
			unroll for value in u { }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionFunctionValueUsesTypeof) {
	const char* source = R"(
		fn callback(value : i32) : bool { return true; }
		comptime First = typeof(callback)::params[0].type;
		comptime Result = typeof(callback)::ret;
		fn accept(value : First) : Result { return true; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionEnumCursorNameMaterializes) {
	const char* source = R"(
		enum State { Idle, Running }
		comptime first = State::values[0];
		fn main() : []const u8 { return first.name; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionCursorAndFieldTypeStayComptimeOnly) {
	const char* source = R"(
		struct S { value : i32; }
		comptime field = S::fields[0];
		fn main() : void {
			var cursor = field;
			var T = field.type;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionValueFieldIterationRequiresMutableStorage) {
	const char* source = R"(
		struct S { value : i32; }
		fn main() : void {
			const value : S = S { 1 };
			unroll for field in value { field.value = 2; }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionTypeofDoesNotEvaluateOperand) {
	const char* source = R"(
		fn main() : void {
			var array : [1]i32 = [1];
			var empty : []i32 = array[0:0];
			comptime Element = typeof(empty[0]);
			var value : Element = 1;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeofObservesUnionPromotion) {
	const char* source = R"(
		comptime U = i32 | f32;
		fn inspect(value : U) : void {
			if value is i32 {
				comptime T = typeof(value);
				if T == i32 { } else { var bad : MissingType = undefined; }
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionArrayLengthMaterializes) {
	const char* source = R"(
		comptime Array = [3]i32;
		comptime count = Array::length;
		fn main() : isize { return count; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeParametersAreComptime) {
	const char* source = R"(
		fn inspect(T : comptime type) : void {
			comptime kind = T::kind;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionFieldAndEnumSequencesPreserveDeclarationOrder) {
	const char* source = R"(
		struct S { first : i32; second : f32; }
		enum E { First, Second }
		comptime field = S::fields[0];
		comptime value = E::values[0];
		fn main() : void {
			if field.name == "first" and value.name == "First" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionFunctionParametersPreserveDeclarationOrder) {
	const char* source = R"(
		comptime F = fn(i32, f32) : void;
		comptime first = F::params[0];
		comptime second = F::params[1];
		fn main() : void {
			if first.type == i32 and second.type == f32 { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}


TEST(IntrospectionCursorSequencesRequireInference) {
	const char* source = R"(
		struct S { value : i32; }
		comptime fields : []FieldCursor = S::fields;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionBareTypesAreNotIterable) {
	const char* source = R"(
		struct S { value : i32; }
		enum E { Value }
		comptime U = i32 | f32;
		fn main() : void {
			unroll for value in S { }
			unroll for value in E { }
			unroll for value in U { }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionFunctionReturnStaysComptimeOnly) {
	const char* source = R"(
		comptime F = fn(i32) : bool;
		fn main() : void { var result = F::ret; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionTypeKindCoversBuiltinDiscriminants) {
	const char* source = R"(
		fn main() : void {
			if bool::kind == .Bool and i8::kind == .I8 and i16::kind == .I16 and i32::kind == .I32 and i64::kind == .I64 and isize::kind == .ISize { }
			else { var bad : MissingType = undefined; }
			if u8::kind == .U8 and u16::kind == .U16 and u32::kind == .U32 and u64::kind == .U64 and byte::kind == .Byte { }
			else { var bad : MissingType = undefined; }
			if f32::kind == .F32 and f64::kind == .F64 and cstr::kind == .CStr and cptr::kind == .CPtr { }
			else { var bad : MissingType = undefined; }
			if void::kind == .Void and type::kind == .Type { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionNumericTypeMinMax) {
	const char* source = R"(
		comptime i8_min = i8::min;
		comptime i8_max = i8::max;
		comptime u8_min = u8::min;
		comptime u8_max = u8::max;
		comptime i32_min = i32::min;
		comptime i32_max = i32::max;
		comptime u32_min = u32::min;
		comptime u32_max = u32::max;
		comptime f32_min = f32::min;
		comptime f32_max = f32::max;
		comptime f64_min = f64::min;
		comptime f64_max = f64::max;
		fn main() : void {
			if i8_min == -128 and i8_max == 127 and u8_min == 0 and u8_max == 255
				and i32_min == -2147483648 and i32_max == 2147483647
				and u32_min == 0 and u32_max == 4294967295
				and f32_min < 0.0 and f32_max > 0.0 and f64_min < 0.0 and f64_max > 0.0 { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionNumericTypeMinMaxRejectsNonNumeric) {
	const char* source = R"(
		comptime value = bool::min;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionNumericTypeMinMaxAreUntyped) {
	EXPECT_COMPILE_FAIL(R"(
		comptime T = typeof(i32::min);
	)");
	EXPECT_COMPILE_FAIL(R"(
		comptime T = typeof(f64::max);
	)");
	return true;
}

TEST(IntrospectionNumericTypeBoundsFoldBeforeBytecode) {
	const char* source = R"(
		fn min_value() : i32 { return i32::min; }
		fn max_value() : u64 { return u64::max; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNamesCoverDeclarationsTemplatesAndUnions) {
	const char* source = R"FACTORY(
		struct S { value : i32; }
		enum E { Value }
		fn pair(T : comptime type) : type { return struct { first : T; second : T; }; }
		fn box(T : comptime type) : type { return struct { value : T; }; }
		fn static_array(T : comptime type, N : comptime i32) : type { return struct { values : [N]T; }; }
		comptime PairI32 = pair(i32);
		comptime Nested = box(pair(i32));
		comptime Array = static_array(i32, 4);
		comptime U = f32 | i32;
		fn main() : void {
			if i32::name == "i32" and S::name == "S" and E::name == "E" and PairI32::name == "pair(i32)" and Nested::name == "box(pair(i32))" and Array::name == "static_array(i32, 4)" and U::name == "f32 | i32" { }
			else { var bad : MissingType = undefined; }
		}
	)FACTORY";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionNamesSelectUnrolledBranchWithSliceEquality) {
	const char* source = R"(
		struct S { hp : i32; mana : i32; }

		fn main() : i32 {
			var v : S = undefined;
			v.hp = 1;
			v.mana = 2;
			var total : i32 = 0;
			unroll for f in S::fields {
				if f.name == "hp" { total += v[f.name]; }
				else { total += 10 * v[f.name]; }
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(21, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(IntrospectionUnrollSliceContinueSkipsField) {
	const char* source = R"(
		struct S { a : i32; b : i32; c : i32; d : i32; }
		fn main() : i32 {
			var v : S = S { 10, 20, 30, 40 };
			var total : i32 = 0;
			unroll for f in S::fields {
				if f.name == "b" { continue; }
				if f.name == "d" { break; }
				total += v[f.name];
			}
			return total;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ex_call(runtime, toLs("main")));
	EXPECT_EQ(40, ex_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(IntrospectionTypeNamesCoverAllPrimitiveTypes) {
	const char* source = R"(
		fn main() : void {
			if bool::name == "bool" and i8::name == "i8" and i16::name == "i16" and i32::name == "i32" and i64::name == "i64" and isize::name == "isize" { }
			else { var bad : MissingType = undefined; }
			if u8::name == "u8" and u16::name == "u16" and u32::name == "u32" and u64::name == "u64" and byte::name == "byte" { }
			else { var bad : MissingType = undefined; }
			if f32::name == "f32" and f64::name == "f64" and cstr::name == "cstr" and cptr::name == "cptr" { }
			else { var bad : MissingType = undefined; }
			if void::name == "void" and type::name == "type" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNamesCoverNestedTypes) {
	const char* source = R"(
		comptime SliceArray = [][4]i32;
		comptime ArraySlice = [2][]i32;
		comptime NullableSlice = ?[]i32;
		comptime NullableArray = ?[2]i32;
		fn main() : void {
			if SliceArray::name == "[][4]i32" and ArraySlice::name == "[2][]i32" { }
			else { var bad : MissingType = undefined; }
			if NullableSlice::name == "?[]i32" and NullableArray::name == "?[2]i32" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNamesCoverZeroParameterFunction) {
	const char* source = R"(
		comptime VoidFunction = fn() : void;
		comptime IntFunction = fn() : i32;
		fn main() : void {
			if VoidFunction::name == "fn() : void" and IntFunction::name == "fn() : i32" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNamesCanonicalizeUnionOrder) {
	const char* source = R"(
		comptime First = i32 | f32;
		comptime Second = f32 | i32;
		fn main() : void {
			if First::name == "i32 | f32" and Second::name == "i32 | f32" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionFieldAndEnumSequencesRejectRuntimeFor) {
	const char* source = R"(
		struct S { value : i32; }
		enum E { Value }
		fn main() : void {
			for field in S::fields { }
			for value in E::values { }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionStructValueIterationTypeCheckRejected) {
	const char* source = R"(
		struct S { integer : i32; fraction : f32; }
		fn main() : void {
			var value : S = S { 1, 2.0 };
			unroll for field in value {
				if field.type == typeof(field.value) { }
				else { var bad : MissingType = undefined; }
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionStructValueIterationPointerRejected) {
	const char* source = R"(
		struct S { value : i32; }
		fn increment(value : *i32) : void { value.* += 1; }
		fn main() : void {
			var object : S = S { 1 };
			unroll for field in object { increment(&field.value); }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionValueFieldTemporaryIsNotMutable) {
	const char* source = R"(
		struct S { value : i32; }
		fn make() : S { return S { 1 }; }
		fn main() : void {
			unroll for field in make() { field.value = 2; }
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionReflectionAndDeclaredEnumValuesRemainDistinct) {
	const char* source = R"(
		enum State { values, Other }
		comptime members = State::values;
		comptime first = members[0];
		fn main() : State {
			if first.value == State.values { return State.values; }
			else { return State.Other; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeEqualityPrunesUnselectedBranch) {
	const char* source = R"(
		fn inspect(value : $T) : void {
			if T == i32 { }
			else { var bad : MissingType = undefined; }
		}
		fn main() : void { inspect(1); }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionUnrollSequenceIndicesAreComptimeValues) {
	const char* source = R"(
		comptime U = i32 | f32;
		fn main() : void {
			unroll for index, T in U::types {
				if index == 0 or index == 1 { }
				else { var bad : MissingType = undefined; }
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionFieldCursorNameMaterializes) {
	const char* source = R"(
		struct S { value : i32; }
		comptime field = S::fields[0];
		fn main() : []const u8 { return field.name; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionUnrollControlFlowWorksForReflectionSequences) {
	const char* source = R"(
		comptime U = i32 | f32;
		fn main() : void {
			unroll for index, T in U::types {
				if index == 0 { continue; }
				break;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

// The generic `print` from the "Compile-time introspection" section of
// reference.md: one `$T` template that dispatches on `T::kind` and walks
// aggregates with `unroll for`. The externs are wired to a capture buffer so
// each kind's rendering is checked exactly, not just that it compiles.
struct PrintCapture {
	char buffer[512];
	u32 size = 0;

	void append(const char* begin, const char* end) {
		for (const char* c = begin; c != end && size < sizeof(buffer); ++c) buffer[size++] = *c;
	}
	void reset() { size = 0; }
	bool equals(const char* expected) const {
		const u32 length = (u32)strlen(expected);
		return length == size && compareMemory(buffer, expected, length) == 0;
	}
};

static PrintCapture g_print_capture;

static void printCaptureBytes(ex_runtime*, ex_call_frame frame) {
	EX_STRING_ARG(frame, text);
	g_print_capture.append(text.begin, text.begin + text.length);
}

static void printCaptureI64(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, i64, value);
	char digits[32];
	toCString(value, digits, sizeof(digits));
	g_print_capture.append(digits, digits + strlen(digits));
}

static void printCaptureU64(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, u64, value);
	char digits[32];
	toCString((i64)value, digits, sizeof(digits));
	g_print_capture.append(digits, digits + strlen(digits));
}

static void printCaptureF64(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, double, value);
	char digits[64];
	const int written = snprintf(digits, sizeof(digits), "%g", value);
	g_print_capture.append(digits, digits + written);
}

static void printCaptureBool(ex_runtime*, ex_call_frame frame) {
	EX_ARG(frame, bool, value);
	const char* text = value ? "true" : "false";
	g_print_capture.append(text, text + strlen(text));
}

TEST(IntrospectionGenericPrintRendersEveryKind) {
	const char* source = R"(
		extern fn write_bytes(text : []const u8) : void;
		extern fn write_i64(value : i64) : void;
		extern fn write_u64(value : u64) : void;
		extern fn write_f64(value : f64) : void;
		extern fn write_bool(value : bool) : void;

		fn print(v : $T) : void {
			if T == []const u8 {
				write_bytes(v);
				return;
			}
			match T::kind {
				case .Bool:                          write_bool(v);
				case .F32, .F64:                     write_f64(v as f64);
				case .I8, .I16, .I32, .I64, .ISize:  write_i64(v as i64);
				case .U8, .U16, .U32, .U64:          write_u64(v as u64);
				case .Nullable:
					if v != null {
						print(v);
					} else {
						write_bytes("null");
					}

				case .Slice, .Array:
					write_bytes("[");
					for i in 0..v.length {
						if i > 0 { write_bytes(", "); }
						print(v[i]);
					}
					write_bytes("]");

				case .Enum:
					unroll for e in T::values {
						if v == e.value {
							write_bytes(e.name);
							return;
						}
					}
					write_bytes("<invalid ");
					write_i64(v as i64);
					write_bytes(">");

				case .Struct:
					write_bytes(T::name);
					write_bytes(" { ");
					unroll for i, f in T::fields {
						if i > 0 { write_bytes(", "); }
						write_bytes(f.name);
						write_bytes(" = ");
						print(v[f.name]);
					}
					write_bytes(" }");

				case .Union:
					unroll for M in T::types {
						if v is M {
							print(v);
							return;
						}
					}

				case:
					write_bytes("<");
					write_bytes(T::name);
					write_bytes(">");
			}
		}

		struct Vec3 { x : f32; y : f32; z : f32; }
		enum State { Idle, Running, Done }

		fn print_bool() : void { print(true); }
		fn print_int() : void { print(-7); }
		fn print_uint() : void { var v : u32 = 9; print(v); }
		fn print_float() : void { print(1.5); }
		fn print_string() : void { print("hi"); }
		fn print_array() : void { var v : [3]i32 = [ 1, 2, 3 ]; print(v); }
		fn print_struct() : void { print(Vec3 { 1.0, 2.0, 3.0 }); }
		fn print_enum() : void { print(State.Running); }
		fn print_invalid_enum() : void { var v : State = 7 as State; print(v); }
		fn print_nested() : void { var v : [2]Vec3 = [ Vec3 { 1.0, 0.0, 0.0 }, Vec3 { 0.0, 1.0, 0.0 } ]; print(v); }

		fn print_nullable() : void { var v : ?i32 = 5; print(v); }
		fn print_null() : void { var v : ?i32 = null; print(v); }
		fn print_union() : void { var v : i32 | f32 = 3; print(v); }
		fn print_void_kind() : void { print(print_bool); }
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ex_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_bytes"), &printCaptureBytes) == EX_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_i64"), &printCaptureI64) == EX_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_u64"), &printCaptureU64) == EX_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_f64"), &printCaptureF64) == EX_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_bool"), &printCaptureBool) == EX_RESULT_OK);

	struct Case {
		const char* function;
		const char* expected;
	};
	const Case cases[] = {
		{ "print_bool",          "true" },
		{ "print_int",           "-7" },
		{ "print_uint",          "9" },
		{ "print_float",         "1.5" },
		{ "print_string",        "hi" },
		{ "print_array",         "[1, 2, 3]" },
		{ "print_struct",        "Vec3 { x = 1, y = 2, z = 3 }" },
		{ "print_enum",          "Running" },
		{ "print_invalid_enum",  "<invalid 7>" },
		{ "print_nested",        "[Vec3 { x = 1, y = 0, z = 0 }, Vec3 { x = 0, y = 1, z = 0 }]" },
		{ "print_nullable",      "5" },
		{ "print_null",          "null" },
		{ "print_union",         "3" },
		// .Fn has no printable byte representation and falls through to `case:`
		{ "print_void_kind",     "<fn() : void>" },
	};

	for (const Case& test_case : cases) {
		g_print_capture.reset();
		EXPECT_TRUE(ex_call(runtime, toLs(test_case.function)) == EX_RESULT_OK);
		if (!g_print_capture.equals(test_case.expected)) {
			printf("TEST FAILED at %s:%d: %s printed \"%.*s\", expected \"%s\"\n",
				__FILE__, __LINE__, test_case.function,
				(int)g_print_capture.size, g_print_capture.buffer, test_case.expected);
			CAPI_END(module);
			return false;
		}
	}

	CAPI_END(module);
	return true;
}
