#include "core/log.h"
#include "lumscript/lumscript_engine_api.h"
#include "tests/lumscript_test_common.h"

using namespace Lumix;

namespace {

struct LumScriptImportFile {
	StringView path;
	StringView source;
};

struct LumScriptImportFiles {
	const LumScriptImportFile* files = nullptr;
	u32 count = 0;
};

bool resolveLumScriptImport(LumScript::Module&, StringView path, StringView, StringView* source, void* userdata) {
	const LumScriptImportFiles* imports = (const LumScriptImportFiles*)userdata;
	if (!imports) return false;
	Span<const LumScriptImportFile> files(imports->files, imports->count);
	for (const LumScriptImportFile& file : files) {
		if (equalStrings(file.path, path)) {
			*source = file.source;
			return true;
		}
	}
	return false;
}

bool resolveLumScriptEngineImport(LumScript::Module& module, StringView path, StringView alias, StringView* source, void*) {
	if (!LumScript::resolveEngineImport(module, nullptr, path, alias)) return false;
	*source = {};
	return true;
}

bool testParseAndTypecheckSample() {
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, LumScriptTests::SAMPLE, diagnostics));
	ASSERT_TRUE(module.structs.size() == 2);
	ASSERT_TRUE(module.functions.size() == 2);
	ASSERT_TRUE(module.expressions.size() > 0);
	ASSERT_TRUE(module.statements.size() > 0);
	return true;
}

bool testStringConcatenationTypechecks() {
	const char* source = R"(
		fn join(a : string, b : string) : string {
			return a + " " + b;
		}

		fn main() : string {
			const hello : string = "Hello";
			const target = "Lumix";
			return join(hello, target);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testStringConcatenationRejectsNonString() {
	const char* source = R"(
		fn main() : string {
			return "count: " + 42;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);

	const char* global_source = R"(
		const a = 1;
		fn main() : void {
			a = 2;
		}
	)";
	LumScript::Module module2(getGlobalAllocator());
	LumScript::Diagnostics diagnostics2(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module2, global_source, diagnostics2));
	ASSERT_TRUE(diagnostics2.has_error);
	return true;
}

bool testConstAssignmentFails() {
	const char* source = R"(
		fn main() : void {
			const a = 1;
			a = 2;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testDuplicateDeclarationsFail() {
	const char* duplicate_struct = R"(
		struct A { x : i32; };
		struct A { y : i32; };
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, duplicate_struct, diagnostics));

	const char* duplicate_field = R"(
		struct A { x : i32; x : i32; };
	)";
	LumScript::Module module2(getGlobalAllocator());
	LumScript::Diagnostics diagnostics2(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module2, duplicate_field, diagnostics2));

	const char* duplicate_local = R"(
		fn main() : void {
			var a = 1;
			var a = 2;
		}
	)";
	LumScript::Module module3(getGlobalAllocator());
	LumScript::Diagnostics diagnostics3(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module3, duplicate_local, diagnostics3));

	const char* duplicate_param = R"(
		fn f(a : i32, a : i32) : i32 {
			return a;
		}
	)";
	LumScript::Module module4(getGlobalAllocator());
	LumScript::Diagnostics diagnostics4(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module4, duplicate_param, diagnostics4));

	const char* duplicate_global = R"(
		var g = 1;
		var g = 2;
	)";
	LumScript::Module module5(getGlobalAllocator());
	LumScript::Diagnostics diagnostics5(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module5, duplicate_global, diagnostics5));

	const char* duplicate_global_function = R"(
		var main = 1;
		fn main() : void {
		}
	)";
	LumScript::Module module6(getGlobalAllocator());
	LumScript::Diagnostics diagnostics6(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module6, duplicate_global_function, diagnostics6));
	return true;
}

bool testGlobalVariablesTypecheck() {
	const char* source = R"(
		var counter : i32 = 1;
		const step = 2;
		var label : string = "count";

		fn increment() : i32 {
			counter += step;
			return counter;
		}

		fn get_label() : string {
			return label;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(module.globals.size() == 3);
	return true;
}

bool testDiagnosticsHaveSourceLocation() {
	const char* source = "fn main() : i32 {\n\treturn missing;\n}\n";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	ASSERT_TRUE(find(diagnostics.message, "line 2, column 9") != nullptr);
	ASSERT_TRUE(find(diagnostics.message, "Unknown variable 'missing'") != nullptr);
	return true;
}

bool testExplicitCastRequired() {
	const char* invalid = R"(
		fn main() : f32 {
			const x : i32 = 10;
			return x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, invalid, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);

	const char* valid = R"(
		fn main() : f32 {
			const x : i32 = 10;
			return x as f32;
		}
	)";
	LumScript::Module module2(getGlobalAllocator());
	LumScript::Diagnostics diagnostics2(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module2, valid, diagnostics2));
	return true;
}

bool testImportAddsDeclarationsToCurrentModule() {
	const char* main_source = R"(
		import "math"

		fn main() : i32 {
			const v : Vec2 = Vec2 { 20, 22 };
			return sum(v);
		}
	)";
	const char* math_source = R"(
		struct Vec2 {
			x : i32;
			y : i32;
		};

		fn sum(v : Vec2) : i32 {
			return v.x + v.y;
		}
	)";
	LumScriptImportFile file = { "math", math_source };
	LumScriptImportFiles files = { &file, 1 };
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, main_source, diagnostics, resolveLumScriptImport, &files));
	ASSERT_TRUE(module.structs.size() == 3);
	ASSERT_TRUE(module.functions.size() == 2);
	return true;
}

bool testMissingImportFails() {
	const char* source = R"(
		import "missing"

		fn main() : void {
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics, resolveLumScriptImport, nullptr));
	ASSERT_TRUE(diagnostics.has_error);
	ASSERT_TRUE(find(diagnostics.message, "Can not import 'missing'") != nullptr);
	return true;
}

bool testGeneratedEngineImportTypechecks() {
	const char* source = R"(
		import "engine:entity" as entity
		import "engine:animator" as animator

		fn update(e : entity.Entity) : void {
			const anim = e.animator();
			if anim != null {
				const speed : i32 = anim.getInputIndex("speed_y");
				anim.setFloatInput(3, 12.5);
				anim.setFloatInput(speed, 1.0);
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	const bool ok = LumScript::compile(module, source, diagnostics, resolveLumScriptEngineImport, nullptr);
	if (!ok) logError(diagnostics.message);
	ASSERT_TRUE(ok);
	return true;
}

bool testGeneratedWorldImportTypechecks() {
	const char* source = R"(
		import "engine:world" as world

		fn init(w : world.World) : void {
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	const bool ok = LumScript::compile(module, source, diagnostics, resolveLumScriptEngineImport, nullptr);
	if (!ok) logError(diagnostics.message);
	ASSERT_TRUE(ok);
	return true;
}

bool testGeneratedInputImportTypechecks() {
	const char* source = R"(
		import "engine:world" as world
		import "engine:input" as input

		fn init(w : world.World, inputs : input.InputSystem) : void {
			var i : i32 = 0;
			const count = inputs.getEventCount();
			while (i < count) {
				const e = inputs.getEvent(i);
				const event_type = e.getType();
				if event_type == input.BUTTON() {
					const key : i32 = e.getKeyId();
					const down : bool = e.isDown();
					const repeat : bool = e.isRepeat();
					const x : f32 = e.getX();
					const y : f32 = e.getY();
					if (key == input.Keycode.W) {
						const w : i32 = input.Keycode.W;
					}
				}
				if event_type == input.AXIS() {
					const axis : i32 = e.getAxis();
					const value : f32 = e.getValue();
				}
				if event_type == input.MOUSE_WHEEL() {
					const wheel_y : f32 = e.getY();
				}
				if event_type == input.TEXT_INPUT() {
					const text : i32 = e.getText();
				}
				if event_type == input.DEVICE_ADDED() {
					const device : i32 = e.getDeviceType();
					const index : i32 = e.getDeviceIndex();
				}
				if event_type == input.DEVICE_REMOVED() {
					const device : i32 = e.getDeviceType();
					const index : i32 = e.getDeviceIndex();
				}
				i = i + 1;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	const bool ok = LumScript::compile(module, source, diagnostics, resolveLumScriptEngineImport, nullptr);
	if (!ok) logError(diagnostics.message);
	ASSERT_TRUE(ok);
	return true;
}

bool testGeneratedWorldFunctionsTypecheck() {
	const char* source = R"(
		import "engine:world" as world
		import "engine:entity" as entity

		fn init(w : world.World) : void {
			var e = world.createEntity(w);
			var named = w.findByName("Player");
			e.setPosition({ 1.0, 2.0, 3.0 });
			e.setRotation({ 0.0, 0.0, 0.0, 1.0 });
			e.setScale({ 2.0, 2.0, 2.0 });
			var p : Vec3 = e.getPosition();
			var r : Quat = e.getRotation();
			var s : Vec3 = e.getScale();
			var px : f32 = p.x;
			var rw : f32 = r.w;
			var sy : f32 = s.y;
			if named != null {
				named.destroy();
			}
			if e.isValid() {
				e.destroy();
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics, resolveLumScriptEngineImport, nullptr));
	bool found_find_by_name = false;
	for (const LumScript::NativeFunctionDecl& fn : module.native_functions) {
		if (!equalStrings(fn.name, "world.findByName")) continue;
		found_find_by_name = true;
		ASSERT_TRUE(fn.return_type.kind == LumScript::TypeRef::NATIVE);
		ASSERT_TRUE(fn.return_type.nullable);
	}
	ASSERT_TRUE(found_find_by_name);
	return true;
}

bool testDemoLumTypechecks() {
	const char* source = R"(
		import "engine:world" as world
		import "engine:entity" as entity
		import "engine:input" as input

		fn init(w : world.World, inputs : input.InputSystem) : void {
			var demo_entity = world.createEntity(w);
			var testor = w.findByName("testor");
			if testor != null {
				testor.setPosition({ 1.0, 2.0, 3.0 });
			}

			demo_entity.setPosition({ 0.0, 1.0, 0.0 });
			demo_entity.setRotation({ 0.0, 0.0, 0.0, 1.0 });
			demo_entity.setScale({ 1.0, 1.0, 1.0 });

			const position = demo_entity.getPosition();
			logError("Hello world!");
		}

		fn update(dt : f32) : void {
			logError("update");
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compileWithBuiltins(module, source, diagnostics, resolveLumScriptEngineImport, nullptr));
	return true;
}

bool testEnumShorthandInComparison() {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		};

		fn check(state : State) : bool {
			return state == .Running;
		}

		fn main() : void {
			var s : State = .Idle;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testEnumShorthandInAssignment() {
	const char* source = R"(
		enum Priority {
			Low = 0,
			Medium = 5,
			High = 10
		};

		fn main() : void {
			var p : Priority = .High;
			p = .Low;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testEnumShorthandInFunctionArg() {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn set_state(s : State) : void {
		}

		fn main() : void {
			set_state(.Running);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testEnumShorthandAmbiguousFails() {
	const char* source = R"(
		fn main() : void {
			var x = .Running;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testMatchTypechecks() {
	const char* source = R"(
		enum State {
			Idle,
			Running,
			Paused
		};

		fn enum_match(state : State) : i32 {
			match state {
				case .Idle:
					return 1;
				case .Running, .Paused:
					return 2;
			}
			return 0;
		}

		fn range_match(score : i32) : i32 {
			match score {
				case 0:
					return 0;
				case 1..9, 99:
					return 1;
				case _:
					return 2;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testMatchRejectsPatternTypeMismatch() {
	const char* source = R"(
		fn main(v : i32) : void {
			match v {
				case "bad":
					return;
				case _:
					return;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testMatchRequiresExhaustiveEnumOrFallback() {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn main(state : State) : void {
			match state {
				case .Idle:
					return;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testMatchRejectsDuplicateEnumCase() {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn main(state : State) : void {
			match state {
				case .Idle:
					return;
				case .Idle:
					return;
				case .Running:
					return;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testRefParameterTypechecks() {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(ref x);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testRefParameterRequiresRefArgument() {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(x);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testRefArgumentMustBeAssignable() {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var x : i32 = 10;
			increment(ref (x + 1));
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testRefArgumentCanNotBeConst() {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			const x : i32 = 10;
			increment(ref x);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testDeferTypechecks() {
	const char* source = R"(
		fn cleanup(v : ref i32) : void {
			v += 1;
		}

		fn main() : i32 {
			var x : i32 = 0;
			defer cleanup(ref x);
			x += 2;
			return x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testDeferCanNotWrapReturn() {
	const char* source = R"(
		fn main() : i32 {
			defer return 1;
			return 2;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testNullablePromotionTypechecks() {
	const char* source = R"(
		fn length_if_present(v : ?Vec3) : f32 {
			if v != null {
				return v.x;
			}
			return 0 as f32;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testNullableUseWithoutCheckFails() {
	const char* source = R"(
		fn bad(v : ?Vec3) : f32 {
			return v.x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testNullOnlyAssignableToNullable() {
	const char* bad = R"(
		fn bad() : i32 {
			var x : i32 = null;
			return x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, bad, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);

	const char* ok = R"(
		fn ok() : i32 {
			var x : ?i32 = null;
			if x != null {
				return x;
			}
			return 0;
		}
	)";
	LumScript::Module module2(getGlobalAllocator());
	LumScript::Diagnostics diagnostics2(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module2, ok, diagnostics2));
	return true;
}

bool testExtendedScalarTypesTypecheck() {
	const char* source = R"(
		fn main() : i32 {
			const a : i8 = 10 as i8;
			const b : u8 = 20 as u8;
			const c : i16 = 30 as i16;
			const d : u16 = 40 as u16;
			const e : i64 = 50 as i64;
			const f : u64 = 60 as u64;
			const g : f64 = 1 as f64;
			return a as i32 + b as i32 + c as i32 + d as i32 + e as i32 + f as i32 + g as i32;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testUntypedLiteralsUseExpectedTypes() {
	const char* source = R"(
		struct Pair {
			x : u8;
			y : f64;
		};

		fn takes_f32(v : f32) : f32 {
			return v;
		}

		fn takes_i16(v : i16) : i16 {
			return v;
		}

		fn returns_i64() : i64 {
			return 42;
		}

		fn main() : f32 {
			const a : i8 = 10;
			const b : u16 = 20;
			const c : f64 = 1.5;
			const d : Vec3 = { 1, 2, 3 };
			const e = Pair { 255, 2.5 };
			return takes_f32(12) + takes_i16(3) as f32 + d.x + e.y as f32 + returns_i64() as f32 + a as f32 + b as f32 + c as f32;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testDecimalLiteralDoesNotConcretizeToInteger() {
	const char* source = R"(
		fn main() : i32 {
			const a : i32 = 1.5;
			return a;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

} // anonymous namespace

void runLumScriptCompilerTests() {
	RUN_TEST(testParseAndTypecheckSample);
	RUN_TEST(testConstAssignmentFails);
	RUN_TEST(testDuplicateDeclarationsFail);
	RUN_TEST(testGlobalVariablesTypecheck);
	RUN_TEST(testDiagnosticsHaveSourceLocation);
	RUN_TEST(testExplicitCastRequired);
	RUN_TEST(testImportAddsDeclarationsToCurrentModule);
	RUN_TEST(testMissingImportFails);
	RUN_TEST(testGeneratedEngineImportTypechecks);
	RUN_TEST(testGeneratedWorldImportTypechecks);
	RUN_TEST(testGeneratedInputImportTypechecks);
	RUN_TEST(testGeneratedWorldFunctionsTypecheck);
	RUN_TEST(testDemoLumTypechecks);
	RUN_TEST(testEnumShorthandInComparison);
	RUN_TEST(testEnumShorthandInAssignment);
	RUN_TEST(testEnumShorthandInFunctionArg);
	RUN_TEST(testEnumShorthandAmbiguousFails);
	RUN_TEST(testMatchTypechecks);
	RUN_TEST(testMatchRejectsPatternTypeMismatch);
	RUN_TEST(testMatchRequiresExhaustiveEnumOrFallback);
	RUN_TEST(testMatchRejectsDuplicateEnumCase);
	RUN_TEST(testRefParameterTypechecks);
	RUN_TEST(testRefParameterRequiresRefArgument);
	RUN_TEST(testRefArgumentMustBeAssignable);
	RUN_TEST(testRefArgumentCanNotBeConst);
	RUN_TEST(testDeferTypechecks);
	RUN_TEST(testDeferCanNotWrapReturn);
	RUN_TEST(testNullablePromotionTypechecks);
	RUN_TEST(testNullableUseWithoutCheckFails);
	RUN_TEST(testNullOnlyAssignableToNullable);
	RUN_TEST(testExtendedScalarTypesTypecheck);
	RUN_TEST(testUntypedLiteralsUseExpectedTypes);
	RUN_TEST(testDecimalLiteralDoesNotConcretizeToInteger);
	RUN_TEST(testStringConcatenationTypechecks);
	RUN_TEST(testStringConcatenationRejectsNonString);
}
