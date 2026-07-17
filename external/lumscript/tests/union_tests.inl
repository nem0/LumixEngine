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

TEST(UnionMatchWithElse) {
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
				else:
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

TEST(UnionAsOperator) {
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
			if maybe_a != null {
				var x : i32 = maybe_a.x;
			}
		}
	)";
	EXPECT_COMPILE(source);
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
		comptime ABC = AB | C;
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

TEST(UnionDuplicateMemberError) {
	const char* source = R"(
		struct A {
			x : i32;
		}
		comptime Union = A | A;
	)";
	EXPECT_COMPILE_FAIL(source);
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

TEST(UnionNullableUnionError) {
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
	EXPECT_COMPILE_FAIL(source);
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

TEST(UnionAsWrongMember) {
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
			var maybe_b : ?B = u as B;
			if maybe_b != null {
				var y : f32 = maybe_b.y;
			}
		}
	)";
	EXPECT_COMPILE(source);
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

TEST(UnionIsAndAsRuntime) {
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
			if value is A {
				result = value.x;
			}
			if value is B {
				return 0;
			}
			var maybe_a : ?A = value as A;
			if maybe_a != null {
				result = result + maybe_a.x;
			}
			var maybe_b : ?B = value as B;
			if maybe_b == null {
				return result + 1;
			}
			return 0;
		}
	)";
	CAPI_BEGIN(module, diagnostics);
	EXPECT_TRUE(ls_module_compile(module, toLs(source), makeStringView(__func__), nullptr, nullptr));
	CAPI_RUNTIME(module, runtime);
	EXPECT_TRUE(ls_call(runtime, toLs("main")));
	EXPECT_EQ(15, ls_to_i32(runtime, -1));
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
				else:
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

TEST(UnionFieldGlobalAndRefRuntime) {
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

		fn set(value : ref Union) : void {
			value = B { 8 };
		}

		fn read(value : ref Union) : i32 {
			if value is B {
				return value.y;
			}
			return 0;
		}

		fn main() : i32 {
			set(ref global.value);
			return read(ref global.value);
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
		comptime StringOrInt = string | i32;

		fn main() : void {
			var u : StringOrInt = "hello";
			u = 42;

			match u {
				case string:
					var s : string = u;
				case i32:
					var i : i32 = u;
			}
		}
	)";
	EXPECT_COMPILE(source);
	return true;
}

TEST(UnionFunctionTypeMemberError) {
	const char* source = R"(
		comptime CallbackOrInt = (fn(i32) : i32) | i32;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionSliceMemberError) {
	const char* source = R"(
		comptime SliceOrInt = ([]i32) | i32;
	)";
	EXPECT_COMPILE_FAIL(source);
	return true;
}

TEST(UnionArrayMemberError) {
	const char* source = R"(
		comptime ArrayOrInt = ([4]i32) | i32;
	)";
	EXPECT_COMPILE_FAIL(source);
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

TEST(UnionAsNonMemberError) {
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

		var u : Union = A { 5 };
		var maybe_c : ?C = u as C;
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

TEST(UnionNullableMemberError) {
	const char* source = R"(
		comptime Union = ?i32 | i32;
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
