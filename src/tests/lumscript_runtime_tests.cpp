#include "core/log.h"
#include "engine/input_system.h"
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

bool resolveRealEngineImport(LumScript::Module& module, StringView path, StringView alias, StringView* source, void*) {
	if (!LumScript::resolveEngineImport(module, nullptr, path, alias)) return false;
	*source = {};
	return true;
}

struct FakeInputDevice final : InputSystem::Device {
	FakeInputDevice() {
		type = KEYBOARD;
		index = 2;
	}

	void update(float) override {}
	const char* getName() const override { return "fake"; }
};

struct FakeInputSystem final : InputSystem {
	explicit FakeInputSystem(IAllocator& allocator)
		: events(allocator)
	{}

	IAllocator& getAllocator() override { return getGlobalAllocator(); }
	void update(float) override {}
	void injectEvent(const Event& event) override { events.push(event); }
	void injectEvent(const os::Event&, int, int) override {}
	Span<const Event> getEvents() const override { return events; }
	void resetDownKeys() override {}
	void addDevice(Device*) override {}
	void removeDevice(Device*) override {}
	Span<Device*> getDevices() override { return Span<Device*>(); }

	Array<Event> events;
};

struct NativeEngineImportState {
	i32 entity = -1;
	u32 input = 0xffFFffFF;
	float value = 0;
	void* world = nullptr;
};

i32 addTestNativeType(LumScript::Module& module, StringView name, StringView id) {
	for (i32 i = 0; i < module.native_types.size(); ++i) {
		if (equalStrings(module.native_types[i].name, name)) return i;
	}
	LumScript::NativeTypeDecl& type = module.native_types.emplace();
	type.name = module.copyName(name);
	type.id = module.copyName(id);
	return module.native_types.size() - 1;
}

bool nativeSetFloatInput(Span<const LumScript::Value> args, LumScript::Value*, void* userdata) {
	NativeEngineImportState* state = (NativeEngineImportState*)userdata;
	state->entity = args[0].i;
	state->input = (u32)args[1].i;
	state->value = args[2].f;
	return true;
}

bool nativeUseWorld(Span<const LumScript::Value> args, LumScript::Value*, void* userdata) {
	NativeEngineImportState* state = (NativeEngineImportState*)userdata;
	state->world = args[0].ptr;
	return true;
}

bool resolveTestEngineImport(LumScript::Module& module, StringView path, StringView alias, StringView* source, void* userdata) {
	*source = {};
	if (equalStrings(path, "engine:entity")) {
		LumScript::NativeTypeDecl& type = module.native_types.emplace();
		type.name = module.makeQualifiedName(alias, "Entity");
		type.id = "engine:entity/Entity";
		return true;
	}
	if (equalStrings(path, "engine:world")) {
		const i32 world_type_idx = addTestNativeType(module, module.makeQualifiedName(alias, "World"), "engine:world/World");
		LumScript::TypeRef params[] = {
			LumScript::TypeRef(LumScript::TypeRef::NATIVE, module.native_types[world_type_idx].id, world_type_idx)
		};
		LumScript::addNativeFunction(module, module.makeQualifiedName(alias, "useWorld"), LumScript::TypeRef(LumScript::TypeRef::VOID), Span<const LumScript::TypeRef>(params), &nativeUseWorld, userdata);
		return true;
	}
	if (equalStrings(path, "engine:animator")) {
		const i32 entity_type_idx = addTestNativeType(module, "entity.Entity", "engine:entity/Entity");
		LumScript::TypeRef params[] = {
			LumScript::TypeRef(LumScript::TypeRef::NATIVE, module.native_types[entity_type_idx].id, entity_type_idx),
			LumScript::TypeRef(LumScript::TypeRef::I32),
			LumScript::TypeRef(LumScript::TypeRef::F32)
		};
		LumScript::addNativeFunction(module, module.makeQualifiedName(alias, "setFloatInput"), LumScript::TypeRef(LumScript::TypeRef::VOID), Span<const LumScript::TypeRef>(params), &nativeSetFloatInput, userdata);
		return true;
	}
	if (equalStrings(path, "engine:world_probe")) {
		const i32 world_type_idx = addTestNativeType(module, "world.World", "engine:world/World");
		LumScript::TypeRef params[] = {
			LumScript::TypeRef(LumScript::TypeRef::NATIVE, module.native_types[world_type_idx].id, world_type_idx)
		};
		LumScript::addNativeFunction(module, module.makeQualifiedName(alias, "useWorld"), LumScript::TypeRef(LumScript::TypeRef::VOID), Span<const LumScript::TypeRef>(params), &nativeUseWorld, userdata);
		return true;
	}
	return false;
}

LumScript::Value makeVec3(float x, float y, float z) {
	return LumScript::Runtime::makeVec3({LumScript::TypeRef::STRUCT, StringView("Vec3"), 0}, x, y, z);
}

bool nativeAdd(Span<const LumScript::Value> args, LumScript::Value* result, void*) {
	*result = LumScript::Runtime::makeI32(args[0].i + args[1].i);
	return true;
}

bool testRuntimeAddVec3() {
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, LumScriptTests::SAMPLE, diagnostics));

	LumScript::Value args[] = {
		makeVec3(10, 20, 30),
		makeVec3(40, 50, 60)
	};

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("add", Span<const LumScript::Value>(args), &result, diagnostics));
	ASSERT_FLOAT_EQ(50, result.composite[0]);
	ASSERT_FLOAT_EQ(70, result.composite[1]);
	ASSERT_FLOAT_EQ(90, result.composite[2]);
	return true;
}

bool testNativeFunctionCall() {
	const char* source = R"(
		fn main() : i32 {
			return native_add(20, 22);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::parse(module, source, diagnostics));

	LumScript::TypeRef params[] = {LumScript::TypeRef(LumScript::TypeRef::I32), LumScript::TypeRef(LumScript::TypeRef::I32)};
	LumScript::addNativeFunction(module, "native_add", LumScript::TypeRef(LumScript::TypeRef::I32), Span<const LumScript::TypeRef>(params), &nativeAdd);
	ASSERT_TRUE(LumScript::typecheck(module, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(42, result.i);
	return true;
}

bool testStringConcatenationRuntime() {
	const char* source = R"(
		fn greet(name : string) : string {
			const hello = "Hello";
			return hello + ", " + name;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value arg = LumScript::Runtime::makeString("Lumix");
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("greet", Span<const LumScript::Value>(&arg, 1), &result, diagnostics));
	ASSERT_TRUE(equalStrings(result.string, "Hello, Lumix"));
	return true;
}

bool testRuntimeCasts() {
	const char* source = R"(
		fn to_f32() : f32 {
			const x : i32 = 10;
			return x as f32;
		}

		fn to_i32() : i32 {
			const x : f32 = 12.75;
			return x as i32;
		}

		fn to_bool() : bool {
			const x : i32 = 1;
			return x as bool;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("to_f32", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_FLOAT_EQ(10, result.f);
	ASSERT_TRUE(runtime.call("to_i32", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(12, result.i);
	ASSERT_TRUE(runtime.call("to_bool", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_TRUE(result.b);
	return true;
}

bool testBuiltinLogErrorString() {
	const char* source = R"(
		fn main() : void {
			logError("Hello " + "Lumix");
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compileWithBuiltins(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), nullptr, diagnostics));
	return true;
}

bool testBuiltinLogErrorRejectsNonString() {
	const char* source = R"(
		fn main() : void {
			logError(42);
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(!LumScript::compileWithBuiltins(module, source, diagnostics));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testRuntimeMainWithLoop() {
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, LumScriptTests::SAMPLE, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::RuntimeOptions options;
	options.max_steps = 1000;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), nullptr, diagnostics, options));
	return true;
}

bool testStepLimit() {
	const char* source = R"(
		fn main() : void {
			while true {
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::RuntimeOptions options;
	options.max_steps = 32;
	ASSERT_TRUE(!runtime.call("main", Span<const LumScript::Value>(), nullptr, diagnostics, options));
	ASSERT_TRUE(diagnostics.has_error);
	return true;
}

bool testRuntimeBlockScope() {
	const char* source = R"(
		fn scoped() : i32 {
			var a = 1;
			{
				var a = 5;
				a = a + 1;
			}
			return a;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("scoped", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(1, result.i);
	return true;
}

bool testGlobalVariablesRuntime() {
	const char* source = R"(
		var counter : i32 = 1;
		const step = 2;

		fn increment() : i32 {
			counter += step;
			return counter;
		}

		fn read_counter() : i32 {
			return counter;
		}

		fn shadow_counter() : i32 {
			var counter : i32 = 100;
			counter += 1;
			return counter;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("read_counter", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(1, result.i);
	ASSERT_TRUE(runtime.call("increment", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(3, result.i);
	ASSERT_TRUE(runtime.call("increment", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(5, result.i);
	ASSERT_TRUE(runtime.call("read_counter", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(5, result.i);
	ASSERT_TRUE(runtime.call("shadow_counter", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(101, result.i);
	ASSERT_TRUE(runtime.call("read_counter", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(5, result.i);
	return true;
}

bool testIfElse() {
	const char* source = R"(
		fn classify(v : i32) : i32 {
			if v > 10 {
				return 2;
			} else if v > 0 {
				return 1;
			} else {
				return 0;
			}
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value arg = LumScript::Runtime::makeI32(4);
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("classify", Span<const LumScript::Value>(&arg, 1), &result, diagnostics));
	ASSERT_EQ(1, result.i);

	LumScript::Value arg2 = LumScript::Runtime::makeI32(11);
	ASSERT_TRUE(runtime.call("classify", Span<const LumScript::Value>(&arg2, 1), &result, diagnostics));
	ASSERT_EQ(2, result.i);

	LumScript::Value arg3 = LumScript::Runtime::makeI32(-1);
	ASSERT_TRUE(runtime.call("classify", Span<const LumScript::Value>(&arg3, 1), &result, diagnostics));
	ASSERT_EQ(0, result.i);
	return true;
}

bool testMatchRuntime() {
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

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	LumScript::Value arg = LumScript::Runtime::makeI32(0);
	ASSERT_TRUE(runtime.call("enum_match", Span<const LumScript::Value>(&arg, 1), &result, diagnostics));
	ASSERT_EQ(1, result.i);
	arg = LumScript::Runtime::makeI32(2);
	ASSERT_TRUE(runtime.call("enum_match", Span<const LumScript::Value>(&arg, 1), &result, diagnostics));
	ASSERT_EQ(2, result.i);
	arg = LumScript::Runtime::makeI32(5);
	ASSERT_TRUE(runtime.call("range_match", Span<const LumScript::Value>(&arg, 1), &result, diagnostics));
	ASSERT_EQ(1, result.i);
	arg = LumScript::Runtime::makeI32(42);
	ASSERT_TRUE(runtime.call("range_match", Span<const LumScript::Value>(&arg, 1), &result, diagnostics));
	ASSERT_EQ(2, result.i);
	return true;
}

bool testShortCircuiting() {
	const char* source = R"(
		fn spin() : bool {
			while true {
			}
			return true;
		}

		fn left_false() : bool {
			return false and spin();
		}

		fn left_true() : bool {
			return true or spin();
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::RuntimeOptions options;
	options.max_steps = 64;
	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("left_false", Span<const LumScript::Value>(), &result, diagnostics, options));
	ASSERT_TRUE(!result.b);

	LumScript::Diagnostics diagnostics2(getGlobalAllocator());
	LumScript::Runtime runtime2(module, getGlobalAllocator());
	ASSERT_TRUE(runtime2.call("left_true", Span<const LumScript::Value>(), &result, diagnostics2, options));
	ASSERT_TRUE(result.b);
	return true;
}

bool testAliasedImportRuntime() {
	const char* main_source = R"(
		import "math" as math
		import "state" as state

		fn main() : i32 {
			const v : math.Vec2 = math.Vec2 { 20, 22 };
			if state.is_running(state.State.Running) {
				return math.sum(v);
			}
			return 0;
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
	const char* state_source = R"(
		enum State {
			Idle,
			Running
		};

		fn is_running(state : State) : bool {
			return state == .Running;
		}
	)";
	LumScriptImportFile files_storage[] = {
		{ "math", math_source },
		{ "state", state_source }
	};
	LumScriptImportFiles files = { files_storage, lengthOf(files_storage) };
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, main_source, diagnostics, resolveLumScriptImport, &files));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(42, result.i);
	return true;
}

bool testNativeEngineImportRuntime() {
	const char* source = R"(
		import "engine:entity" as entity
		import "engine:animator" as animator

		fn update(e : entity.Entity) : void {
			animator.setFloatInput(e, 3, 12.5);
		}
	)";
	NativeEngineImportState state;
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics, resolveTestEngineImport, &state));
	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value entity;
	entity.type = LumScript::TypeRef(LumScript::TypeRef::NATIVE, "engine:entity/Entity", 0);
	entity.i = 42;
	ASSERT_TRUE(runtime.call("update", Span<const LumScript::Value>(&entity, 1), nullptr, diagnostics));
	ASSERT_EQ(42, state.entity);
	ASSERT_EQ((u32)3, state.input);
	ASSERT_FLOAT_EQ(12.5f, state.value);
	return true;
}

bool testNativeWorldImportRuntime() {
	const char* source = R"(
		import "engine:world" as world

		fn init(w : world.World) : void {
			w.useWorld();
		}
	)";
	NativeEngineImportState state;
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics, resolveTestEngineImport, &state));
	LumScript::Runtime runtime(module, getGlobalAllocator());
	int fake_world;
	LumScript::Value world;
	world.type = LumScript::TypeRef(LumScript::TypeRef::NATIVE, "engine:world/World", 0);
	world.ptr = &fake_world;
	ASSERT_TRUE(runtime.call("init", Span<const LumScript::Value>(&world, 1), nullptr, diagnostics));
	ASSERT_TRUE(state.world == &fake_world);
	return true;
}

bool testRefParameterMutatesCaller() {
	const char* source = R"(
		fn increment(v : ref i32) : void {
			v += 1;
		}

		fn main() : i32 {
			var x : i32 = 10;
			increment(ref x);
			return x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(11, result.i);
	return true;
}

bool testDeferRunsAtScopeExit() {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 1;
			{
				defer x += 3;
				x += 1;
			}
			return x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(5, result.i);
	return true;
}

bool testDeferRunsInLifoOrder() {
	const char* source = R"(
		fn main() : i32 {
			var x : i32 = 1;
			{
				defer x += 1;
				defer x *= 2;
			}
			return x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(3, result.i);
	return true;
}

bool testDeferRunsOnEarlyReturn() {
	const char* source = R"(
		fn apply(v : ref i32) : void {
			defer v += 1;
			return;
		}

		fn main() : i32 {
			var x : i32 = 10;
			apply(ref x);
			return x;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(11, result.i);
	return true;
}

bool testNullableNullBranchRuntime() {
	const char* source = R"(
		fn main() : i32 {
			var x : ?i32 = null;
			if x != null {
				return x;
			}
			return 42;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(42, result.i);
	return true;
}

bool testNullableNonNullBranchRuntime() {
	const char* source = R"(
		fn main() : i32 {
			var x : ?i32 = 7;
			if x != null {
				return x;
			}
			return 0;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(7, result.i);
	return true;
}

bool testExtendedScalarTypesRuntime() {
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

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("main", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(211, result.i);
	return true;
}

bool testIntegerOverflowWraparoundRuntime() {
	const char* source = R"(
		fn u8_add_wrap() : i32 {
			const a : u8 = 255 as u8;
			const b : u8 = (a + 1 as u8) as u8;
			return b as i32;
		}

		fn i8_add_wrap() : i32 {
			const a : i8 = 127 as i8;
			const b : i8 = (a + 1 as i8) as i8;
			return b as i32;
		}

		fn u8_add_assign_wrap() : i32 {
			var x : u8 = 255 as u8;
			x += 1 as u8;
			return x as i32;
		}

		fn cast_i8_wrap() : i32 {
			const x : i32 = 255;
			return (x as i8) as i32;
		}

		fn cast_u8_wrap() : i32 {
			const x : i32 = 256;
			return (x as u8) as i32;
		}
	)";

	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("u8_add_wrap", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(0, result.i);
	ASSERT_TRUE(runtime.call("i8_add_wrap", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(-128, result.i);
	ASSERT_TRUE(runtime.call("u8_add_assign_wrap", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(0, result.i);
	ASSERT_TRUE(runtime.call("cast_i8_wrap", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(-1, result.i);
	ASSERT_TRUE(runtime.call("cast_u8_wrap", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(0, result.i);
	return true;
}

bool testUntypedLiteralsRuntime() {
	const char* source = R"(
		struct Pair {
			x : u8;
			y : f64;
		};

		fn vec3_sum() : f32 {
			const v : Vec3 = { 1, 2, 3 };
			return v.x + v.y + v.z;
		}

		fn integer_widths() : i32 {
			const a : i8 = 10;
			const b : u8 = 20;
			const c : i16 = 30;
			const d : u16 = 40;
			const e : i64 = 50;
			const f : u64 = 60;
			const pair = Pair { 255, 2.5 };
			return a as i32 + b as i32 + c as i32 + d as i32 + e as i32 + f as i32 + pair.x as i32 + pair.y as i32;
		}

		fn return_f64() : f64 {
			return 1.5;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("vec3_sum", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_FLOAT_EQ(6, result.f);
	ASSERT_TRUE(runtime.call("integer_widths", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ(467, result.i);
	ASSERT_TRUE(runtime.call("return_f64", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_FLOAT_EQ(1.5f, (float)result.d);
	return true;
}

bool testInputEventIterationRuntime() {
	const char* source = R"(
		import "engine:input" as input

		fn read(inputs : input.InputSystem) : i32 {
			const count = inputs.getEventCount();
			if count == 0 {
				return -1;
			}
			const e = inputs.getEvent(0);
			if e.getType() == input.BUTTON() {
				if e.isDown() {
					return e.getKeyId() + e.getDeviceIndex();
				}
			}
			return -2;
		}

		fn key_w() : i32 {
			return input.Keycode.W;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	const bool ok = LumScript::compile(module, source, diagnostics, resolveRealEngineImport, nullptr);
	if (!ok) logError(diagnostics.message);
	ASSERT_TRUE(ok);

	FakeInputDevice device;
	FakeInputSystem input(getGlobalAllocator());
	InputSystem::Event event = {};
	event.type = InputSystem::Event::BUTTON;
	event.device = &device;
	event.data.button.key_id = 40;
	event.data.button.down = true;
	input.events.push(event);

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value arg;
	arg.type = LumScript::TypeRef(LumScript::TypeRef::NATIVE, "engine:input/InputSystem", -1);
	arg.ptr = &input;
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("read", Span<const LumScript::Value>(&arg, 1), &result, diagnostics));
	ASSERT_EQ(42, result.i);
	ASSERT_TRUE(runtime.call("key_w", Span<const LumScript::Value>(), &result, diagnostics));
	ASSERT_EQ((i32)'W', result.i);
	return true;
}

bool testUpdateReceivesTimeDeltaRuntime() {
	const char* source = R"(
		fn update(dt : f32) : f32 {
			return dt * 2.0;
		}
	)";
	LumScript::Module module(getGlobalAllocator());
	LumScript::Diagnostics diagnostics(getGlobalAllocator());
	ASSERT_TRUE(LumScript::compile(module, source, diagnostics));

	LumScript::Runtime runtime(module, getGlobalAllocator());
	LumScript::Value arg = LumScript::Runtime::makeF32(0.125f);
	LumScript::Value result;
	ASSERT_TRUE(runtime.call("update", Span<const LumScript::Value>(&arg, 1), &result, diagnostics));
	ASSERT_FLOAT_EQ(0.25f, result.f);
	return true;
}

} // anonymous namespace

void runLumScriptRuntimeTests() {
	RUN_TEST(testRuntimeAddVec3);
	RUN_TEST(testNativeFunctionCall);
	RUN_TEST(testStringConcatenationRuntime);
	RUN_TEST(testRuntimeCasts);
	RUN_TEST(testBuiltinLogErrorString);
	RUN_TEST(testBuiltinLogErrorRejectsNonString);
	RUN_TEST(testRuntimeMainWithLoop);
	RUN_TEST(testStepLimit);
	RUN_TEST(testRuntimeBlockScope);
	RUN_TEST(testGlobalVariablesRuntime);
	RUN_TEST(testIfElse);
	RUN_TEST(testMatchRuntime);
	RUN_TEST(testShortCircuiting);
	RUN_TEST(testAliasedImportRuntime);
	RUN_TEST(testNativeEngineImportRuntime);
	RUN_TEST(testNativeWorldImportRuntime);
	RUN_TEST(testRefParameterMutatesCaller);
	RUN_TEST(testDeferRunsAtScopeExit);
	RUN_TEST(testDeferRunsInLifoOrder);
	RUN_TEST(testDeferRunsOnEarlyReturn);
	RUN_TEST(testNullableNullBranchRuntime);
	RUN_TEST(testNullableNonNullBranchRuntime);
	RUN_TEST(testExtendedScalarTypesRuntime);
	RUN_TEST(testIntegerOverflowWraparoundRuntime);
	RUN_TEST(testUntypedLiteralsRuntime);
	RUN_TEST(testInputEventIterationRuntime);
	RUN_TEST(testUpdateReceivesTimeDeltaRuntime);
}
