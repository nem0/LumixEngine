TEST(UnionBasicConstruction) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			var a : A = A { 10 };
			u = a;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionMemberToUnion) {
	const char* source = R"(
		struct ButtonEvent {
			button : i32;
		}
		struct MouseMoveEvent {
			x : i32;
			y : i32;
		}
		comptime InputEvent = ButtonEvent | MouseMoveEvent;

		fn main() : void {
			var b : ButtonEvent = ButtonEvent { 1 };
			var e : InputEvent = b;
			e = MouseMoveEvent { 0, 1 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionAnonymousType) {
	const char* source = R"(
		struct Error {
			code : i32;
		}
		struct ASTNode {
			id : i32;
		}

		fn parse() : Error | ASTNode {
			return Error { -1 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionExhaustiveMatch) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			match u {
				case A:
					var x : i32 = u.x;
				case B:
					var y : f32 = u.y;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionMatchImportedMemberWithoutNamespace) {
	const char* source = R"(
		import "events" as ns

		comptime Event = ns.ButtonEvent | ns.AxisEvent;

		fn main() : void {
			var event : Event = ns.ButtonEvent { 1 };
			match event {
				case ButtonEvent:
					var button : i32 = event.button;
				case AxisEvent:
					var axis : i32 = event.axis;
			}
		}
	)";
	const char* events_source = R"(
		struct ButtonEvent {
			button : i32;
		}

		struct AxisEvent {
			axis : i32;
		}
	)";
	LumScriptImportFile file = { toLs("events"), toLs(events_source) };
	LumScriptImportFiles files = { &file, 1 };
	EXPECT_COMPILE_WITH_IMPORTS(source, files);
	return true;
}

TEST(UnionMatchWithFallback) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			match u {
				case A:
					var x : i32 = u.x;
				case:
					var y : f32 = u.y;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionMatchNarrowing) {
	const char* source = R"(
		struct ButtonEvent {
			button : i32;
		}
		struct MouseMoveEvent {
			x : i32;
			y : i32;
		}
		comptime InputEvent = ButtonEvent | MouseMoveEvent;

		fn main() : void {
			var e : InputEvent = ButtonEvent { 1 };
			match e {
				case ButtonEvent:
					var b : i32 = e.button;
				case MouseMoveEvent:
					var x : i32 = e.x;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeBindingDoesNotUnboxNullable) {
	const char* source = R"(
		comptime n : ?i32 = 1;
		fn main() : i32 { return n; }
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(ComptimeUnionBindingKeepsDeclaredType) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : f32; }
		comptime Union = A | B;
		comptime u : Union = A { 5 };
		comptime is_a = u is A;

		fn main() : void {
			if is_a { } else { var bad : MissingType = undefined; }
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeUnionSubsetWidening) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		struct C { z : i32; }
		comptime Small = A | B;
		comptime Large = A | B | C;
		comptime small : Small = A { 5 };
		comptime large : Large = small;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(ComptimeReorderedUnionPreservesMemberTag) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		comptime AB = A | B;
		comptime BA = B | A;
		comptime source : AB = A { 7 };
		comptime same_first_member = AB::types[0] == BA::types[0];

		fn main() : i32 {
			if not same_first_member { return -2; }
			comptime reordered : BA = source;
			var runtime : BA = reordered;
			match runtime {
				case A:
					return runtime.x;
				case B:
					return -1;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionIsOperator) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			if u is A {
				var x : i32 = u.x;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionMemberCastError) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			var maybe_a : ?A = u as A;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionSubsetWidening) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		struct C {
			z : bool;
		}
		comptime Small = A | B;
		comptime Large = A | B | C;

		var small : Small = A { 5 };
		var large : Large = small;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionFlattening) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		struct C {
			z : bool;
		}
		comptime AB = A | B;
		comptime BC = B | C;
		comptime ABC = AB | BC;

		var value : ABC = A { 1 };
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionIdentityUnordered) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union1 = A | B;
		comptime Union2 = B | A;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionDuplicateMemberFlattening) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct C {
			y : i32;
		}
		comptime Direct = A | A;
		comptime AC = A | C;
		comptime Nested = A | AC;

		var direct : Direct = A { 1 };
		var nested : Nested = C { 2 };
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionTypelessLiteralError) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		var u : Union = { 5 };
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionNullMemberError) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		comptime Union = A | null;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionNullableUnion) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime OptionalUnion = ?(A | B);

		var u : OptionalUnion = null;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionNullableMemberPrecedence) {
	const char* source = R"(
		comptime Parsed = ?i32 | []const u8;
		comptime Expected = (?i32) | []const u8;

		fn main() : void {
			var parsed : Parsed = "text";
			var expected : Expected = parsed;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionUndefinedInit) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		var u : Union = undefined;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionNoEquality) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u1 : Union = A { 5 };
			var u2 : Union = A { 5 };
			if u1 == u2 {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionMatchCommaAlternative) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			match u {
				case A, B:
					var v : i32 = 0;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionMatchDuplicateCaseError) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			match u {
				case A:
					var v : i32 = 1;
				case A:
					var w : i32 = 2;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionMutableNarrowing) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			match u {
				case A:
					u.x = 10;
					u = B { 3.14 };
				case B:
					var v : f32 = u.y;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionPrimitiveMembers) {
	const char* source = R"(
		comptime IntOrFloat = i32 | f32;

		fn main() : void {
			var u : IntOrFloat = 42;
			u = 3.14;

			match u {
				case i32:
					var x : i32 = 0;
				case f32:
					var y : f32 = 0.0;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionWithLeadingSliceMember) {
	// `[]const u8 | i32` must be a union of a slice and i32, not a slice of `u8 | i32`
	const char* source = R"(
		comptime StringOrInt = []const u8 | i32;
		comptime ArrayOrInt = [2]u8 | i32;

		fn main() : void {
			var u : StringOrInt = 42;
			u = "hello";

			match u {
				case []const u8:
					var s : []const u8 = "";
				case i32:
					var x : i32 = 0;
			}

			var a : ArrayOrInt = 42;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionTypeExpressionOutsideComptimeFails) {
	const char* source = R"(
		fn main() : void {
			var type_value = i32 | f32;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionTypeExpressionGlobalOutsideComptimeFails) {
	const char* source = R"(
		const Union = i32 | f32;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionErrorPropagation) {
	const char* source = R"(
		struct ParseError {
			line : i32;
		}
		struct ASTNode {
			id : i32;
		}
		struct IOError {
			errno : i32;
		}

		fn parse() : ParseError | ASTNode {
			return ParseError { 1 };
		}

		fn readAndParse() : ParseError | IOError | ASTNode {
			var result : ParseError | ASTNode = parse();
			return result;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionMatchNotExhaustive) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			match u {
				case A:
					var x : i32 = u.x;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionSizeof) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
			z : f32;
		}
		comptime Union = A | B;

		fn main() : i32 {
			return sizeof(Union);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionAlignof) {
	const char* source = R"(
		comptime Union = i32 | f32;

		fn main() : i32 {
			return alignof(Union);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(4, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionNarrowedValueRuntime) {
	const char* source = R"(
		comptime Union = i32 | i64;

		fn main() : i32 {
			var value : Union = 42 as i32;
			match value {
				case i32:
					var extracted : i32 = value;
					return extracted;
				case i64:
					return 0;
			}
			return 0;
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

TEST(UnionIsRuntime) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : i32;
		}
		comptime Union = A | B;

		fn main() : i32 {
			var value : Union = A { 7 };
			var result : i32 = 0;
			if value is B {
				return 0;
			}
			result = value.x;
			return result + 1;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(8, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionMatchDispatchRuntime) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : i32;
		}
		struct C {
			z : i32;
		}
		comptime Union = A | B | C;

		fn classify(value : Union) : i32 {
			match value {
				case A:
					return value.x;
				case B, C:
					return 10;
			}
			return 0;
		}

		fn fallback(value : Union) : i32 {
			match value {
				case A:
					return 1;
				case:
					return 2;
			}
			return 0;
		}

		fn main() : i32 {
			return classify(A { 7 }) + classify(B { 0 }) + classify(C { 0 }) + fallback(A { 0 }) + fallback(C { 0 });
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(30, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionWideningAcrossCallRuntime) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : i32;
		}
		struct C {
			z : i32;
		}
		comptime AB = A | B;
		comptime BAC = B | A | C;

		fn widen(value : AB) : BAC {
			return value;
		}

		fn read(value : BAC) : i32 {
			match value {
				case A:
					return value.x;
				case B:
					return value.y;
				case C:
					return value.z;
			}
			return 0;
		}

		fn main() : i32 {
			var value : AB = A { 9 };
			return read(widen(value));
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(9, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionFieldGlobalAndPointerRuntime) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : i32;
		}
		comptime Union = A | B;

		struct Container {
			value : Union;
		}

		var global : Container = Container { A { 3 } };

		fn set(value : *Union) : void {
			value.* = B { 8 };
		}

		fn read(value : *Union) : i32 {
			if value.* is B {
				return value.*.y;
			}
			return 0;
		}

		fn main() : i32 {
			set(&global.value);
			return read(&global.value);
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(8, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionEnumMembers) {
	const char* source = R"(
		enum Color {
			Red,
			Green,
			Blue
		}
		struct Point {
			x : i32;
			y : i32;
		}
		comptime ColorOrPoint = Color | Point;

		fn main() : void {
			var u : ColorOrPoint = Color.Red;
			u = Point { 1, 2 };

			match u {
				case Color:
					var c : Color = u;
				case Point:
					var p : Point = u;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionStringMembers) {
	const char* source = R"(
		comptime StringOrInt = []const u8 | i32;

		fn main() : void {
			var u : StringOrInt = "hello";
			u = 42;

			match u {
				case []const u8:
					var s : []const u8 = u;
				case i32:
					var i : i32 = u;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionAllConcreteRuntimeMemberKinds) {
	const char* source = R"(
		enum Kind {
			First,
			Second
		}
		struct Record {
			value : i32;
		}

		comptime AllKinds = byte | bool | i8 | u8 | i16 | u16 | i32 | u32 | i64 | u64 | isize | f32 | f64 | []const u8 | cstr | cptr | []i32 | []f32 | [3]i32 | [4]i32 | (fn(u8) : void) | Kind | Record;

		fn main() : void {
			var value : AllKinds = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionStructTemplateMember) {
	const char* source = R"(
		fn box(T : type) : type {
			return struct { value : T; };
		}

		comptime I32Box = box(i32);
		comptime F32Box = box(f32);
		comptime Boxes = I32Box | F32Box;

		fn main() : void {
			var value : Boxes = I32Box { 42 };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionGenericTypeMember) {
	const char* source = R"(
		comptime make = fn(value : $T) : i32 | T {
			return value;
		};

		fn main() : void {
			var value : i32 | f32 = make(1.5 as f32);
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionSliceMembers) {
	const char* source = R"(
		comptime Slices = []i32 | []f32;

		fn main() : void {
			var value : Slices = undefined;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionRecursiveStructMemberError) {
	const char* source = R"(
		struct Node {
			child : Node | i32;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionIsNonMemberError) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		struct C {
			z : bool;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			if u is C {
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionMatchNonMemberCaseError) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		struct C {
			z : bool;
		}
		comptime Union = A | B;

		fn main() : void {
			var u : Union = A { 5 };
			match u {
				case A:
					var x : i32 = u.x;
				case C:
					var z : bool = u.z;
			}
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionInStructField) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}

		struct Container {
			value : A | B;
		}

		var c : Container = Container { A { 5 } };
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionInFunctionParameter) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}

		fn process(u : A | B) : void {
			match u {
				case A:
					var x : i32 = u.x;
				case B:
					var y : f32 = u.y;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionVoidMemberError) {
	const char* source = R"(
		comptime Union = void | i32;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionNestedUnionFlattens) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		struct C {
			z : bool;
		}
		comptime AB = A | B;
		comptime ABC = AB | C;
		comptime Direct = A | B | C;

		var u : ABC = A { 5 };
		var v : Direct = u;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionComposedDuplicateFlattens) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : f32;
		}
		struct C {
			z : bool;
		}
		comptime AB = A | B;
		comptime BC = B | C;
		comptime ABC = AB | BC;
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionElseReturnExtractsMemberRuntime) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }

		fn choose(flag : bool) : A | B {
			if flag { return A { 7 }; }
			return B { 9 };
		}

		fn take(flag : bool) : B {
			var a : A = choose(flag) else return;
			return B { a.x };
		}

		fn main() : i32 {
			return take(true).y + take(false).y;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(16, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionElseReturnExtractsSubunion) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		struct C { z : i32; }
		comptime U = A | B | C;

		fn take(value : U) : A {
			var rest : B | C = value else return;
			match rest {
				case B: return A { rest.y };
				case C: return A { rest.z };
			}
		}

		fn main() : i32 {
			return take(B { 3 }).x + take(C { 4 }).x + take(A { 5 }).x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(12, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionElseReturnRejectsNonSubsetTarget) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		struct C { z : i32; }
		fn main(value : A | B) : void {
			var c : C = value else return;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionElseReturnRejectsWholeUnionTarget) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		comptime U = A | B;
		fn main(value : U) : void {
			var same : U = value else return;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionElseReturnRejectsNonUnionInitializer) {
	const char* source = R"(
		struct A { x : i32; }
		fn main() : void {
			var a : A = A { 1 } else return;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionIfElseResidualNarrowing) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		fn main(value : A | B) : i32 {
			if value is A {
				return value.x;
			} else {
				return value.y;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionEarlyReturnResidualNarrowing) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		fn main(value : A | B) : i32 {
			if value is A { return value.x; }
			return value.y;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionElseReturnEvaluatesExpressionOnce) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		var calls : i32 = 0;
		fn make() : A | B {
			calls += 1;
			return A { 6 };
		}
		fn main() : i32 {
			var a : A = make() else return;
			return calls + a.x;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(7, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionElseReturnRunsDeferOnFailure) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		var cleaned : i32 = 0;
		fn cleanup() : void { cleaned = 1; }
		fn make() : A | B { return B { 4 }; }
		fn take() : B {
			defer cleanup();
			var a : A = make() else return;
			return B { a.x };
		}
		fn main() : i32 {
			take();
		return cleaned;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionElseReturnResidualReturnWidening) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		struct C { z : i32; }
		fn make() : A | B | C { return B { 2 }; }
		fn take() : B | C {
			var a : A = make() else return;
			return C { a.x };
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionElseReturnResidualReturnWideningRuns) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		struct C { z : i32; }
		fn make() : A | B | C { return B { 2 }; }
		fn take() : B | C {
			var a : A = make() else return;
			return C { a.x };
		}
		fn main() : i32 {
			var value : B | C = take();
			if value is B { return value.y; }
			return value.z;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(2, ls_to_i32(runtime, -1));
	CAPI_END(module);
	return true;
}

TEST(UnionElseReturnRejectsIncompatibleResidualReturn) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		struct C { z : i32; }
		fn make() : A | B { return B { 2 }; }
		fn take() : C {
			var a : A = make() else return;
			return C { a.x };
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionNarrowingAfterContinue) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		fn main(value : A | B) : i32 {
			while true {
				if value is A { continue; }
				return value.y;
			}
			return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionNarrowingAfterBreak) {
	const char* source = R"(
		struct A { x : i32; }
		struct B { y : i32; }
		fn main(value : A | B) : i32 {
			while true {
				if value is A { break; }
				return value.y;
			}
		return 0;
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionNotBindsLooserThanIs) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : i32;
		}
		fn main() : i32 {
			var e : A | B = B { 7 };
			// not (e is A) is true; (not e) is A would not compile
			return not e is A ? 1 : 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(1, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(UnionNotBindsLooserThanIsMatchingVariant) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : i32;
		}
		fn main() : i32 {
			var e : A | B = B { 7 };
			// not (e is B) is false - the active variant matches
			return not e is B ? 1 : 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}

TEST(UnionNotIsOnBoolSubjectError) {
	const char* source = R"(
		struct B {
			y : i32;
		}
		fn main() : i32 {
			var e : bool = true;
			// is requires a union subject, so this fails either way it groups
			return not e is B ? 1 : 0;
		}
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionNotIsAcrossAnd) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		struct B {
			y : i32;
		}
		fn main() : i32 {
			var e : A | B = A { 1 };
			var f : A | B = A { 2 };
			// (not (e is A)) and (not (f is B)) is false and true -> false
			return not e is A and not f is B ? 1 : 0;
		}
	)";

	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));

	ls_bytecode* bytecode = ls_bytecode_compile(module, &module_host);
	EXPECT_TRUE(bytecode != nullptr);

	ls_runtime* runtime = ls_runtime_create(bytecode, nullptr);
	EXPECT_TRUE(runtime != nullptr);

	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(0, ls_to_i32(runtime, -1));

	ls_runtime_destroy(runtime);
	ls_bytecode_destroy(bytecode);
	CAPI_END(module);
	return true;
}
