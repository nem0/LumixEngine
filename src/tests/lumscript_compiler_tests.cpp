#include "core/log.h"
#include "lumscript/lumscript_engine_api.h"
#include "tests/lumscript_test_common.h"

#define ASSERT_COMPILE(CALL) do { \
		bool ok = CALL; \
		if (!ok) logError(diagnostics.message); \
		ASSERT_TRUE(ok); \
	} while (false)

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

static const char* CORE_VEC3_SOURCE = R"(
	struct Vec3 {
		x : f32;
		y : f32;
		z : f32;
	};
)";

static const char* CORE_DVEC3_SOURCE = R"(
	struct DVec3 {
		x : f64;
		y : f64;
		z : f64;
	};
)";

static const char* CORE_QUAT_SOURCE = R"(
	struct Quat {
		x : f32;
		y : f32;
		z : f32;
		w : f32;
	};
)";

bool resolveCoreTestImport(StringView path, StringView* source) {
	if (equalStrings(path, "core:vec3") || equalStrings(path, "core:vec3.lum")) {
		*source = CORE_VEC3_SOURCE;
		return true;
	}
	if (equalStrings(path, "core:dvec3") || equalStrings(path, "core:dvec3.lum")) {
		*source = CORE_DVEC3_SOURCE;
		return true;
	}
	if (equalStrings(path, "core:quat") || equalStrings(path, "core:quat.lum")) {
		*source = CORE_QUAT_SOURCE;
		return true;
	}
	return false;
}

bool resolveLumScriptImport(LumScript::Module&, StringView path, StringView, StringView* source, void* userdata) {
	if (resolveCoreTestImport(path, source)) return true;
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
	if (resolveCoreTestImport(path, source)) return true;
	if (!LumScript::resolveEngineImport(module, nullptr, path, alias)) return false;
	*source = {};
	return true;
}

bool testParseAndTypecheckSample() {
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, LumScriptTests::SAMPLE, diagnostics, resolveLumScriptImport, nullptr));
	ASSERT_TRUE(module.structs.size() == 1);
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
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

bool testCoreVec3RequiresImport() {
	const char* source = R"(
		fn main() : void {
			const v : Vec3 = { 1, 2, 3 };
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(module.globals.size() == 3);
	return true;
}

bool testCoreDVec3RequiresImport() {
	const char* source = R"(
		fn main() : void {
			const v : DVec3 = { 1, 2, 3 };
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testCoreDVec3TypechecksWithImport() {
	const char* source = R"(
		import "core:dvec3"
		fn main() : f64 {
			const v : DVec3 = DVec3 { 1.0, 2.0, 3.0 };
			return v.x + v.y + v.z;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics, resolveLumScriptImport, nullptr));
	return true;
}

bool testVariableAndConstRequireInitializer() {
	const char* source = R"(
		var g : i32;

		fn main() : void {
			var local : i32;
			const c : i32;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testVariableCanBeExplicitlyUndefined() {
	const char* source = R"(
		var g : i32 = undefined;

		fn main() : i32 {
			var local : i32 = undefined;
			local = 7;
			return local + g;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testConstCanNotBeUndefined() {
	const char* source = R"(
		const g : i32 = undefined;

		fn main() : void {
			const c : i32 = undefined;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
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
	ASSERT_COMPILE(LumScript::compile(module2, valid, diagnostics2));
	return true;
}

bool testBinaryNumericOperatorsRequireSameOperandType() {
	const char* mixed_add = R"(
		fn main() : i32 {
			const a : i32 = 1;
			const b : i64 = 2 as i64;
			return a + b;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, mixed_add, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);

	const char* mixed_compare = R"(
		fn main() : bool {
			const a : i32 = 1;
			const b : f32 = 1 as f32;
			return a < b;
		}
	)";
	LumScript::Module module2(getGlobalAllocator());
	LumScript::Diagnostics diagnostics2(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module2, mixed_compare, diagnostics2));
	ASSERT_TRUE(diagnostics2.has_error);

	const char* explicit_cast_ok = R"(
		fn main() : i64 {
			const a : i32 = 1;
			const b : i64 = 2 as i64;
			return (a as i64) + b;
		}
	)";
	LumScript::Module module3(getGlobalAllocator());
	LumScript::Diagnostics diagnostics3(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module3, explicit_cast_ok, diagnostics3));
	return true;
}

bool testDivisionAndModuloByConstantZeroFail() {
	const char* divide_source = R"(
		fn main(v : i32) : i32 {
			return v / 0;
		}
	)";
	LumScript::Module divide_module(getGlobalAllocator());
	LumScript::Diagnostics divide_diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(divide_module, divide_source, divide_diagnostics));
	ASSERT_TRUE(divide_diagnostics.has_error);

	const char* modulo_source = R"(
		fn main(v : i32) : i32 {
			return v % 0;
		}
	)";
	LumScript::Module modulo_module(getGlobalAllocator());
	LumScript::Diagnostics modulo_diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(modulo_module, modulo_source, modulo_diagnostics));
	ASSERT_TRUE(modulo_diagnostics.has_error);

	const char* float_source = R"(
		fn main() : f32 {
			return 1.0 / 0.0;
		}
	)";
	LumScript::Module float_module(getGlobalAllocator());
	LumScript::Diagnostics float_diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(float_module, float_source, float_diagnostics));
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
	ASSERT_COMPILE(LumScript::compile(module, main_source, diagnostics, resolveLumScriptImport, &files));
	ASSERT_TRUE(module.structs.size() == 1);
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

bool testDuplicateUnaliasedImportIsNoOp() {
	const char* main_source = R"(
		import "math"
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
	ASSERT_COMPILE(LumScript::compile(module, main_source, diagnostics, resolveLumScriptImport, &files));
	ASSERT_EQ(1, module.structs.size());
	ASSERT_EQ(2, module.functions.size());
	return true;
}

bool testDuplicateAliasedImportOfSamePathIsNoOp() {
	const char* main_source = R"(
		import "math" as m
		import "math" as m

		fn main() : i32 {
			const v : m.Vec2 = m.Vec2 { 20, 22 };
			return m.sum(v);
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
	ASSERT_COMPILE(LumScript::compile(module, main_source, diagnostics, resolveLumScriptImport, &files));
	ASSERT_EQ(1, module.structs.size());
	ASSERT_EQ(2, module.functions.size());
	return true;
}

bool testAliasedImportCollisionFails() {
	const char* source = R"(
		import "math_a" as m
		import "math_b" as m

		fn main() : i32 {
			return 0;
		}
	)";
	const char* math_a_source = R"(
		fn one() : i32 {
			return 1;
		}
	)";
	const char* math_b_source = R"(
		fn two() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ "math_a", math_a_source },
		{ "math_b", math_b_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics, resolveLumScriptImport, &files));
	ASSERT_TRUE(diagnostics.has_error);
	ASSERT_TRUE(find(diagnostics.message, "Import alias collision") != nullptr);
	return true;
}

bool testImportCycleFails() {
	const char* source = R"(
		import "a"

		fn main() : i32 {
			return 0;
		}
	)";
	const char* a_source = R"(
		import "b"

		fn in_a() : i32 {
			return 1;
		}
	)";
	const char* b_source = R"(
		import "a"

		fn in_b() : i32 {
			return 2;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ "a", a_source },
		{ "b", b_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics, resolveLumScriptImport, &files));
	ASSERT_TRUE(diagnostics.has_error);
	ASSERT_TRUE(find(diagnostics.message, "Import cycle") != nullptr);
	return true;
}

bool testGeneratedEngineImportTypechecks() {
	const char* source = R"(
		import "core:vec3"
		import "core:quat"
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
		import "engine:Keycode"

		fn init(w : world.World, inputs : input.InputSystem) : void {
			var i : i32 = 0;
			const count = inputs.getEventCount();
			while (i < count) {
				const e = inputs.getEvent(i);
				const event_type = e.getType();
				if event_type == InputEventType.BUTTON {
					const key : i32 = e.getKeyId();
					const down : bool = e.isDown();
					const repeat : bool = e.isRepeat();
					const x : f32 = e.getX();
					const y : f32 = e.getY();
					if (key == Keycode.W as i32) {
						const w : i32 = Keycode.W as i32;
					}
				}
				if event_type == InputEventType.AXIS {
					const axis : i32 = e.getAxis();
					const value : f32 = e.getValue();
				}
				if event_type == InputEventType.MOUSE_WHEEL {
					const wheel_y : f32 = e.getY();
				}
				if event_type == InputEventType.TEXT_INPUT {
					const text : i32 = e.getText();
				}
				if event_type == InputEventType.DEVICE_ADDED {
					const device : i32 = e.getDeviceType();
					const index : i32 = e.getDeviceIndex();
				}
				if event_type == InputEventType.DEVICE_REMOVED {
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

bool testTransitiveNativeImport() {
	const char* source = R"(
		import "core:dvec3"
		import "engine:world" as world
		import "engine:entity" as entity

		fn init(w : world.World) : void {
			var e = world.createEntity(w);
			e.setPosition({ 1.0, 2.0, 3.0 });
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	bool ok = LumScript::compile(module, source, diagnostics, resolveLumScriptEngineImport, nullptr);
	if (!ok) logError(diagnostics.message);
	ASSERT_TRUE(ok);
	return true;
}

bool testGeneratedWorldFunctionsTypecheck() {
	const char* source = R"(
		import "core:vec3"
		import "core:dvec3"
		import "core:quat"
		import "engine:world" as world
		import "engine:entity" as entity

		fn init(w : world.World) : void {
			var e = world.createEntity(w);
			var named = w.findByName("Player");
			e.setPosition({ 1.0, 2.0, 3.0 });
			e.setRotation({ 0.0, 0.0, 0.0, 1.0 });
			e.setScale({ 2.0, 2.0, 2.0 });
			var p : DVec3 = e.getPosition();
			var r : Quat = e.getRotation();
			var s : Vec3 = e.getScale();
			var px : f64 = p.x;
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics, resolveLumScriptEngineImport, nullptr));
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

bool testWorldRendererAccessorTypecheck() {
	const char* source = R"(
		import "engine:world" as world

		fn init(w : world.World) : void {
			const r = w.renderer();
			if r != null {
				return;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	const bool ok = LumScript::compile(module, source, diagnostics, resolveLumScriptEngineImport, nullptr);
	ASSERT_TRUE(ok);
	return true;
}

bool testFirstParameterNamespaceResolutionPrecedenceTypecheck() {
	const char* main_source = R"(
		import "entity_mod" as entity
		import "helper_mod" as e

		fn destroy(x : entity.Entity) : i32 {
			return 3;
		}

		fn main() : i32 {
			const x : entity.Entity = entity.Entity { 7 };
			return e.destroy() + x.destroy() + destroy(x);
		}
	)";

	const char* entity_source = R"(
		struct Entity {
			id : i32;
		};

		fn destroy(x : Entity) : i32 {
			return x.id;
		}
	)";

	const char* helper_source = R"(
		fn destroy() : i32 {
			return 2;
		}
	)";

	LumScriptImportFile files_storage[] = {
		{ "entity_mod", entity_source },
		{ "helper_mod", helper_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };

	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, main_source, diagnostics, resolveLumScriptImport, &files));
	return true;
}

bool testWorldTransformFunctionsAreNotRegistered() {
	const char* source = R"(
		import "core:vec3"
		import "core:quat"
		import "engine:world" as world
		import "engine:entity" as entity

		fn init(w : world.World, e : entity.Entity) : void {
			const p = world.getPosition(w, e);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics, resolveLumScriptEngineImport, nullptr));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testDemoLumTypechecks() {
	const char* source = R"(
		import "core:dvec3"
		import "core:quat"
		import "engine:world" as world
		import "engine:entity" as entity
		import "engine:input" as input
		import "engine:log" as log

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
			log.logError("Hello world!");
		}

		fn update(dt : f32) : void {
			log.logError("update");
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compileWithBuiltins(module, source, diagnostics, resolveLumScriptEngineImport, nullptr));
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
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

bool testEnumDoesNotConvertImplicitlyToInteger() {
	const char* assignment = R"(
		enum State {
			Idle,
			Running
		};

		fn main() : void {
			const value : i32 = State.Running;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, assignment, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);

	const char* argument = R"(
		enum State {
			Idle,
			Running
		};

		fn takes_i32(value : i32) : void {
		}

		fn main() : void {
			takes_i32(State.Running);
		}
	)";
	LumScript::Module module2(getGlobalAllocator());
	LumScript::Diagnostics diagnostics2(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module2, argument, diagnostics2));
	ASSERT_TRUE(diagnostics2.has_error);

	const char* comparison = R"(
		enum State {
			Idle,
			Running
		};

		fn main(value : i32) : bool {
			return value == State.Running;
		}
	)";
	LumScript::Module module3(getGlobalAllocator());
	LumScript::Diagnostics diagnostics3(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module3, comparison, diagnostics3));
	ASSERT_TRUE(diagnostics3.has_error);

	const char* integer_to_enum = R"(
		enum State {
			Idle,
			Running
		};

		fn main(value : i32) : void {
			const state : State = value;
		}
	)";
	LumScript::Module module4(getGlobalAllocator());
	LumScript::Diagnostics diagnostics4(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module4, integer_to_enum, diagnostics4));
	ASSERT_TRUE(diagnostics4.has_error);
	return true;
}

bool testEnumCanBeExplicitlyCastToInteger() {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn takes_i32(value : i32) : void {
		}

		fn main(value : i32) : bool {
			const running : i32 = State.Running as i32;
			const state : State = value as State;
			takes_i32(State.Idle as i32);
			return state == State.Running and value == State.Running as i32;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics, resolveLumScriptImport, nullptr));
	return true;
}

bool testIntegerToEnumCastAllowsAnyIntegerValue() {
	const char* source = R"(
		enum State {
			Idle,
			Running
		};

		fn main() : State {
			const raw : i32 = 123;
			return raw as State;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testMatchArmAllowsMultipleStatements() {
	const char* source = R"(
		fn main(v : i32) : i32 {
			var result : i32 = 0;
			match v {
				case 0:
					result = 1;
					result += 2;
				case _:
					result = 10;
					result += 20;
			}
			return result;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
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

bool testRefArgumentAllowsMutableGlobal() {
	const char* source = R"(
		var counter : i32 = 0;

		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			increment(ref counter);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testRefArgumentRejectsConstGlobal() {
	const char* source = R"(
		const counter : i32 = 0;

		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			increment(ref counter);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testRefArgumentAllowsNestedMutableField() {
	const char* source = R"(
		struct Stats {
			hp : i32;
		};

		struct Player {
			stats : Stats;
		};

		fn bump(v : ref i32) : void {
			v += 1;
		}

		fn main() : void {
			var p = Player { Stats { 10 } };
			bump(ref p.stats.hp);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testRefArgumentRejectsNullableTargetType() {
	const char* source = R"(
		fn clear(v : ref ?i32) : void {
			v = null;
		}

		fn main() : void {
			var x : ?i32 = 10;
			clear(ref x);
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
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
		import "core:vec3"

		fn length_if_present(v : ?Vec3) : f32 {
			if v != null {
				return v.x;
			}
			return 0 as f32;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics, resolveLumScriptImport, nullptr));
	return true;
}

bool testNullableUseWithoutCheckFails() {
	const char* source = R"(
		import "core:vec3"

		fn bad(v : ?Vec3) : f32 {
			return v.x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics, resolveLumScriptImport, nullptr));
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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testUntypedLiteralsUseExpectedTypes() {
	const char* source = R"(
		import "core:vec3"

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
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics, resolveLumScriptImport, nullptr));
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

bool testFirstClassFunctionsTypecheck() {
	const char* source = R"(
		fn add(a : i32, b : i32) : i32 {
			return a + b;
		}

		fn apply(f : fn(i32, i32) : i32, a : i32, b : i32) : i32 {
			return f(a, b);
		}

		fn main() : i32 {
			const f = add;
			const g : fn(i32, i32) : i32 = add;
			return apply(f, 20, 2) + g(10, 5);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testFirstClassFunctionSignatureMismatchFails() {
	const char* source = R"(
		fn to_float(a : i32) : f32 {
			return a as f32;
		}

		fn main() : void {
			const f : fn(i32) : i32 = to_float;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics, resolveLumScriptImport, nullptr));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testNestedFunctionsTypecheck() {
	const char* source = R"(
		fn main() : i32 {
			fn add(a : i32, b : i32) : i32 {
				return a + b;
			}
			return add(20, 22);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics, resolveLumScriptImport, nullptr));
	return true;
}

bool testNestedFunctionCanNotCaptureOuterLocal() {
	const char* source = R"(
		fn main() : i32 {
			const x = 10;
			fn get() : i32 {
				return x;
			}
			return get();
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testNestedFunctionNotGloballyVisible() {
	const char* source = R"(
		fn main() : i32 {
			fn local() : i32 {
				return 1;
			}
			return local();
		}

		fn other() : i32 {
			return local();
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testGeneratedImguiImportTypechecks() {
	const char* source = R"(
		import "engine:imgui" as imgui

		fn update(dt : f32) : void {
			if imgui.beginWindow("Demo") {
				imgui.textUnformatted("Hello");
				if imgui.button("Do Action") {
				}
				imgui.endWindow();
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

bool testStaticArrayTypecheckAndIndexing() {
	const char* source = R"(
		fn main() : i32 {
			var d : i32[4] = undefined;
			d[0] = 40;
			d[1] = 2;
			return d[0] + d[1];
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testStaticArrayConstantIndexOutOfRangeFails() {
	const char* source = R"(
		fn main() : void {
			var d : i32[4] = undefined;
			d[99] = 1;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testStaticArrayIndexMustBeInteger() {
	const char* source = R"(
		fn main() : void {
			var d : i32[4] = undefined;
			d[1.5] = 1;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testBreakContinueTypecheck() {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			while i < 10 {
				i += 1;
				if i == 3 {
					continue;
				}
				if i == 8 {
					break;
				}
			}
			return i;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testBreakContinueOutsideLoopFail() {
	const char* source = R"(
		fn main() : void {
			break;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testNamedLabelTypecheck() {
	const char* source = R"(
		fn main() : i32 {
			var i : i32 = 0;
			outer: while i < 10 {
				i += 1;
				var j : i32 = 0;
				while j < 10 {
					j += 1;
					if j == 2 {
						continue outer;
					}
				}
			}
			return i;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_COMPILE(LumScript::compile(module, source, diagnostics));
	return true;
}

bool testNamedLabelUnknownFails() {
	const char* source = R"(
		fn main() : void {
			while true {
				break missing;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testDuplicateNamedLabelFails() {
	const char* source = R"(
		fn main() : void {
			outer: while true {
				break;
			}
			outer: while true {
				break;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compile(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testDiagnosticsIncludeSourceName() {
	{
		const char* source = R"(
			fn main() : void {
				missing;
			}
		)";
		LumScript::Module module(getGlobalAllocator());
		LumScript::Diagnostics diagnostics(getGlobalAllocator());
		ASSERT_TRUE(!LumScript::compile(module, source, diagnostics, nullptr, nullptr, "main.lum"));
		ASSERT_TRUE(find(diagnostics.message, "main.lum: line") != nullptr);
		ASSERT_TRUE(find(diagnostics.message, "Unknown variable 'missing'") != nullptr);
	}
	{
		const char* source = R"(
			import "bad"
		)";
		auto resolver = [](LumScript::Module&, StringView path, StringView, StringView* source, void*) -> bool {
			if (!equalStrings(path, "bad")) return false;
			*source = "fn broken() : void { if }";
			return true;
		};
		LumScript::Module module(getGlobalAllocator());
		LumScript::Diagnostics diagnostics(getGlobalAllocator());
		ASSERT_TRUE(!LumScript::compile(module, source, diagnostics, resolver, nullptr, "main.lum"));
		ASSERT_TRUE(find(diagnostics.message, "bad: line") != nullptr);
		ASSERT_TRUE(find(diagnostics.message, "main.lum:") == nullptr);
	}
	return true;
}

} // anonymous namespace

void runLumScriptCompilerTests() {
	RUN_TEST(testParseAndTypecheckSample);
	RUN_TEST(testConstAssignmentFails);
	RUN_TEST(testDuplicateDeclarationsFail);
	RUN_TEST(testGlobalVariablesTypecheck);
	RUN_TEST(testVariableAndConstRequireInitializer);
	RUN_TEST(testVariableCanBeExplicitlyUndefined);
	RUN_TEST(testConstCanNotBeUndefined);
	RUN_TEST(testDiagnosticsHaveSourceLocation);
	RUN_TEST(testExplicitCastRequired);
	RUN_TEST(testBinaryNumericOperatorsRequireSameOperandType);
	RUN_TEST(testDivisionAndModuloByConstantZeroFail);
	RUN_TEST(testImportAddsDeclarationsToCurrentModule);
	RUN_TEST(testMissingImportFails);
	RUN_TEST(testDuplicateUnaliasedImportIsNoOp);
	RUN_TEST(testDuplicateAliasedImportOfSamePathIsNoOp);
	RUN_TEST(testAliasedImportCollisionFails);
	RUN_TEST(testImportCycleFails);
	RUN_TEST(testGeneratedEngineImportTypechecks);
	RUN_TEST(testGeneratedWorldImportTypechecks);
	RUN_TEST(testGeneratedInputImportTypechecks);
	RUN_TEST(testGeneratedImguiImportTypechecks);
	RUN_TEST(testTransitiveNativeImport);
	RUN_TEST(testGeneratedWorldFunctionsTypecheck);
	RUN_TEST(testWorldRendererAccessorTypecheck);
	RUN_TEST(testFirstParameterNamespaceResolutionPrecedenceTypecheck);
	RUN_TEST(testWorldTransformFunctionsAreNotRegistered);
	RUN_TEST(testDemoLumTypechecks);
	RUN_TEST(testEnumShorthandInComparison);
	RUN_TEST(testEnumShorthandInAssignment);
	RUN_TEST(testEnumShorthandInFunctionArg);
	RUN_TEST(testEnumShorthandAmbiguousFails);
	RUN_TEST(testEnumDoesNotConvertImplicitlyToInteger);
	RUN_TEST(testEnumCanBeExplicitlyCastToInteger);
	RUN_TEST(testIntegerToEnumCastAllowsAnyIntegerValue);
	RUN_TEST(testMatchTypechecks);
	RUN_TEST(testMatchArmAllowsMultipleStatements);
	RUN_TEST(testMatchRejectsPatternTypeMismatch);
	RUN_TEST(testMatchRequiresExhaustiveEnumOrFallback);
	RUN_TEST(testMatchRejectsDuplicateEnumCase);
	RUN_TEST(testRefParameterTypechecks);
	RUN_TEST(testRefParameterRequiresRefArgument);
	RUN_TEST(testRefArgumentMustBeAssignable);
	RUN_TEST(testRefArgumentCanNotBeConst);
	RUN_TEST(testRefArgumentAllowsMutableGlobal);
	RUN_TEST(testRefArgumentRejectsConstGlobal);
	RUN_TEST(testRefArgumentAllowsNestedMutableField);
	RUN_TEST(testRefArgumentRejectsNullableTargetType);
	RUN_TEST(testDeferTypechecks);
	RUN_TEST(testDeferCanNotWrapReturn);
	RUN_TEST(testNullablePromotionTypechecks);
	RUN_TEST(testNullableUseWithoutCheckFails);
	RUN_TEST(testNullOnlyAssignableToNullable);
	RUN_TEST(testExtendedScalarTypesTypecheck);
	RUN_TEST(testUntypedLiteralsUseExpectedTypes);
	RUN_TEST(testDecimalLiteralDoesNotConcretizeToInteger);
	RUN_TEST(testFirstClassFunctionsTypecheck);
	RUN_TEST(testFirstClassFunctionSignatureMismatchFails);
	RUN_TEST(testNestedFunctionsTypecheck);
	RUN_TEST(testNestedFunctionCanNotCaptureOuterLocal);
	RUN_TEST(testNestedFunctionNotGloballyVisible);
	RUN_TEST(testStaticArrayTypecheckAndIndexing);
	RUN_TEST(testStaticArrayConstantIndexOutOfRangeFails);
	RUN_TEST(testStaticArrayIndexMustBeInteger);
	RUN_TEST(testBreakContinueTypecheck);
	RUN_TEST(testBreakContinueOutsideLoopFail);
	RUN_TEST(testNamedLabelTypecheck);
	RUN_TEST(testNamedLabelUnknownFails);
	RUN_TEST(testDuplicateNamedLabelFails);
	RUN_TEST(testDiagnosticsIncludeSourceName);
	RUN_TEST(testStringConcatenationTypechecks);
	RUN_TEST(testStringConcatenationRejectsNonString);
	RUN_TEST(testCoreVec3RequiresImport);
	RUN_TEST(testCoreDVec3RequiresImport);
	RUN_TEST(testCoreDVec3TypechecksWithImport);
}
