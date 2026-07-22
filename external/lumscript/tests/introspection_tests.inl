TEST(FunctionTypeIntrospectionParamsAndReturn) {
	const char* source = R"(
		comptime Handler = fn(i32, f32) : bool;
		comptime params = Handler::params;
		comptime result = Handler::ret;
		comptime count = length(params);

		fn main() : i32 {
			return count;
		}
	)";
	EXPECT_COMPILE(source);
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
		comptime params : []type = Handler::params;
		comptime First = params[0];
		fn main(value : First) : void {}
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
				if field.type == i32 { var name : string = field.name; }
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnrollForStructValueFields) {
	const char* source = R"(
		struct Value { i : i32; f : f32; }
		fn main() : i32 {
			var value : Value = Value { 1, 2.0 };
			unroll for field in value { if field.type == i32 { field.value = 7; } }
			return value.i;
		}
	)";
	EXPECT_COMPILE(source);
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

TEST(UnrollForOperandEvaluatedBeforeBinding) {
	const char* source = R"(
		struct Value { x : i32; }
		fn main() : i32 {
			var value : Value = Value { 3 };
			unroll for value in value { if value.type == i32 { return value.value; } }
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionStructFieldsCanBeIndexed) {
	const char* source = R"(
		struct S { x : i32; y : f32; }
		comptime fields = S::fields;
		comptime first = fields[0];
		fn main() : void { var name : string = first.name; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionKindMemberGuardRejectsWrongKind) {
	const char* source = R"(
		comptime bad = i32::fields;
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

TEST(IntrospectionFieldCursorValueOnlyOnValueForm) {
	const char* source = R"(
		struct S { x : i32; }
		fn main() : void {
			var value : S = S { 1 };
			unroll for f in value { f.value = 2; }
		}
	)";
	EXPECT_COMPILE(source);
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

TEST(IntrospectionUnknownKindGuardRejectsFields) {
	const char* source = R"(
		fn inspect(T : comptime type) : void {
			var fields = T::fields;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionKindProvingBranchAllowsFields) {
	const char* source = R"(
		fn inspect(T : comptime type) : void {
			if T::kind == .Struct {
				unroll for f in T::fields { var name : string = f.name; }
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionSequenceLength) {
	const char* source = R"(
		comptime U = i32 | f32;
		comptime count = length(U::types);
		fn main() : i32 { return count; }
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionEmptySequenceLength) {
	const char* source = R"(
		comptime empty : []type = undefined;
		comptime count = length(empty);
		fn main() : i32 { return count; }
	)";
	EXPECT_COMPILE(source);
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
		comptime Result = typeof(1 + 2);
		fn accept(value : Result) : void {}
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
		fn main() : void { var T = typeof(1); }
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
			if b == .Bool && n == .Nullable && s == .Slice && a == .Array && e == .Enum && r == .Struct && u == .Union && f == .Fn { }
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
		comptime Slice = []i32;
		comptime Array = [2]i32;
		comptime NullableChild = Nullable::child;
		comptime SliceChild = Slice::child;
		comptime ArrayChild = Array::child;
		fn nullable(value : NullableChild) : void {}
		fn slice(value : SliceChild) : void {}
		fn array(value : ArrayChild) : void {}
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
			if nullable == "?S" && slice == "[]i32" && array == "[2]i32" && function == "fn(i32, f32) : bool" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNameMaterializes) {
	const char* source = R"(
		comptime name = i32::name;
		fn main() : string { return name; }
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
			if U1 == U2 && A != B { } else { var bad : MissingType = undefined; }
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
		comptime First = typeof(callback)::params[0];
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
		fn main() : string { return first.name; }
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

TEST(IntrospectionTypeMembersRequireComptimeType) {
	const char* source = R"(
		fn inspect(T : type) : void {
			comptime kind = T::kind;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(IntrospectionFieldAndEnumSequencesPreserveDeclarationOrder) {
	const char* source = R"(
		struct S { first : i32; second : f32; }
		enum E { First, Second }
		comptime field = S::fields[0];
		comptime value = E::values[0];
		fn main() : void {
			if field.name == "first" && value.name == "First" { }
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
			if first == i32 && second == f32 { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionBoundSequenceEscapesKindProvingBranch) {
	const char* source = R"(
		fn inspect(T : comptime type) : void {
			comptime fields = undefined;
			if T::kind == .Struct { fields = T::fields; }
			unroll for field in fields { var name : string = field.name; }
		}
		struct S { value : i32; }
		fn main() : void { inspect(S); }
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
			if bool::kind == .Bool && i8::kind == .I8 && i16::kind == .I16 && i32::kind == .I32 && i64::kind == .I64 && isize::kind == .ISize { }
			else { var bad : MissingType = undefined; }
			if u8::kind == .U8 && u16::kind == .U16 && u32::kind == .U32 && u64::kind == .U64 && byte::kind == .Byte { }
			else { var bad : MissingType = undefined; }
			if f32::kind == .F32 && f64::kind == .F64 && string::kind == .String && cstr::kind == .CStr && cptr::kind == .CPtr { }
			else { var bad : MissingType = undefined; }
			if void::kind == .Void && type::kind == .Type { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNamesCoverDeclarationsTemplatesAndUnions) {
	const char* source = R"(
		struct S { value : i32; }
		enum E { Value }
		struct Pair[T] { first : T; second : T; }
		comptime U = f32 | i32;
		fn main() : void {
			if i32::name == "i32" && S::name == "S" && E::name == "E" && Pair[i32]::name == "Pair[i32]" && U::name == "i32 | f32" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionTypeNamesCoverAllPrimitiveTypes) {
	const char* source = R"(
		fn main() : void {
			if bool::name == "bool" && i8::name == "i8" && i16::name == "i16" && i32::name == "i32" && i64::name == "i64" && isize::name == "isize" { }
			else { var bad : MissingType = undefined; }
			if u8::name == "u8" && u16::name == "u16" && u32::name == "u32" && u64::name == "u64" && byte::name == "byte" { }
			else { var bad : MissingType = undefined; }
			if f32::name == "f32" && f64::name == "f64" && string::name == "string" && cstr::name == "cstr" && cptr::name == "cptr" { }
			else { var bad : MissingType = undefined; }
			if void::name == "void" && type::name == "type" { }
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
			if SliceArray::name == "[][4]i32" && ArraySlice::name == "[2][]i32" { }
			else { var bad : MissingType = undefined; }
			if NullableSlice::name == "?[]i32" && NullableArray::name == "?[2]i32" { }
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
			if VoidFunction::name == "fn() : void" && IntFunction::name == "fn() : i32" { }
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
			if First::name == "i32 | f32" && Second::name == "i32 | f32" { }
			else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionUninstantiatedTemplateTypeMemberRejected) {
	const char* source = R"(
		fn inspect(value : $T) : void {
			comptime fields = T::fields;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
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

TEST(IntrospectionFieldTypeEqualsValueType) {
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
	EXPECT_COMPILE(source);
	return true;
}

TEST(IntrospectionValueFieldCanBePassedByReference) {
	const char* source = R"(
		struct S { value : i32; }
		fn increment(value : ref i32) : void { value += 1; }
		fn main() : void {
			var object : S = S { 1 };
			unroll for field in object { increment(ref field.value); }
		}
	)";
	EXPECT_COMPILE(source);
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
				if index == 0 || index == 1 { }
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
		fn main() : string { return field.name; }
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

static void printCaptureBytes(ls_runtime*, ls_call_frame frame) {
	LS_STRING_ARG(frame, text);
	g_print_capture.append(text.begin, text.end);
}

static void printCaptureI64(ls_runtime*, ls_call_frame frame) {
	LS_ARG(frame, i64, value);
	char digits[32];
	toCString(value, digits, sizeof(digits));
	g_print_capture.append(digits, digits + strlen(digits));
}

static void printCaptureU64(ls_runtime*, ls_call_frame frame) {
	LS_ARG(frame, u64, value);
	char digits[32];
	toCString((i64)value, digits, sizeof(digits));
	g_print_capture.append(digits, digits + strlen(digits));
}

static void printCaptureF64(ls_runtime*, ls_call_frame frame) {
	LS_ARG(frame, double, value);
	char digits[64];
	const int written = snprintf(digits, sizeof(digits), "%g", value);
	g_print_capture.append(digits, digits + written);
}

static void printCaptureBool(ls_runtime*, ls_call_frame frame) {
	LS_ARG(frame, bool, value);
	const char* text = value ? "true" : "false";
	g_print_capture.append(text, text + strlen(text));
}

TEST(IntrospectionGenericPrintRendersEveryKind) {
	const char* source = R"(
		extern fn write_bytes(text : string) : void;
		extern fn write_i64(value : i64) : void;
		extern fn write_u64(value : u64) : void;
		extern fn write_f64(value : f64) : void;
		extern fn write_bool(value : bool) : void;

		fn print(v : $T) : void {
			match T::kind {
				case .Bool:                          write_bool(v);
				case .F32, .F64:                     write_f64(v as f64);
				case .I8, .I16, .I32, .I64, .ISize:  write_i64(v as i64);
				case .U8, .U16, .U32, .U64:          write_u64(v as u64);
				case .String:                        write_bytes(v);

				case .Nullable:
					if v != null {
						print(v);
					} else {
						write_bytes("null");
					}

				case .Slice, .Array:
					write_bytes("[");
					for i in 0..length(v) {
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
					unroll for i, f in v {
						if i > 0 { write_bytes(", "); }
						write_bytes(f.name);
						write_bytes(" = ");
						print(f.value);
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
		fn print_array() : void { var v : [3]i32 = { 1, 2, 3 }; print(v); }
		fn print_struct() : void { print(Vec3 { 1.0, 2.0, 3.0 }); }
		fn print_enum() : void { print(State.Running); }
		fn print_invalid_enum() : void { var v : State = 7 as State; print(v); }
		fn print_nested() : void { var v : [2]Vec3 = { Vec3 { 1.0, 0.0, 0.0 }, Vec3 { 0.0, 1.0, 0.0 } }; print(v); }

		fn print_nullable() : void { var v : ?i32 = 5; print(v); }
		fn print_null() : void { var v : ?i32 = null; print(v); }
		fn print_union() : void { var v : i32 | f32 = 3; print(v); }
		fn print_void_kind() : void { print(print_bool); }
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);

	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_bytes"), &printCaptureBytes) == LS_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_i64"), &printCaptureI64) == LS_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_u64"), &printCaptureU64) == LS_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_f64"), &printCaptureF64) == LS_RESULT_OK);
	EXPECT_TRUE(setNativeFunctionCallback(runtime, module, toLs("write_bool"), &printCaptureBool) == LS_RESULT_OK);

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
		EXPECT_TRUE(ls_call(runtime, toLs(test_case.function)) == LS_RESULT_OK);
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
