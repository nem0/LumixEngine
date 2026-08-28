#include "lumscript/lumscript_module.h"
#include "../../external/lumscript/arena.h"
#include "core/allocator.h"
#include "core/array.h"
#include "core/crt.h"
#include "core/hash_map.h"
#include "core/log.h"
#include "core/path.h"
#include "core/stream.h"
#include "core/tag_allocator.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/input_system.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "lumscript/capi.h"
#include "lumscript/lumscript_resource.h"

namespace Lumix {

namespace LumScript {
void gatherCoreFunctions(HashMap<NativeFunctionKey, ls_native_fn, NativeFunctionKeyHash>& functions);
}

struct LumScriptDiagnosticsContext {
	String* message = nullptr;
	ls_host* host = nullptr;
};

static void printLumScriptMessage(void* userdata, ls_string_view msg) {
	LumScriptDiagnosticsContext* ctx = (LumScriptDiagnosticsContext*)userdata;
	if (ctx->message) ctx->message->append(StringView(msg.begin, (u64)msg.length));
}

static ls_string_view toLs(StringView value) {
	return {value.data, (i64)value.length};
}

static ls_string_view toLs(const char* value) {
	return {value, (i64)stringLength(value)};
}

static void bindCoreFunctions(ls_module* module, ls_runtime* runtime, IAllocator& allocator) {
	HashMap<NativeFunctionKey, ls_native_fn, NativeFunctionKeyHash> functions(allocator);
	Lumix::LumScript::gatherCoreFunctions(functions);
	for (i32 unit_index = 0, unit_count = ls_module_get_unit_count(module); unit_index < unit_count; ++unit_index) {
		ls_unit* unit = ls_module_get_unit(module, unit_index);
		const ls_string_view path = ls_unit_get_path(unit);
		const StringView unit_path(path.begin, path.length);
		if (unit_path == "std:math" || unit_path == "std:mem") continue;
		for (i32 function_index = 0, function_count = ls_unit_get_native_function_count(unit); function_index < function_count; ++function_index) {
			const ls_string_view name = ls_unit_get_native_function_name(unit, function_index);
			const NativeFunctionKey key{unit_path, {name.begin, (u64)name.length}};
			auto iter = functions.find(key);
			if (!iter.isValid()) {
				logError("LumScript : failed to bind native function ", key.unit_path, ".", key.name);
				continue;
			}
			ls_runtime_set_native_function_callback(runtime, unit, function_index, iter.value());
		}
	}
}

struct LumScriptModuleImpl : LumScriptModule {
	struct AlignedByteBuffer {
		AlignedByteBuffer(IAllocator& allocator, u32 alignment)
			: allocator(allocator)
			, alignment(alignment)
		{}

		AlignedByteBuffer(AlignedByteBuffer&& rhs)
			: allocator(rhs.allocator)
			, data(rhs.data)
			, size(rhs.size)
			, capacity(rhs.capacity)
			, alignment(rhs.alignment)
		{
			rhs.data = nullptr;
			rhs.size = 0;
			rhs.capacity = 0;
		}

		~AlignedByteBuffer() {
			if (data) allocator.deallocate(data);
		}

		void resize(u32 new_size) {
			if (new_size > capacity) {
				u32 new_capacity = capacity > 0 ? capacity * 2 : 64;
				if (new_capacity < new_size) new_capacity = new_size;
				u8* new_data = (u8*)allocator.allocate(new_capacity, alignment);
				if (size > 0) memcpy(new_data, data, size);
				if (data) allocator.deallocate(data);
				data = new_data;
				capacity = new_capacity;
			}
			size = new_size;
		}

		IAllocator& allocator;
		u8* data = nullptr;
		u32 size = 0;
		u32 capacity = 0;
		u32 alignment;
	};

	struct LumScriptDataType {
		LumScriptDataType(const ls_type* type, IAllocator& allocator)
			: type(type)
			, element_size(ls_type_get_size(type))
			, values(allocator, ls_type_get_alignment(type))
			, entities(allocator)
		{}

		LumScriptDataType(LumScriptDataType&& rhs)
			: type(rhs.type)
			, element_size(rhs.element_size)
			, values(static_cast<AlignedByteBuffer&&>(rhs.values))
			, entities(rhs.entities.move())
		{}

		const ls_type* type;
		u32 element_size;
		AlignedByteBuffer values;
		Array<EntityRef> entities;
	};

	struct LumScriptDataRef {
		const ls_type* type;
		u32 index;
	};

	struct LumScriptComponent {
		explicit LumScriptComponent(IAllocator& allocator) : data(allocator) {}
		Array<LumScriptDataRef> data;
	};

	LumScriptModuleImpl(World& world, LumScriptSystem& system)
		: m_world(world)
		, m_system(system)
		, m_allocator(world.getAllocator())
		, m_data_types(m_allocator)
		, m_data_storage(m_allocator)
		, m_components(m_allocator)
	{
		m_system.registerModule(*this);
	}

	~LumScriptModuleImpl() { m_system.unregisterModule(*this); }

	static void reflect() {
		#include "lumscript_module.gen.h"
	}

	const char* getName() const override { return "lumscript"; }
	World& getWorld() override { return m_world; }

	void createLumScript(EntityRef entity) override {
		if (m_components.find(entity).isValid()) return;
		m_components.insert(entity, LumScriptComponent(m_allocator));
		m_world.onComponentCreated(entity, reflection::getComponentType("lumscript"), this);
	}

	void destroyLumScript(EntityRef entity) override {
		auto iter = m_components.find(entity);
		if (!iter.isValid()) return;
		while (!iter.value().data.empty()) removeLumScriptDataAt(iter.value(), iter.value().data.size() - 1);
		m_components.erase(iter);
		m_world.onComponentDestroyed(entity, reflection::getComponentType("lumscript"), this);
	}
	ISystem& getSystem() const override { return m_system; }
	i32 getVersion() const override { return 1; }
	bool shouldSerialize() override { return false; }
	void serialize(OutputMemoryStream&) override {}
	void deserialize(InputMemoryStream& serializer, const EntityMap&, i32 version) override {
		// Consume the level-script path written by the previous per-world implementation.
		if (version != 1) serializer.readString();
	}
	void update(float) override {}

	void setLumScriptDataTypes(Span<const ls_type*> types) override {
		clearLumScriptData();
		m_data_types.reserve(types.length());
		m_data_storage.reserve(types.length());
		for (const ls_type* type : types) {
			m_data_types.push(type);
			m_data_storage.emplace(type, m_allocator);
		}
	}
	void clearLumScriptData() override {
		for (LumScriptComponent& component : m_components) component.data.clear();
		m_data_storage.clear();
		m_data_types.clear();
	}
	bool isReady() const override { return m_system.isReady(); }
	Span<const ls_type*> getLumScriptDataTypes() const override { return m_data_types; }
	u32 getLumScriptDataCount(EntityRef entity) const override {
		auto iter = m_components.find(entity);
		return iter.isValid() ? iter.value().data.size() : 0;
	}
	const ls_type* getLumScriptDataType(EntityRef entity, u32 index) const override {
		auto iter = m_components.find(entity);
		if (!iter.isValid() || index >= (u32)iter.value().data.size()) return nullptr;
		return iter.value().data[index].type;
	}
	const void* getLumScriptData(EntityRef entity, const ls_type* type) const override {
		auto iter = m_components.find(entity);
		if (!iter.isValid()) return nullptr;
		const i32 ref_index = findDataRef(iter.value(), type);
		if (ref_index < 0) return nullptr;
		const LumScriptDataType* data_type = findDataType(type);
		if (!data_type) return nullptr;
		return data_type->values.data + iter.value().data[ref_index].index * data_type->element_size;
	}
	bool addLumScriptData(EntityRef entity, const ls_type* type) override {
		LumScriptDataType* data_type = findDataType(type);
		if (!data_type) return false;
		if (!m_world.hasComponent(entity, reflection::getComponentType("lumscript"))) {
			m_world.createComponent(reflection::getComponentType("lumscript"), entity);
		}
		LumScriptComponent& component = m_components[entity];
		if (findDataRef(component, type) >= 0) return false;

		const u32 index = data_type->entities.size();
		const u32 old_size = data_type->values.size;
		data_type->values.resize(old_size + data_type->element_size);
		memset(data_type->values.data + old_size, 0, data_type->element_size);
		data_type->entities.push(entity);
		component.data.push({type, index});
		return true;
	}
	bool removeLumScriptData(EntityRef entity, const ls_type* type) override {
		auto iter = m_components.find(entity);
		if (!iter.isValid()) return false;
		const i32 ref_index = findDataRef(iter.value(), type);
		if (ref_index < 0) return false;
		removeLumScriptDataAt(iter.value(), ref_index);
		return true;
	}
	bool hasLumScriptData(EntityRef entity, const ls_type* type) const override {
		auto iter = m_components.find(entity);
		return iter.isValid() && findDataRef(iter.value(), type) >= 0;
	}
	ls_runtime* getDebugRuntime() override { return m_system.getDebugRuntime(); }
	const Path& getDebugPath() const override { return m_system.getDebugPath(); }
	bool setDebugBreakpoint(const Path& source, u32 line) override { return m_system.setDebugBreakpoint(source, line); }
	bool removeDebugBreakpoint(const Path& source, u32 line) override { return m_system.removeDebugBreakpoint(source, line); }

private:
	static i32 findDataRef(const LumScriptComponent& component, const ls_type* type) {
		return component.data.find([type](const LumScriptDataRef& ref) { return ref.type == type; });
	}

	LumScriptDataType* findDataType(const ls_type* type) {
		const i32 index = m_data_storage.find([type](const LumScriptDataType& data_type) { return data_type.type == type; });
		return index >= 0 ? &m_data_storage[index] : nullptr;
	}

	const LumScriptDataType* findDataType(const ls_type* type) const {
		const i32 index = m_data_storage.find([type](const LumScriptDataType& data_type) { return data_type.type == type; });
		return index >= 0 ? &m_data_storage[index] : nullptr;
	}

	void removeLumScriptDataAt(LumScriptComponent& component, u32 ref_index) {
		const LumScriptDataRef ref = component.data[ref_index];
		LumScriptDataType* data_type = findDataType(ref.type);
		ASSERT(data_type && ref.index < (u32)data_type->entities.size());

		const u32 last_index = data_type->entities.size() - 1;
		if (ref.index != last_index) {
			memcpy(data_type->values.data + ref.index * data_type->element_size,
				data_type->values.data + last_index * data_type->element_size,
				data_type->element_size);
			const EntityRef moved_entity = data_type->entities[last_index];
			data_type->entities[ref.index] = moved_entity;
			auto moved_component = m_components.find(moved_entity);
			ASSERT(moved_component.isValid());
			const i32 moved_ref = findDataRef(moved_component.value(), ref.type);
			ASSERT(moved_ref >= 0);
			moved_component.value().data[moved_ref].index = ref.index;
		}
		data_type->entities.pop();
		data_type->values.resize(last_index * data_type->element_size);
		component.data.swapAndPop(ref_index);
	}

	World& m_world;
	LumScriptSystem& m_system;
	IAllocator& m_allocator;
	Array<const ls_type*> m_data_types;
	Array<LumScriptDataType> m_data_storage;
	HashMap<EntityRef, LumScriptComponent> m_components;
};

struct LumScriptSystemImpl : LumScriptSystem {
	struct ImportContext {
		FileSystem& filesystem;
		IAllocator& allocator;
		Array<OutputMemoryStream> sources;
		ImportContext(FileSystem& filesystem, IAllocator& allocator)
			: filesystem(filesystem), allocator(allocator), sources(allocator) {}
	};

	explicit LumScriptSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "lumscript")
		, m_lumscript_resource_manager(m_allocator)
		, m_path("scripts/main.lum")
		, m_data_types(m_allocator)
		, m_modules(m_allocator)
	{
		m_host.arena = {};
		LumScriptModuleImpl::reflect();
		m_lumscript_resource_manager.create(LumScriptResource::TYPE, m_engine.getResourceManager());
	}

	~LumScriptSystemImpl() {
		if (m_resource) {
			m_resource->getObserverCb().unbind<&LumScriptSystemImpl::onResourceChanged>(this);
			m_resource->decRefCount();
		}
		destroyScript();
		m_lumscript_resource_manager.destroy();
	}

	void startGame() override {
		m_is_game_running = true;
	}

	void stopGame() override {
		m_is_game_running = false;
	}


	const char* getName() const override { return "lumscript_system"; }
	Engine& getEngine() override { return m_engine; }
	void serialize(OutputMemoryStream&) const override {}
	bool deserialize(i32, InputMemoryStream&) override { return true; }

	void registerModule(LumScriptModule& module) override {
		m_modules.push(&module);
		module.setLumScriptDataTypes(m_data_types);
	}

	void unregisterModule(LumScriptModule& module) override { m_modules.eraseItem(&module); }
	bool isReady() const override { return m_is_ready; }
	Span<const ls_type*> getLumScriptDataTypes() const override { return m_data_types; }
	ls_runtime* getDebugRuntime() override { return m_runtime; }
	const Path& getDebugPath() const override { return m_path; }

	bool setDebugBreakpoint(const Path& source, u32 line) override {
		return m_bytecode && ls_debug_set_breakpoint(m_bytecode, toLs(source.c_str()), line, nullptr) != LS_RESULT_FAILURE;
	}

	bool removeDebugBreakpoint(const Path& source, u32 line) override {
		return m_bytecode && ls_debug_remove_breakpoint(m_bytecode, toLs(source.c_str()), line) != LS_RESULT_FAILURE;
	}

	void createModules(World& world) override {
		loadRoot();
		auto module = UniquePtr<LumScriptModuleImpl>::create(m_allocator, world, *this);
		world.addModule(module.move());
		if (m_is_ready) addWorld(world);
	}

	void update(float time_delta) override {
		if (!m_is_ready || !m_runtime || ls_debug_is_suspended(m_runtime)) return;
		if (!m_is_game_running) return;

		const ls_string_view function_name = toLs("update");
		if (ls_bytecode_runtime_result_kind(m_runtime, function_name) == LS_TYPE_INVALID) return;

		ls_push_f32(m_runtime, time_delta);
		if (ls_call(m_runtime, function_name) == LS_RESULT_FAILURE) logError("LumScript update failed");
	}

private:
	void loadRoot() {
		if (m_resource) return;
		m_resource = m_engine.getResourceManager().load<LumScriptResource>(m_path);
		if (m_resource) m_resource->onLoaded<&LumScriptSystemImpl::onResourceChanged>(this);
	}

	void addWorld(World& world) {
		const ls_string_view function_name = toLs("addWorld");
		if (ls_bytecode_runtime_result_kind(m_runtime, function_name) == LS_TYPE_INVALID) return;
		ls_push_ptr(m_runtime, &world);
		if (ls_call(m_runtime, function_name) == LS_RESULT_FAILURE) {
			logError("LumScript addWorld failed");
		}
	}

	static bool isLumScriptDataType(const ls_type* type) {
		if (!type || ls_type_get_kind(type) != LS_TYPE_STRUCT) return false;
		for (u32 i = 0, count = ls_type_attribute_count(type); i < count; ++i) {
			const ls_attribute attribute = ls_type_attribute_value(type, i);
			if (!attribute.type) continue;
			const ls_string_view name = ls_type_get_name(attribute.type);
			if (StringView(name.begin, name.length) == "component") return true;
		}
		return false;
	}

	static int resolveImport(void* userdata, ls_string_view path, ls_string_view, ls_string_view* source) {
		ImportContext& ctx = *(ImportContext*)userdata;
		StringView requested(path.begin, path.length);
		Path file_path;
		if (startsWith(requested, "core:")) {
			StringView name = requested.withoutLeft(5);
			file_path = endsWith(name, ".lum") ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".lum");
		}
		else {
			file_path = endsWith(requested, ".lum") ? Path(requested) : Path(requested, ".lum");
		}
		OutputMemoryStream& blob = ctx.sources.emplace(ctx.allocator);
		if (!ctx.filesystem.getContentSync(file_path, blob)) {
			ctx.sources.pop();
			return 0;
		}
		*source = {(const char*)blob.data(), (i64)blob.size()};
		return 1;
	}

	void onResourceChanged(Resource::State, Resource::State state, Resource&) {
		m_resource_ready = state == Resource::State::READY;
		if (!m_resource_ready) {
			destroyScript();
			return;
		}
		m_is_ready = compileAndRun();
	}

	void destroyScript() {
		m_is_ready = false;
		for (LumScriptModule* module : m_modules) module->clearLumScriptData();
		m_data_types.clear();
		if (m_runtime) { ls_runtime_destroy(m_runtime); m_runtime = nullptr; }
		if (m_bytecode) { ls_bytecode_destroy(m_bytecode); m_bytecode = nullptr; }
		if (m_module) { ls_module_destroy(m_module); m_module = nullptr; }
		if (m_host.arena.allocate) {
			ls_default_arena_destroy(&m_host.arena);
			m_host.arena = {};
		}
	}

	bool compileAndRun() {
		if (!m_resource) return false;
		destroyScript();
		String diagnostics(m_allocator);
		LumScriptDiagnosticsContext diagnostics_context = {&diagnostics, &m_host};
		m_host.diagnostics_userdata = &diagnostics_context;
		m_host.print = &printLumScriptMessage;
		ls_default_arena_create(&m_host.arena);
		m_module = ls_module_create(&m_host);
		ImportContext imports(m_engine.getFileSystem(), m_allocator);
		if (!m_module || !ls_module_compile(m_module, toLs(m_resource->getSourceCode()), toLs(m_path.c_str()), &resolveImport, &imports)) {
			m_host.diagnostics_userdata = nullptr;
			m_host.print = nullptr;
			logError("LumScript compilation failed: ", diagnostics);
			return false;
		}
		m_bytecode = ls_bytecode_compile(m_module, &m_host, nullptr);
		m_host.diagnostics_userdata = nullptr;
		m_host.print = nullptr;
		if (!m_bytecode) {
			logError("LumScript bytecode compilation failed: ", diagnostics);
			return false;
		}
		for (u32 i = 0, count = ls_bytecode_type_count(m_bytecode); i < count; ++i) {
			const ls_type* type = ls_bytecode_type(m_bytecode, i);
			if (isLumScriptDataType(type)) m_data_types.push(type);
		}
		m_runtime = ls_runtime_create(m_bytecode, &m_host);
		if (!m_runtime) return false;
		bindCoreFunctions(m_module, m_runtime, m_allocator);
		for (LumScriptModule* module : m_modules) module->setLumScriptDataTypes(m_data_types);
		const ls_string_view init_name = toLs("init");
		if (ls_bytecode_runtime_result_kind(m_runtime, init_name) != LS_TYPE_INVALID) {
			ls_push_ptr(m_runtime, &m_engine.getInputSystem());
			if (ls_call(m_runtime, init_name) == LS_RESULT_FAILURE) {
				logError("LumScript init failed");
				return false;
			}
		}
		for (LumScriptModule* module : m_modules) addWorld(module->getWorld());
		return true;
	}

	Engine& m_engine;
	TagAllocator m_allocator;
	LumScriptResourceManager m_lumscript_resource_manager;
	Path m_path;
	LumScriptResource* m_resource = nullptr;
	ls_host m_host;
	ls_module* m_module = nullptr;
	ls_bytecode* m_bytecode = nullptr;
	ls_runtime* m_runtime = nullptr;
	Array<const ls_type*> m_data_types;
	Array<LumScriptModule*> m_modules;
	bool m_resource_ready = false;
	bool m_is_ready = false;
	bool m_is_game_running = false;
};

IModule* createLumScriptModule(World& world);
void destroyLumScriptModule(IModule* module);

LUMIX_PLUGIN_ENTRY(lumscript) {
	return LUMIX_NEW(engine.getAllocator(), LumScriptSystemImpl)(engine);
}

} // namespace Lumix
