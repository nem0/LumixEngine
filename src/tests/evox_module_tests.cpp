#include "../../external/evox/arena.h"
#include "core/log.h"
#include "core/stream.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/world.h"
#include "evox/capi.h"
#include "evox/evox_module.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

struct EvoxTestHost {
	EvoxTestHost() { ex_default_arena_create(&host.arena); }
	~EvoxTestHost() { ex_default_arena_destroy(&host.arena); }

	ex_host host = {};
};

bool testEvoxModuleSerialization() {
	// The type handles used by the module point into the bytecode, so keep the
	// script alive until both the source and destination worlds are destroyed.
	EvoxTestHost script_host;
	const char* source = R"(
		struct Data {}
		#[Data{}]
		struct TestData { value : i32; }
	)";
	ex_module* script = ex_module_create(&script_host.host);
	ASSERT_TRUE(script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(script, {source, (i64)stringLength(source)}, {"test.evox", 8}, nullptr, nullptr));
	ex_bytecode* bytecode = ex_bytecode_compile(script, &script_host.host, nullptr);
	ASSERT_TRUE(bytecode);

	const ex_type* data_type = nullptr;
	for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
		const ex_type* type = ex_bytecode_type(bytecode, i);
		const ex_string_view name = ex_type_get_name(type);
		if (StringView(name.begin, (u64)name.length) == "TestData") data_type = type;
	}
	ASSERT_TRUE(data_type);

	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	// The Evox system requests its root script while creating a world. Give
	// the test filesystem a root mount so a missing script is a normal load
	// failure instead of an invalid virtual path.
	engine->getFileSystem().mount(".", "");
	// No system initialization is needed for module serialization. Skipping it
	// also keeps the fixture independent of filesystem-backed script loading.
	World& source_world = engine->createWorld();
	EvoxModule* source_module = (EvoxModule*)source_world.getModule("evox");
	ASSERT_TRUE(source_module);
	source_module->setEvoxDataTypes(Span<const ex_type*>(&data_type, 1));
	const EntityRef source_entities[] = {
		source_world.createEntity({}, Quat::IDENTITY),
		source_world.createEntity({}, Quat::IDENTITY),
		source_world.createEntity({}, Quat::IDENTITY)
	};
	const i32 source_values[] = { 111, 222, 333 };
	for (u32 i = 0; i < lengthOf(source_entities); ++i) {
		source_module->createEvox(source_entities[i]);
		ASSERT_TRUE(source_module->addEvoxData(source_entities[i], data_type));
		i32* value = (i32*)source_module->getEvoxData(source_entities[i], data_type);
		ASSERT_TRUE(value);
		*value = source_values[i];
	}

	OutputMemoryStream blob(getGlobalAllocator());
	source_module->serialize(blob);

	World& target_world = engine->createWorld();
	EvoxModule* target_module = (EvoxModule*)target_world.getModule("evox");
	ASSERT_TRUE(target_module);
	const EntityRef target_entities[] = {
		target_world.createEntity({}, Quat::IDENTITY),
		target_world.createEntity({}, Quat::IDENTITY),
		target_world.createEntity({}, Quat::IDENTITY)
	};
	EntityMap entity_map(getGlobalAllocator());
	for (u32 i = 0; i < lengthOf(source_entities); ++i) {
		entity_map.set(source_entities[i], target_entities[i]);
	}

	// Deserialization must retain data until the script types become available.
	InputMemoryStream input(blob);
	target_module->deserialize(input, entity_map, 2);
	ASSERT_EQ(0, target_module->getEvoxDataCount(target_entities[0]));
	target_module->setEvoxDataTypes(Span<const ex_type*>(&data_type, 1));
	for (u32 i = 0; i < lengthOf(target_entities); ++i) {
		ASSERT_EQ(1, target_module->getEvoxDataCount(target_entities[i]));
		const i32* restored = (const i32*)target_module->getEvoxData(target_entities[i], data_type);
		ASSERT_TRUE(restored);
		ASSERT_EQ(source_values[i], *restored);
	}

	// Destroying an entity must remove its data and compact storage without
	// corrupting the other entities.
	target_world.destroyEntity(target_entities[1]);
	const i32* first = (const i32*)target_module->getEvoxData(target_entities[0], data_type);
	const i32* last = (const i32*)target_module->getEvoxData(target_entities[2], data_type);
	ASSERT_TRUE(first && last);
	ASSERT_EQ(111, *first);
	ASSERT_EQ(333, *last);

	engine->destroyWorld(target_world);
	engine->destroyWorld(source_world);
	engine.reset();
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(script);
	return true;
}

} // namespace

void runEvoxModuleTests() {
	RUN_TEST(testEvoxModuleSerialization);
}
