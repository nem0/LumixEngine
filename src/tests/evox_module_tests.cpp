#include "../../external/evox/arena.h"
#include "core/crt.h"
#include "core/log.h"
#include "core/stream.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/resource.h"
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

static constexpr const char* EVOX_DATA_ATTRIBUTES_SOURCE = "struct Data {} struct Owner {}";
static constexpr const char* EVOX_ENTITY_SOURCE = "struct Entity { index : i32; world : cptr; }";
static constexpr const char* EVOX_SAME_DATA_SOURCE = "import \"core:attributes\" #[Data{}] struct Same { value : i32; }";

static int resolveTestImport(void*, ex_string_view path, ex_string_view, ex_string_view* source) {
	const StringView requested(path.begin, (u64)path.length);
	if (requested == "core:attributes") {
		*source = {EVOX_DATA_ATTRIBUTES_SOURCE, (i64)stringLength(EVOX_DATA_ATTRIBUTES_SOURCE)};
		return 1;
	}
	if (requested == "core:entity") {
		*source = {EVOX_ENTITY_SOURCE, (i64)stringLength(EVOX_ENTITY_SOURCE)};
		return 1;
	}
	if (requested == "a" || requested == "b") {
		*source = {EVOX_SAME_DATA_SOURCE, (i64)stringLength(EVOX_SAME_DATA_SOURCE)};
		return 1;
	}
	return 0;
}

// Serve the compiled root resource through the normal asynchronous loading path.
// This exercises EvoxSystem's type discovery instead of injecting type handles.
struct EvoxDiscoveryFileSystem : FileSystem {
	explicit EvoxDiscoveryFileSystem(const char* source)
		: content(getGlobalAllocator())
		, resource_path(".lumix/resources/", Path("scripts/main.evox").getHash(), ".res")
	{
		content.write(CompiledResourceHeader{});
		content.write(source, stringLength(source));
	}

	const char* getEngineDataDir() override { return ""; }
	u64 getLastModified(StringView) override { return 0; }
	bool copyFile(StringView, StringView) override { return false; }
	bool moveFile(StringView, StringView) override { return false; }
	bool deleteFile(StringView) override { return false; }
	bool fileExists(StringView path) override { return Path(path) == resource_path; }
	bool dirExists(StringView) override { return false; }
	FileIterator* createFileIterator(StringView) override { return nullptr; }
	bool open(StringView, os::InputFile&) override { return false; }
	bool open(StringView, os::OutputFile&) override { return false; }
	void mount(StringView, StringView) override {}
	Path getFullPath(StringView path) const override { return Path(path); }
	bool saveContentSync(const Path&, Span<const u8>) override { return false; }
	bool getContentSync(const Path& path, OutputMemoryStream& output) override {
		if (path != Path("engine/scripts/core/attributes.evox")) return false;
		output.write(EVOX_DATA_ATTRIBUTES_SOURCE, stringLength(EVOX_DATA_ATTRIBUTES_SOURCE));
		return true;
	}
	bool hasWork() override { return pending; }
	AsyncHandle getContent(const Path& path, const ContentCallback& cb) override {
		ASSERT(!pending);
		callback = cb;
		found = path == resource_path;
		pending = true;
		return AsyncHandle(0);
	}
	void cancel(AsyncHandle) override { pending = false; }
	void processCallbacks() override {
		if (!pending) return;
		pending = false;
		callback.invoke(Span<const u8>(content.data(), content.size()), found);
	}

	OutputMemoryStream content;
	Path resource_path;
	ContentCallback callback;
	bool pending = false;
	bool found = false;
};

bool testEvoxDataTypeDiscovery() {
	const char* source = R"(
		import "core:attributes"
		struct Unmarked { value : i32; }
		#[Data{}]
		struct Supported { value : i32; }
		#[Data{}]
		struct Mixed { values : [2]i32; tail : i32; }
		struct Nested { values : []i32; tail : i32; }
		#[Data{}]
		struct NestedData { nested : Nested; }
		#[Data{}]
		struct Unsupported { values : [2]i32; slice : []i32; }
	)";
	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	args.file_system = UniquePtr<EvoxDiscoveryFileSystem>::create(getGlobalAllocator(), source).move();
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	World& world = engine->createWorld();
	EvoxModule* module = (EvoxModule*)world.getModule("evox");
	ASSERT_TRUE(module);
	ASSERT_TRUE(!module->isReady());
	engine->getFileSystem().processCallbacks();
	ASSERT_TRUE(module->isReady());
	const Span<const ex_type*> types = module->getEvoxDataTypes();
	ASSERT_EQ(4, types.length());
	const EntityRef entity = world.createEntity({}, Quat::IDENTITY);
	module->createEvox(entity);
	bool supported = false, mixed = false, nested = false, unsupported = false;
	for (const ex_type* type : types) {
		const ex_string_view ex_name = ex_type_get_name(type);
		const StringView name(ex_name.begin, (u64)ex_name.length);
		ASSERT_TRUE(name != "scripts/main.evox.Unmarked" && name != "core:attributes.Data" && name != "scripts/main.evox.Nested");
		if (name == "scripts/main.evox.Supported") supported = true;
		if (name == "scripts/main.evox.Mixed") mixed = true;
		if (name == "scripts/main.evox.NestedData") nested = true;
		if (name == "scripts/main.evox.Unsupported") unsupported = true;
		ASSERT_TRUE(module->addEvoxData(entity, type));
	}
	ASSERT_TRUE(supported && mixed && nested && unsupported);
	ASSERT_EQ(4, module->getEvoxDataCount(entity));

	// A world created after compilation must receive the same discovered types.
	World& second_world = engine->createWorld();
	EvoxModule* second_module = (EvoxModule*)second_world.getModule("evox");
	ASSERT_TRUE(second_module && second_module->isReady());
	const EntityRef second_entity = second_world.createEntity({}, Quat::IDENTITY);
	second_module->createEvox(second_entity);
	for (const ex_type* type : types) ASSERT_TRUE(second_module->addEvoxData(second_entity, type));
	ASSERT_EQ(4, second_module->getEvoxDataCount(second_entity));
	engine->destroyWorld(second_world);
	engine->destroyWorld(world);
	return true;
}

bool testEvoxModuleSerialization() {
	// The type handles used by the module point into the bytecode, so keep the
	// script alive until both the source and destination worlds are destroyed.
	EvoxTestHost script_host;
	const char* source = R"(
		import "core:attributes"
		#[Data{}]
		struct TestData { value : i32; second : i32; }
	)";
	ex_module* script = ex_module_create(&script_host.host);
	ASSERT_TRUE(script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(script, {source, (i64)stringLength(source)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ex_bytecode* bytecode = ex_bytecode_compile(script, &script_host.host, nullptr);
	ASSERT_TRUE(bytecode);

	const ex_type* data_type = nullptr;
	for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
		const ex_type* type = ex_bytecode_type(bytecode, i);
		const ex_string_view name = ex_type_get_name(type);
		if (StringView(name.begin, (u64)name.length) == "test.evox.TestData") data_type = type;
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
	const i32 source_second_values[] = { 444, 555, 666 };
	const u32 second_offset = ex_type_struct_field_offset(data_type, 1);
	for (u32 i = 0; i < lengthOf(source_entities); ++i) {
		source_module->createEvox(source_entities[i]);
		ASSERT_TRUE(source_module->addEvoxData(source_entities[i], data_type));
		u8* value = (u8*)source_module->getEvoxData(source_entities[i], data_type);
		ASSERT_TRUE(value);
		memcpy(value, &source_values[i], sizeof(source_values[i]));
		memcpy(value + second_offset, &source_second_values[i], sizeof(source_second_values[i]));
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
		const u8* restored = (const u8*)target_module->getEvoxData(target_entities[i], data_type);
		ASSERT_TRUE(restored);
		i32 restored_value;
		i32 restored_second_value;
		memcpy(&restored_value, restored, sizeof(restored_value));
		memcpy(&restored_second_value, restored + second_offset, sizeof(restored_second_value));
		ASSERT_EQ(source_values[i], restored_value);
		ASSERT_EQ(source_second_values[i], restored_second_value);
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

bool testEvoxModuleSerializationSchemaMigration() {
	EvoxTestHost source_script_host;
	EvoxTestHost target_script_host;
	const char* source = R"(
		import "core:attributes"
		struct Nested { first : i32; second : i32; }
		#[Data{}]
		struct TestData { first : i32; nested : Nested; changed : i32; removed : i32; }
	)";
	const char* target = R"(
		import "core:attributes"
		struct Nested { first : i32; added : i32; second : i32; }
		#[Data{}]
		struct TestData { first : i32; nested : Nested; changed : f32; added : i32; }
	)";

	ex_module* source_script = ex_module_create(&source_script_host.host);
	ex_module* target_script = ex_module_create(&target_script_host.host);
	ASSERT_TRUE(source_script && target_script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(source_script, {source, (i64)stringLength(source)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(target_script, {target, (i64)stringLength(target)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ex_bytecode* source_bytecode = ex_bytecode_compile(source_script, &source_script_host.host, nullptr);
	ex_bytecode* target_bytecode = ex_bytecode_compile(target_script, &target_script_host.host, nullptr);
	ASSERT_TRUE(source_bytecode && target_bytecode);

	auto findTestData = [](ex_bytecode* bytecode) -> const ex_type* {
		for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
			const ex_type* type = ex_bytecode_type(bytecode, i);
			const ex_string_view name = ex_type_get_name(type);
			if (StringView(name.begin, (u64)name.length) == "test.evox.TestData") return type;
		}
		return nullptr;
	};
	const ex_type* source_type = findTestData(source_bytecode);
	const ex_type* target_type = findTestData(target_bytecode);
	ASSERT_TRUE(source_type && target_type);

	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	engine->getFileSystem().mount(".", "");

	World& source_world = engine->createWorld();
	EvoxModule* source_module = (EvoxModule*)source_world.getModule("evox");
	ASSERT_TRUE(source_module);
	source_module->setEvoxDataTypes(Span<const ex_type*>(&source_type, 1));
	const EntityRef source_entity = source_world.createEntity({}, Quat::IDENTITY);
	source_module->createEvox(source_entity);
	ASSERT_TRUE(source_module->addEvoxData(source_entity, source_type));
	u8* source_value = (u8*)source_module->getEvoxData(source_entity, source_type);
	ASSERT_TRUE(source_value);
	const i32 first = 123;
	const i32 nested_first = 456;
	const i32 nested_second = 789;
	const i32 changed = 321;
	const i32 removed = 654;
	const u32 source_nested_offset = ex_type_struct_field_offset(source_type, 1);
	const ex_type* source_nested_type = ex_type_struct_field_type(source_type, 1);
	memcpy(source_value + ex_type_struct_field_offset(source_type, 0), &first, sizeof(first));
	memcpy(source_value + source_nested_offset + ex_type_struct_field_offset(source_nested_type, 0), &nested_first, sizeof(nested_first));
	memcpy(source_value + source_nested_offset + ex_type_struct_field_offset(source_nested_type, 1), &nested_second, sizeof(nested_second));
	memcpy(source_value + ex_type_struct_field_offset(source_type, 2), &changed, sizeof(changed));
	memcpy(source_value + ex_type_struct_field_offset(source_type, 3), &removed, sizeof(removed));

	OutputMemoryStream blob(getGlobalAllocator());
	source_module->serialize(blob);

	World& target_world = engine->createWorld();
	EvoxModule* target_module = (EvoxModule*)target_world.getModule("evox");
	ASSERT_TRUE(target_module);
	target_module->setEvoxDataTypes(Span<const ex_type*>(&target_type, 1));
	const EntityRef target_entity = target_world.createEntity({}, Quat::IDENTITY);
	EntityMap entity_map(getGlobalAllocator());
	entity_map.set(source_entity, target_entity);
	InputMemoryStream input(blob);
	target_module->deserialize(input, entity_map, 2);

	const u8* restored = (const u8*)target_module->getEvoxData(target_entity, target_type);
	ASSERT_TRUE(restored);
	i32 restored_first;
	i32 restored_nested_first;
	i32 restored_nested_added;
	i32 restored_nested_second;
	f32 restored_changed;
	i32 restored_added;
	const u32 target_nested_offset = ex_type_struct_field_offset(target_type, 1);
	const ex_type* target_nested_type = ex_type_struct_field_type(target_type, 1);
	memcpy(&restored_first, restored + ex_type_struct_field_offset(target_type, 0), sizeof(restored_first));
	memcpy(&restored_nested_first, restored + target_nested_offset + ex_type_struct_field_offset(target_nested_type, 0), sizeof(restored_nested_first));
	memcpy(&restored_nested_added, restored + target_nested_offset + ex_type_struct_field_offset(target_nested_type, 1), sizeof(restored_nested_added));
	memcpy(&restored_nested_second, restored + target_nested_offset + ex_type_struct_field_offset(target_nested_type, 2), sizeof(restored_nested_second));
	memcpy(&restored_changed, restored + ex_type_struct_field_offset(target_type, 2), sizeof(restored_changed));
	memcpy(&restored_added, restored + ex_type_struct_field_offset(target_type, 3), sizeof(restored_added));
	ASSERT_EQ(first, restored_first);
	ASSERT_EQ(nested_first, restored_nested_first);
	ASSERT_EQ(0, restored_nested_added);
	ASSERT_EQ(nested_second, restored_nested_second);
	ASSERT_EQ(0, restored_changed);
	ASSERT_EQ(0, restored_added);

	engine->destroyWorld(target_world);
	engine->destroyWorld(source_world);
	engine.reset();
	ex_bytecode_destroy(target_bytecode);
	ex_bytecode_destroy(source_bytecode);
	ex_module_destroy(target_script);
	ex_module_destroy(source_script);
	return true;
}

bool testEvoxModuleEnumSerialization(bool migrate_schema) {
	EvoxTestHost source_script_host;
	EvoxTestHost target_script_host;
	const char* source = R"(
		import "core:attributes"
		enum State { Idle = 0, Running = 10, Failed = -7 }
		struct Nested { state : State; tail : i32; flag : bool; wide : i64; pointer : cptr; }
		#[Data{}]
		struct TestData { state : State; nested : Nested; tail : i32; flag : bool; wide : i64; pointer : cptr; removed : State; after : i64; }
	)";
	const char* migrated = R"(
		import "core:attributes"
		enum State { Idle = 0, Running = 10, Failed = -7 }
		struct Nested { tail : i32; state : State; flag : bool; wide : i64; pointer : cptr; }
		#[Data{}]
		struct TestData { tail : i32; nested : Nested; state : State; flag : bool; wide : i64; pointer : cptr; after : i64; }
	)";
	const char* target = migrate_schema ? migrated : source;
	ex_module* source_script = ex_module_create(&source_script_host.host);
	ex_module* target_script = ex_module_create(&target_script_host.host);
	ASSERT_TRUE(source_script && target_script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(source_script, {source, (i64)stringLength(source)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(target_script, {target, (i64)stringLength(target)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ex_bytecode* source_bytecode = ex_bytecode_compile(source_script, &source_script_host.host, nullptr);
	ex_bytecode* target_bytecode = ex_bytecode_compile(target_script, &target_script_host.host, nullptr);
	ASSERT_TRUE(source_bytecode && target_bytecode);
	auto findTestData = [](ex_bytecode* bytecode) -> const ex_type* {
		for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
			const ex_type* type = ex_bytecode_type(bytecode, i);
			const ex_string_view name = ex_type_get_name(type);
			if (StringView(name.begin, (u64)name.length) == "test.evox.TestData") return type;
		}
		return nullptr;
	};
	const ex_type* source_type = findTestData(source_bytecode);
	const ex_type* target_type = findTestData(target_bytecode);
	ASSERT_TRUE(source_type && target_type);

	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	engine->getFileSystem().mount(".", "");
	World& source_world = engine->createWorld();
	World& target_world = engine->createWorld();
	EvoxModule* source_module = (EvoxModule*)source_world.getModule("evox");
	EvoxModule* target_module = (EvoxModule*)target_world.getModule("evox");
	ASSERT_TRUE(source_module && target_module);
	source_module->setEvoxDataTypes(Span<const ex_type*>(&source_type, 1));

	const i32 states[] = {0, 10, -7};
	const i32 nested_states[] = {-7, 0, 10};
	const i32 tails[] = {111, 222, 333};
	const i32 nested_tails[] = {444, 555, 666};
	const bool flags[] = {true, false, true};
	const i64 wide_values[] = {0x123456789abcdefLL, -0x123456789abcdefLL, 0x76543210abcdefLL};
	void* pointer = &source_world;
	const EntityRef target_entities[] = {
		target_world.createEntity({}, Quat::IDENTITY),
		target_world.createEntity({}, Quat::IDENTITY),
		target_world.createEntity({}, Quat::IDENTITY)
	};
	EntityMap entity_map(getGlobalAllocator());
	const u32 source_nested_offset = ex_type_struct_field_offset(source_type, 1);
	const ex_type* source_nested_type = ex_type_struct_field_type(source_type, 1);
	for (u32 i = 0; i < lengthOf(target_entities); ++i) {
		const EntityRef entity = source_world.createEntity({}, Quat::IDENTITY);
		entity_map.set(entity, target_entities[i]);
		source_module->createEvox(entity);
		ASSERT_TRUE(source_module->addEvoxData(entity, source_type));
		u8* value = (u8*)source_module->getEvoxData(entity, source_type);
		ASSERT_TRUE(value);
		// Poison padding so accidentally serializing it cannot pass as zero values.
		memset(value, 0xcd, ex_type_get_size(source_type));
		memcpy(value + ex_type_struct_field_offset(source_type, 0), &states[i], sizeof(i32));
		memcpy(value + source_nested_offset + ex_type_struct_field_offset(source_nested_type, 0), &nested_states[i], sizeof(i32));
		memcpy(value + source_nested_offset + ex_type_struct_field_offset(source_nested_type, 1), &nested_tails[i], sizeof(i32));
		memcpy(value + ex_type_struct_field_offset(source_type, 2), &tails[i], sizeof(i32));
		memcpy(value + ex_type_struct_field_offset(source_type, 3), &flags[i], sizeof(bool));
		memcpy(value + ex_type_struct_field_offset(source_type, 4), &wide_values[i], sizeof(i64));
		memcpy(value + ex_type_struct_field_offset(source_type, 5), &pointer, sizeof(pointer));
		memcpy(value + ex_type_struct_field_offset(source_type, 6), &states[i], sizeof(i32));
		memcpy(value + ex_type_struct_field_offset(source_type, 7), &wide_values[i], sizeof(i64));
		memcpy(value + source_nested_offset + ex_type_struct_field_offset(source_nested_type, 2), &flags[i], sizeof(bool));
		memcpy(value + source_nested_offset + ex_type_struct_field_offset(source_nested_type, 3), &wide_values[i], sizeof(i64));
		memcpy(value + source_nested_offset + ex_type_struct_field_offset(source_nested_type, 4), &pointer, sizeof(pointer));
	}

	OutputMemoryStream blob(getGlobalAllocator());
	source_module->serialize(blob);
	// The serialized records must be packed, with pointer slots zeroed.
	InputMemoryStream packed_input(blob);
	ASSERT_EQ(3, packed_input.read<u32>());
	for (u32 i = 0; i < 3; ++i) packed_input.read<EntityRef>();
	ASSERT_EQ(1, packed_input.read<u32>());
	Array<EntityRef> packed_entities(getGlobalAllocator());
	packed_input.readArray(&packed_entities);
	ASSERT_EQ(3, packed_entities.size());
	const u32 packed_nested_size = 4 + 4 + 1 + 8 + 8;
	const u32 packed_root_pointer_offset = 4 + packed_nested_size + 4 + 1 + 8;
	const u32 packed_nested_pointer_offset = 4 + 4 + 4 + 1 + 8;
	const u32 packed_stride = packed_root_pointer_offset + 8 + 4 + 8;
	ASSERT_TRUE(ex_type_get_size(source_type) > packed_stride);
	ASSERT_EQ((u64)(3 * packed_stride), packed_input.read<u64>());
	const u64 packed_offset = packed_input.getPosition();
	for (u32 i = 0; i < 3; ++i) {
		u8 record[packed_stride];
		packed_input.read(record, sizeof(record));
		u64 nested_pointer, root_pointer;
		memcpy(&nested_pointer, record + packed_nested_pointer_offset, sizeof(nested_pointer));
		memcpy(&root_pointer, record + packed_root_pointer_offset, sizeof(root_pointer));
		ASSERT_EQ(0, nested_pointer);
		ASSERT_EQ(0, root_pointer);
	}
	InputMemoryStream input(blob);
	target_module->deserialize(input, entity_map, 2);
	ASSERT_EQ(0, target_module->getEvoxDataCount(target_entities[0]));
	// Re-saving unresolved data must preserve the same packed representation.
	OutputMemoryStream pending_blob(getGlobalAllocator());
	target_module->serialize(pending_blob);
	ASSERT_EQ(blob.size(), pending_blob.size());
	ASSERT_EQ(0, memcmp(blob.data() + packed_offset, pending_blob.data() + packed_offset, 3 * packed_stride));
	target_module->setEvoxDataTypes(Span<const ex_type*>(&target_type, 1));

	// Check both deserialization and the packed-data path used by hot reload.
	for (u32 pass = 0; pass < 2; ++pass) {
		if (pass == 1) {
			// Also verify live pointers are discarded by the hot-reload packer.
			for (EntityRef entity : target_entities) {
				u8* value = (u8*)target_module->getEvoxData(entity, target_type);
				memcpy(value + ex_type_struct_field_offset(target_type, 5), &pointer, sizeof(pointer));
			}
			target_module->setEvoxDataTypes(Span<const ex_type*>(&target_type, 1));
		}
		const u32 nested_offset = ex_type_struct_field_offset(target_type, 1);
		const ex_type* nested_type = ex_type_struct_field_type(target_type, 1);
		for (u32 i = 0; i < lengthOf(target_entities); ++i) {
			ASSERT_EQ(1, target_module->getEvoxDataCount(target_entities[i]));
			const u8* value = (const u8*)target_module->getEvoxData(target_entities[i], target_type);
			ASSERT_TRUE(value);
			i32 state, nested_state, tail, nested_tail;
			memcpy(&state, value + ex_type_struct_field_offset(target_type, migrate_schema ? 2 : 0), sizeof(state));
			memcpy(&nested_state, value + nested_offset + ex_type_struct_field_offset(nested_type, migrate_schema ? 1 : 0), sizeof(nested_state));
			memcpy(&nested_tail, value + nested_offset + ex_type_struct_field_offset(nested_type, migrate_schema ? 0 : 1), sizeof(nested_tail));
			memcpy(&tail, value + ex_type_struct_field_offset(target_type, migrate_schema ? 0 : 2), sizeof(tail));
			ASSERT_EQ(states[i], state);
			ASSERT_EQ(nested_states[i], nested_state);
			ASSERT_EQ(tails[i], tail);
			ASSERT_EQ(nested_tails[i], nested_tail);
			bool flag, nested_flag;
			i64 wide, nested_wide, after;
			void* restored_pointer;
			void* nested_pointer;
			memcpy(&flag, value + ex_type_struct_field_offset(target_type, 3), sizeof(flag));
			memcpy(&wide, value + ex_type_struct_field_offset(target_type, 4), sizeof(wide));
			memcpy(&restored_pointer, value + ex_type_struct_field_offset(target_type, 5), sizeof(restored_pointer));
			memcpy(&after, value + ex_type_struct_field_offset(target_type, migrate_schema ? 6 : 7), sizeof(after));
			memcpy(&nested_flag, value + nested_offset + ex_type_struct_field_offset(nested_type, 2), sizeof(nested_flag));
			memcpy(&nested_wide, value + nested_offset + ex_type_struct_field_offset(nested_type, 3), sizeof(nested_wide));
			memcpy(&nested_pointer, value + nested_offset + ex_type_struct_field_offset(nested_type, 4), sizeof(nested_pointer));
			ASSERT_EQ(flags[i], flag);
			ASSERT_EQ(flags[i], nested_flag);
			ASSERT_EQ(wide_values[i], wide);
			ASSERT_EQ(wide_values[i], nested_wide);
			ASSERT_EQ(wide_values[i], after);
			ASSERT_TRUE(!restored_pointer && !nested_pointer);
		}
	}

	engine->destroyWorld(target_world);
	engine->destroyWorld(source_world);
	engine.reset();
	ex_bytecode_destroy(target_bytecode);
	ex_bytecode_destroy(source_bytecode);
	ex_module_destroy(target_script);
	ex_module_destroy(source_script);
	return true;
}

bool testEvoxModuleEnumSerializationRoundTrip() {
	return testEvoxModuleEnumSerialization(false);
}

bool testEvoxModuleEnumSerializationSchemaMigration() {
	return testEvoxModuleEnumSerialization(true);
}

bool testEvoxModuleSerializationFilteredFieldsAndOwner() {
	EvoxTestHost script_host;
	const char* source = R"(
		import "core:attributes"
		import "core:entity"
		#[Data{}]
		struct ValidData { flag : bool; #[Owner{}] owner : Entity; tail : i64; }
		#[Data{}]
		struct ArrayData { values : [2]i32; tail : i32; }
		struct Nested { values : []i32; tail : i32; }
		#[Data{}]
		struct SliceData { nested : Nested; tail : i32; }
		struct EmptyNested { values : []i32; }
		#[Data{}]
		struct UnsupportedData { values : [2]i32; nested : EmptyNested; }
	)";
	ex_module* script = ex_module_create(&script_host.host);
	ASSERT_TRUE(script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(script, {source, (i64)stringLength(source)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ex_bytecode* bytecode = ex_bytecode_compile(script, &script_host.host, nullptr);
	ASSERT_TRUE(bytecode);
	const ex_type* types[4] = {};
	for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
		const ex_type* type = ex_bytecode_type(bytecode, i);
		const ex_string_view name = ex_type_get_name(type);
		const StringView type_name(name.begin, (u64)name.length);
		if (type_name == "test.evox.ValidData") types[0] = type;
		if (type_name == "test.evox.ArrayData") types[1] = type;
		if (type_name == "test.evox.SliceData") types[2] = type;
		if (type_name == "test.evox.UnsupportedData") types[3] = type;
	}
	ASSERT_TRUE(types[0] && types[1] && types[2] && types[3]);
	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	engine->getFileSystem().mount(".", "");
	World& source_world = engine->createWorld();
	World& target_world = engine->createWorld();
	EvoxModule* source_module = (EvoxModule*)source_world.getModule("evox");
	EvoxModule* target_module = (EvoxModule*)target_world.getModule("evox");
	ASSERT_TRUE(source_module && target_module);
	source_module->setEvoxDataTypes(types);
	const EntityRef source_entity = source_world.createEntity({}, Quat::IDENTITY);
	source_module->createEvox(source_entity);
	for (const ex_type* type : types) ASSERT_TRUE(source_module->addEvoxData(source_entity, type));
	ASSERT_EQ(4, source_module->getEvoxDataCount(source_entity));
	const i32 filtered_tail = 12345;
	auto populateFilteredData = [&](EvoxModule& module, EntityRef entity) {
		for (u32 i = 1; i < lengthOf(types); ++i) {
			u8* data = (u8*)module.getEvoxData(entity, types[i]);
			// Nonzero unsupported fields must not survive save/load or hot reload.
			memset(data, 0x5a, ex_type_get_size(types[i]));
			if (i < 3) memcpy(data + ex_type_struct_field_offset(types[i], 1), &filtered_tail, sizeof(filtered_tail));
			if (i == 2) {
				const ex_type* nested_type = ex_type_struct_field_type(types[i], 0);
				memcpy(data + ex_type_struct_field_offset(types[i], 0) + ex_type_struct_field_offset(nested_type, 1), &filtered_tail, sizeof(filtered_tail));
			}
		}
	};
	populateFilteredData(*source_module, source_entity);
	const bool flag = true;
	const i64 tail = 0x123456789abcdefLL;
	u8* value = (u8*)source_module->getEvoxData(source_entity, types[0]);
	ASSERT_TRUE(value);
	memcpy(value + ex_type_struct_field_offset(types[0], 0), &flag, sizeof(flag));
	memcpy(value + ex_type_struct_field_offset(types[0], 2), &tail, sizeof(tail));
	OutputMemoryStream blob(getGlobalAllocator());
	source_module->serialize(blob);
	// Ensure the owner entity index really needs remapping.
	target_world.createEntity({}, Quat::IDENTITY);
	const EntityRef target_entity = target_world.createEntity({}, Quat::IDENTITY);
	EntityMap entity_map(getGlobalAllocator());
	entity_map.set(source_entity, target_entity);
	InputMemoryStream input(blob);
	target_module->deserialize(input, entity_map, 2);
	ASSERT_EQ(0, target_module->getEvoxDataCount(target_entity));
	target_module->setEvoxDataTypes(types);
	for (u32 pass = 0; pass < 2; ++pass) {
		if (pass == 1) {
			populateFilteredData(*target_module, target_entity);
			target_module->setEvoxDataTypes(types);
		}
		ASSERT_EQ(4, target_module->getEvoxDataCount(target_entity));
		for (u32 i = 1; i < lengthOf(types); ++i) {
			const u8* data = (const u8*)target_module->getEvoxData(target_entity, types[i]);
			ASSERT_TRUE(data);
			u32 zero_size = ex_type_get_size(types[i]);
			const u8* zero_data = data;
			if (i < 3) {
				i32 restored_filtered_tail;
				memcpy(&restored_filtered_tail, data + ex_type_struct_field_offset(types[i], 1), sizeof(restored_filtered_tail));
				ASSERT_EQ(filtered_tail, restored_filtered_tail);
				const ex_type* field_type = ex_type_struct_field_type(types[i], 0);
				zero_data += ex_type_struct_field_offset(types[i], 0);
				zero_size = ex_type_get_size(field_type);
				if (i == 2) {
					memcpy(&restored_filtered_tail, zero_data + ex_type_struct_field_offset(field_type, 1), sizeof(restored_filtered_tail));
					ASSERT_EQ(filtered_tail, restored_filtered_tail);
					zero_data += ex_type_struct_field_offset(field_type, 0);
					zero_size = ex_type_get_size(ex_type_struct_field_type(field_type, 0));
				}
			}
			for (u32 j = 0; j < zero_size; ++j) ASSERT_EQ(0, zero_data[j]);
		}
		const u8* restored = (const u8*)target_module->getEvoxData(target_entity, types[0]);
		ASSERT_TRUE(restored);
		bool restored_flag;
		i64 restored_tail;
		i32 owner_index;
		World* owner_world;
		const u32 owner_offset = ex_type_struct_field_offset(types[0], 1);
		const ex_type* owner_type = ex_type_struct_field_type(types[0], 1);
		memcpy(&restored_flag, restored + ex_type_struct_field_offset(types[0], 0), sizeof(restored_flag));
		memcpy(&restored_tail, restored + ex_type_struct_field_offset(types[0], 2), sizeof(restored_tail));
		memcpy(&owner_index, restored + owner_offset + ex_type_struct_field_offset(owner_type, 0), sizeof(owner_index));
		memcpy(&owner_world, restored + owner_offset + ex_type_struct_field_offset(owner_type, 1), sizeof(owner_world));
		ASSERT_EQ(flag, restored_flag);
		ASSERT_EQ(tail, restored_tail);
		ASSERT_EQ(target_entity.index, owner_index);
		ASSERT_TRUE(owner_world == &target_world);
	}
	engine->destroyWorld(target_world);
	engine->destroyWorld(source_world);
	engine.reset();
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(script);
	return true;
}

bool testEvoxModulePendingDataDestroyedEntity() {
	EvoxTestHost script_host;
	const char* source = R"(
		import "core:attributes"
		#[Data{}]
		struct TestData { value : i32; }
	)";
	ex_module* script = ex_module_create(&script_host.host);
	ASSERT_TRUE(script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(script, {source, (i64)stringLength(source)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ex_bytecode* bytecode = ex_bytecode_compile(script, &script_host.host, nullptr);
	ASSERT_TRUE(bytecode);

	const ex_type* data_type = nullptr;
	for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
		const ex_type* type = ex_bytecode_type(bytecode, i);
		const ex_string_view name = ex_type_get_name(type);
		if (StringView(name.begin, (u64)name.length) == "test.evox.TestData") data_type = type;
	}
	ASSERT_TRUE(data_type);

	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	engine->getFileSystem().mount(".", "");
	World& source_world = engine->createWorld();
	EvoxModule* source_module = (EvoxModule*)source_world.getModule("evox");
	ASSERT_TRUE(source_module);
	source_module->setEvoxDataTypes(Span<const ex_type*>(&data_type, 1));
	const EntityRef source_entity = source_world.createEntity({}, Quat::IDENTITY);
	source_module->createEvox(source_entity);
	ASSERT_TRUE(source_module->addEvoxData(source_entity, data_type));
	OutputMemoryStream blob(getGlobalAllocator());
	source_module->serialize(blob);

	World& target_world = engine->createWorld();
	EvoxModule* target_module = (EvoxModule*)target_world.getModule("evox");
	ASSERT_TRUE(target_module);
	const EntityRef target_entity = target_world.createEntity({}, Quat::IDENTITY);
	EntityMap entity_map(getGlobalAllocator());
	entity_map.set(source_entity, target_entity);
	InputMemoryStream input(blob);
	target_module->deserialize(input, entity_map, 2);
	target_world.destroyEntity(target_entity);
	target_module->setEvoxDataTypes(Span<const ex_type*>(&data_type, 1));
	ASSERT_EQ(0, target_module->getEvoxDataCount(target_entity));
	ASSERT_TRUE(!target_module->getEvoxData(target_entity, data_type));

	engine->destroyWorld(target_world);
	engine->destroyWorld(source_world);
	engine.reset();
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(script);
	return true;
}

bool testEvoxModuleMultiplePendingTypes() {
	EvoxTestHost script_host;
	const char* source = R"(
		import "core:attributes"
		#[Data{}]
		struct FirstData { value : i32; }
		#[Data{}]
		struct SecondData { value : i32; }
	)";
	ex_module* script = ex_module_create(&script_host.host);
	ASSERT_TRUE(script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(script, {source, (i64)stringLength(source)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ex_bytecode* bytecode = ex_bytecode_compile(script, &script_host.host, nullptr);
	ASSERT_TRUE(bytecode);

	const ex_type* first_type = nullptr;
	const ex_type* second_type = nullptr;
	for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
		const ex_type* type = ex_bytecode_type(bytecode, i);
		const ex_string_view name = ex_type_get_name(type);
		if (StringView(name.begin, (u64)name.length) == "test.evox.FirstData") first_type = type;
		if (StringView(name.begin, (u64)name.length) == "test.evox.SecondData") second_type = type;
	}
	ASSERT_TRUE(first_type && second_type);

	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	engine->getFileSystem().mount(".", "");
	World& source_world = engine->createWorld();
	EvoxModule* source_module = (EvoxModule*)source_world.getModule("evox");
	ASSERT_TRUE(source_module);
	const ex_type* source_types[] = {first_type, second_type};
	source_module->setEvoxDataTypes(source_types);
	const EntityRef source_entity = source_world.createEntity({}, Quat::IDENTITY);
	source_module->createEvox(source_entity);
	ASSERT_TRUE(source_module->addEvoxData(source_entity, first_type));
	ASSERT_TRUE(source_module->addEvoxData(source_entity, second_type));
	const i32 first_value = 123;
	const i32 second_value = 456;
	memcpy((void*)source_module->getEvoxData(source_entity, first_type), &first_value, sizeof(first_value));
	memcpy((void*)source_module->getEvoxData(source_entity, second_type), &second_value, sizeof(second_value));
	OutputMemoryStream blob(getGlobalAllocator());
	source_module->serialize(blob);

	World& target_world = engine->createWorld();
	EvoxModule* target_module = (EvoxModule*)target_world.getModule("evox");
	ASSERT_TRUE(target_module);
	const EntityRef target_entity = target_world.createEntity({}, Quat::IDENTITY);
	EntityMap entity_map(getGlobalAllocator());
	entity_map.set(source_entity, target_entity);
	target_module->setEvoxDataTypes(Span<const ex_type*>(&first_type, 1));
	InputMemoryStream input(blob);
	target_module->deserialize(input, entity_map, 2);
	ASSERT_TRUE(target_module->getEvoxData(target_entity, first_type));
	ASSERT_TRUE(!target_module->getEvoxData(target_entity, second_type));

	target_module->setEvoxDataTypes(source_types);
	const i32* restored_first = (const i32*)target_module->getEvoxData(target_entity, first_type);
	const i32* restored_second = (const i32*)target_module->getEvoxData(target_entity, second_type);
	ASSERT_TRUE(restored_first && restored_second);
	ASSERT_EQ(first_value, *restored_first);
	ASSERT_EQ(second_value, *restored_second);

	engine->destroyWorld(target_world);
	engine->destroyWorld(source_world);
	engine.reset();
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(script);
	return true;
}

bool testEvoxModuleZeroSizedData() {
	EvoxTestHost script_host;
	const char* source = R"(
		import "core:attributes"
		#[Data{}]
		struct EmptyData {}
	)";
	ex_module* script = ex_module_create(&script_host.host);
	ASSERT_TRUE(script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(script, {source, (i64)stringLength(source)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ex_bytecode* bytecode = ex_bytecode_compile(script, &script_host.host, nullptr);
	ASSERT_TRUE(bytecode);
	const ex_type* data_type = nullptr;
	for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
		const ex_type* type = ex_bytecode_type(bytecode, i);
		const ex_string_view name = ex_type_get_name(type);
		if (StringView(name.begin, (u64)name.length) == "test.evox.EmptyData") data_type = type;
	}
	ASSERT_TRUE(data_type);

	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	engine->getFileSystem().mount(".", "");
	World& source_world = engine->createWorld();
	EvoxModule* source_module = (EvoxModule*)source_world.getModule("evox");
	source_module->setEvoxDataTypes(Span<const ex_type*>(&data_type, 1));
	const EntityRef source_entity = source_world.createEntity({}, Quat::IDENTITY);
	source_module->createEvox(source_entity);
	ASSERT_TRUE(source_module->addEvoxData(source_entity, data_type));
	OutputMemoryStream blob(getGlobalAllocator());
	source_module->serialize(blob);

	World& target_world = engine->createWorld();
	EvoxModule* target_module = (EvoxModule*)target_world.getModule("evox");
	const EntityRef target_entity = target_world.createEntity({}, Quat::IDENTITY);
	EntityMap entity_map(getGlobalAllocator());
	entity_map.set(source_entity, target_entity);
	InputMemoryStream input(blob);
	target_module->deserialize(input, entity_map, 2);
	target_module->setEvoxDataTypes(Span<const ex_type*>(&data_type, 1));
	ASSERT_EQ(1, target_module->getEvoxDataCount(target_entity));

	engine->destroyWorld(target_world);
	engine->destroyWorld(source_world);
	engine.reset();
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(script);
	return true;
}

bool testEvoxModuleSameNamedTypes() {
	EvoxTestHost script_host;
	const char* source = "import \"a\" as a import \"b\" as b";
	ex_module* script = ex_module_create(&script_host.host);
	ASSERT_TRUE(script);
	ASSERT_EQ(EX_RESULT_OK, ex_module_compile(script, {source, (i64)stringLength(source)}, {"test.evox", 9}, &resolveTestImport, nullptr));
	ex_bytecode* bytecode = ex_bytecode_compile(script, &script_host.host, nullptr);
	ASSERT_TRUE(bytecode);

	const ex_type* types[2] = {};
	for (u32 i = 0; i < ex_bytecode_type_count(bytecode); ++i) {
		const ex_type* type = ex_bytecode_type(bytecode, i);
		const ex_string_view ex_name = ex_type_get_name(type);
		const StringView name(ex_name.begin, (u64)ex_name.length);
		if (name == "a.Same") types[0] = type;
		if (name == "b.Same") types[1] = type;
	}
	ASSERT_TRUE(types[0] && types[1]);

	const char* static_plugins[] = {"evox"};
	Engine::InitArgs args;
	args.static_plugins = static_plugins;
	UniquePtr<Engine> engine = Engine::create(static_cast<Engine::InitArgs&&>(args), getGlobalAllocator());
	ASSERT_TRUE(engine);
	engine->getFileSystem().mount(".", "");
	World& source_world = engine->createWorld();
	World& target_world = engine->createWorld();
	EvoxModule* source_module = (EvoxModule*)source_world.getModule("evox");
	EvoxModule* target_module = (EvoxModule*)target_world.getModule("evox");
	ASSERT_TRUE(source_module && target_module);
	source_module->setEvoxDataTypes(types);

	const EntityRef source_entity = source_world.createEntity({}, Quat::IDENTITY);
	const EntityRef target_entity = target_world.createEntity({}, Quat::IDENTITY);
	source_module->createEvox(source_entity);
	ASSERT_TRUE(source_module->addEvoxData(source_entity, types[0]));
	ASSERT_TRUE(source_module->addEvoxData(source_entity, types[1]));
	const i32 values[] = {42, 99};
	memcpy((void*)source_module->getEvoxData(source_entity, types[0]), &values[0], sizeof(values[0]));
	memcpy((void*)source_module->getEvoxData(source_entity, types[1]), &values[1], sizeof(values[1]));

	OutputMemoryStream blob(getGlobalAllocator());
	source_module->serialize(blob);
	EntityMap entity_map(getGlobalAllocator());
	entity_map.set(source_entity, target_entity);
	InputMemoryStream input(blob);
	target_module->deserialize(input, entity_map, 2);
	target_module->setEvoxDataTypes(types);
	ASSERT_EQ(2, target_module->getEvoxDataCount(target_entity));
	ASSERT_EQ(values[0], *(const i32*)target_module->getEvoxData(target_entity, types[0]));
	ASSERT_EQ(values[1], *(const i32*)target_module->getEvoxData(target_entity, types[1]));

	engine->destroyWorld(target_world);
	engine->destroyWorld(source_world);
	engine.reset();
	ex_bytecode_destroy(bytecode);
	ex_module_destroy(script);
	return true;
}

} // namespace

void runEvoxModuleTests() {
	RUN_TEST(testEvoxDataTypeDiscovery);
	RUN_TEST(testEvoxModuleSerialization);
	RUN_TEST(testEvoxModuleSerializationSchemaMigration);
	RUN_TEST(testEvoxModuleEnumSerializationRoundTrip);
	RUN_TEST(testEvoxModuleEnumSerializationSchemaMigration);
	RUN_TEST(testEvoxModuleSerializationFilteredFieldsAndOwner);
	RUN_TEST(testEvoxModulePendingDataDestroyedEntity);
	RUN_TEST(testEvoxModuleMultiplePendingTypes);
	RUN_TEST(testEvoxModuleZeroSizedData);
	RUN_TEST(testEvoxModuleSameNamedTypes);
}
