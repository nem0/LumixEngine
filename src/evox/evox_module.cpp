#include "evox/evox_module.h"
#include "../../external/evox/arena.h"
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
#include "evox/capi.h"
#include "evox/evox_resource.h"

namespace Lumix {

namespace Evox {
void gatherCoreFunctions(HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>& functions);
}

static constexpr const char* EVOX_DATA_ATTRIBUTE_TYPE = "core:attributes.Data";
static constexpr const char* EVOX_OWNER_ATTRIBUTE_TYPE = "core:attributes.Owner";
static constexpr const char* EVOX_ENTITY_TYPE = "core:entity.Entity";

struct EvoxDiagnosticsContext {
	String* message = nullptr;
	ex_host* host = nullptr;
};

static void printEvoxMessage(void* userdata, ex_string_view msg) {
	EvoxDiagnosticsContext* ctx = (EvoxDiagnosticsContext*)userdata;
	if (ctx->message) ctx->message->append(StringView(msg.begin, (u64)msg.length));
}

static ex_string_view toEvox(StringView value) {
	return {value.data, (i64)value.length};
}

static ex_string_view toEvox(const char* value) {
	return {value, (i64)stringLength(value)};
}

static StringView fromEvox(ex_string_view value) {
	return {value.begin, (u64)value.length};
}

static bool isSerializableType(const ex_type& type) {
	switch (ex_type_get_kind(&type)) {
		case EX_TYPE_BOOL:
		case EX_TYPE_I8:
		case EX_TYPE_U8:
		case EX_TYPE_I16:
		case EX_TYPE_U16:
		case EX_TYPE_I32:
		case EX_TYPE_U32:
		case EX_TYPE_I64:
		case EX_TYPE_U64:
		case EX_TYPE_F32:
		case EX_TYPE_F64:
		case EX_TYPE_ENUM:
		case EX_TYPE_CPTR:
		case EX_TYPE_STRUCT: // Keep structs even when all their fields are filtered out.
			return true;
		default:
			return false;
	}
}

static ex_native_fn resolveCoreFunction(ex_runtime*, ex_native_function_desc function, void* userdata) {
	auto& functions = *(HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash>*)userdata;
	const NativeFunctionKey key{fromEvox(function.unit_path), fromEvox(function.name)};
	auto iter = functions.find(key);
	if (iter.isValid()) return iter.value();
	logError("Evox : failed to resolve native function ", key.unit_path, ".", key.name);
	return nullptr;
}


struct EvoxSystemImpl : EvoxSystem {
	struct ImportContext {
		FileSystem& filesystem;
		IAllocator& allocator;
		Array<OutputMemoryStream> sources;
		ImportContext(FileSystem& filesystem, IAllocator& allocator)
			: filesystem(filesystem), allocator(allocator), sources(allocator) {}
	};

	explicit EvoxSystemImpl(Engine& engine);

	~EvoxSystemImpl() {
		if (m_resource) {
			m_resource->getObserverCb().unbind<&EvoxSystemImpl::onResourceChanged>(this);
			m_resource->decRefCount();
		}
		destroyScript();
		m_evox_resource_manager.destroy();
	}

	void startGame() override {
		m_is_game_running = true;
		if (!m_is_ready) return;
		for (EvoxModule* module : m_modules) addWorld(module->getWorld());
		callStart();
	}

	void stopGame() override {
		m_is_game_running = false;
		if (m_runtime) {
			ex_runtime_destroy(m_runtime);
			m_runtime = nullptr;
			createRuntime();
			// recreated the runtime to avoid any dangling stuff (changed globals, suspended state, ...)
		}
	}


	const char* getName() const override { return "evox_system"; }
	Engine& getEngine() override { return m_engine; }

	void serialize(OutputMemoryStream& out) const override {}

	bool deserialize(i32, InputMemoryStream&) override { return true; }

	void registerModule(EvoxModule& module) override {
		m_modules.push(&module);
		module.setEvoxDataTypes(m_data_types);
		if (m_is_game_running && m_is_ready) addWorld(module.getWorld());
	}

	void unregisterModule(EvoxModule& module) override { m_modules.eraseItem(&module); }
	bool isReady() const override { return m_is_ready; }
	Span<const ex_type*> getEvoxDataTypes() const override { return m_data_types; }
	ex_task* getTask() override { return m_task; }
	ex_module* getDebugModule() override { return m_module; }
	const Path& getDebugPath() const override { return m_path; }

	ex_string_view debugSourceName(const Path& source) {
		// Debug locations retain import names, while the editor uses filesystem paths.
		for (u32 i = 0, count = ex_debug_unit_count(m_runtime); i < count; ++i) {
			const ex_string_view name = ex_debug_unit_source_name(m_runtime, i);
			const StringView requested(name.begin, name.length);
			Path file_path;
			if (startsWith(requested, "core:")) {
				const StringView core_name = requested.withoutLeft(5);
				file_path = endsWith(core_name, ".evox") ? Path("engine/scripts/core/", core_name) : Path("engine/scripts/core/", core_name, ".evox");
			} else {
				file_path = endsWith(requested, ".evox") ? Path(requested) : Path(requested, ".evox");
			}
			if (file_path == source) return name;
		}
		return toEvox(source.c_str());
	}

	bool setDebugBreakpoint(const Path& source, u32 line) override {
		return m_bytecode && ex_debug_set_breakpoint(m_bytecode, debugSourceName(source), line, nullptr) != EX_RESULT_FAILURE;
	}

	bool removeDebugBreakpoint(const Path& source, u32 line) override {
		return m_bytecode && ex_debug_remove_breakpoint(m_bytecode, debugSourceName(source), line) != EX_RESULT_FAILURE;
	}

	void createModules(World& world) override;

	void update(float time_delta) override {
		if (!m_is_ready || !m_runtime || !m_task) return;
		// update() is not a resumable invocation. A suspended task can only be
		// continued by the debugger; update scripts must not yield.
		if (ex_task_get_state(m_task) == EX_TASK_SUSPENDED) return;
		if (!m_is_game_running) return;

		const ex_string_view function_name = toEvox("update");
		const ex_result result = ex_call(m_task, function_name, &time_delta, sizeof(time_delta));
		if (result != EX_RESULT_OK && result != EX_RESULT_FUNCTION_NOT_FOUND) logError("Evox update failed");
	}

	void loadRoot() {
		if (m_resource) return;
		m_resource = m_engine.getResourceManager().load<EvoxResource>(m_path);
		if (m_resource) m_resource->onLoaded<&EvoxSystemImpl::onResourceChanged>(this);
	}

	void callStart() {
		if (!m_runtime) return;
		const ex_string_view function_name = toEvox("start");
		InputSystem* input = &m_engine.getInputSystem();
		const ex_result result = ex_call(m_task, function_name, &input, sizeof(input));
		if (result != EX_RESULT_OK && result != EX_RESULT_FUNCTION_NOT_FOUND) logError("Evox start failed");
	}

	void addWorld(World& world) {
		const ex_string_view function_name = toEvox("addWorld");
		World* world_ptr = &world;
		const ex_result result = ex_call(m_task, function_name, &world_ptr, sizeof(world_ptr));
		if (result != EX_RESULT_OK && result != EX_RESULT_FUNCTION_NOT_FOUND) {
			logError("Evox addWorld failed");
		}
	}

	static bool isEvoxDataType(const ex_type& type) {
		if (ex_type_get_kind(&type) != EX_TYPE_STRUCT) return false;

		for (u32 i = 0, count = ex_type_attribute_count(&type); i < count; ++i) {
			const ex_attribute attribute = ex_type_attribute_value(&type, i);
			if (!attribute.type) continue;

			const ex_string_view name = ex_type_get_name(attribute.type);
			if (StringView(name.begin, name.length) == EVOX_DATA_ATTRIBUTE_TYPE) return true;
		}
		return false;
	}

	static int resolveImport(void* userdata, ex_string_view path, ex_string_view, ex_string_view* source) {
		ImportContext& ctx = *(ImportContext*)userdata;
		StringView requested(path.begin, path.length);
		Path file_path;
		if (startsWith(requested, "core:")) {
			StringView name = requested.withoutLeft(5);
			file_path = endsWith(name, ".evox") ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".evox");
		}
		else {
			file_path = endsWith(requested, ".evox") ? Path(requested) : Path(requested, ".evox");
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
		for (EvoxModule* module : m_modules) module->clearEvoxData();
		m_data_types.clear();
		if (m_task) { ex_task_destroy(m_task); m_task = nullptr; }
		if (m_runtime) { ex_runtime_destroy(m_runtime); m_runtime = nullptr; }
		if (m_bytecode) { ex_bytecode_destroy(m_bytecode); m_bytecode = nullptr; }
		if (m_module) { ex_module_destroy(m_module); m_module = nullptr; }
		if (m_host.arena.allocate) {
			ex_default_arena_destroy(&m_host.arena);
			m_host.arena = {};
		}
	}

	bool createRuntime() {
		m_runtime = ex_runtime_create(m_bytecode, &m_host);
		if (!m_runtime) return false;
		if (ex_runtime_set_native_resolver(m_runtime, &resolveCoreFunction, &m_native_functions) != EX_RESULT_OK) return false;
		m_task = ex_task_create(m_runtime);
		return m_task != nullptr;
	}

	bool compileAndRun() {
		if (!m_resource) return false;
		destroyScript();
		String diagnostics(m_allocator);
		EvoxDiagnosticsContext diagnostics_context = {&diagnostics, &m_host};
		m_host.diagnostics_userdata = &diagnostics_context;
		m_host.print = &printEvoxMessage;
		ex_default_arena_create(&m_host.arena);
		m_module = ex_module_create(&m_host);
		ImportContext imports(m_engine.getFileSystem(), m_allocator);
		if (!m_module || !ex_module_compile(m_module, toEvox(m_resource->getSourceCode()), toEvox(m_path.c_str()), &resolveImport, &imports)) {
			m_host.diagnostics_userdata = nullptr;
			m_host.print = nullptr;
			logError("Evox compilation failed: ", diagnostics);
			return false;
		}
		m_bytecode = ex_bytecode_compile(m_module, &m_host, nullptr);
		m_host.diagnostics_userdata = nullptr;
		m_host.print = nullptr;
		if (!m_bytecode) {
			logError("Evox bytecode compilation failed: ", diagnostics);
			return false;
		}
		for (u32 i = 0, count = ex_bytecode_type_count(m_bytecode); i < count; ++i) {
			const ex_type* type = ex_bytecode_type(m_bytecode, i);
			if (isEvoxDataType(*type)) m_data_types.push(type);
		}
		for (EvoxModule* module : m_modules) module->setEvoxDataTypes(m_data_types);
		if (!createRuntime()) return false;
		// startGame can be called before the script resource has finished loading.
		if (m_is_game_running) {
			for (EvoxModule* module : m_modules) addWorld(module->getWorld());
			callStart();
		}
		return true;
	}

	Engine& m_engine;
	TagAllocator m_allocator;
	EvoxResourceManager m_evox_resource_manager;
	Path m_path;
	EvoxResource* m_resource = nullptr;
	ex_host m_host;
	ex_module* m_module = nullptr;
	ex_bytecode* m_bytecode = nullptr;
	ex_runtime* m_runtime = nullptr;
	ex_task* m_task = nullptr;
	HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash> m_native_functions;
	Array<const ex_type*> m_data_types;
	Array<EvoxModule*> m_modules;
	bool m_resource_ready = false;
	bool m_is_ready = false;
	bool m_is_game_running = false;
};


struct EvoxModuleImpl : EvoxModule {
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

	struct EvoxDataType {
		EvoxDataType(const ex_type* type, IAllocator& allocator)
			: type(type)
			, element_size(ex_type_get_size(type))
			, values(allocator, ex_type_get_alignment(type))
			, entities(allocator)
		{}

		EvoxDataType(EvoxDataType&& rhs)
			: type(rhs.type)
			, element_size(rhs.element_size)
			, values(static_cast<AlignedByteBuffer&&>(rhs.values))
			, entities(rhs.entities.move())
		{}

		const ex_type* type = nullptr;
		u32 element_size;
		AlignedByteBuffer values;
		Array<EntityRef> entities;
	};

	struct EvoxDataRef {
		const ex_type* type;
		u32 index;
	};

	struct EvoxComponent {
		explicit EvoxComponent(IAllocator& allocator) : data(allocator) {}
		Array<EvoxDataRef> data;
	};

	struct EvoxFieldDesc;

	struct EvoxTypeDesc {
		explicit EvoxTypeDesc(IAllocator& allocator) : type_name(allocator), fields(allocator) {}
		
		u32 getSize() const;

		String type_name;
		ex_type_kind kind = EX_TYPE_INVALID;
		Array<EvoxFieldDesc> fields;
	};

	struct EvoxFieldDesc {
		explicit EvoxFieldDesc(IAllocator& allocator) : name(allocator), type(allocator) {}
		String name;
		EvoxTypeDesc type;
	};

	struct PendingType {
		explicit PendingType(IAllocator& allocator) : type_desc(allocator), values(allocator), entities(allocator) {}
		PendingType(PendingType&& rhs)
			: type_desc(static_cast<EvoxTypeDesc&&>(rhs.type_desc))
			, values(static_cast<OutputMemoryStream&&>(rhs.values))
			, entities(static_cast<Array<EntityRef>&&>(rhs.entities)) {}

		EvoxTypeDesc type_desc;
		OutputMemoryStream values;
		Array<EntityRef> entities;
	};

	EvoxModuleImpl(World& world, EvoxSystemImpl& system)
		: m_world(world)
		, m_system(system)
		, m_allocator(world.getAllocator())
		, m_data_storage(m_allocator)
		, m_components(m_allocator)
		, m_pending_types(m_allocator)
	{
		m_system.registerModule(*this);
	}

	~EvoxModuleImpl() { m_system.unregisterModule(*this); }

	static void reflect() {
		#include "evox_module.gen.h"
	}

	const char* getName() const override { return "evox"; }
	World& getWorld() override { return m_world; }

	void createEvox(EntityRef entity) override {
		if (m_components.find(entity).isValid()) return;
		m_components.insert(entity, EvoxComponent(m_allocator));
		m_world.onComponentCreated(entity, reflection::getComponentType("evox"), this);
	}

	void destroyEvox(EntityRef entity) override {
		auto iter = m_components.find(entity);
		if (!iter.isValid()) return;
		while (!iter.value().data.empty()) removeEvoxDataAt(iter.value(), iter.value().data.size() - 1);
		m_components.erase(iter);
		removePendingData(entity);
		m_world.onComponentDestroyed(entity, reflection::getComponentType("evox"), this);
	}
	ISystem& getSystem() const override { return m_system; }
	i32 getVersion() const override { return 2; }
	bool shouldSerialize() override { return true; }

	void deserializeTypeDesc(InputMemoryStream& in, EvoxTypeDesc& desc) {
		desc.type_name = in.readString();
		in.read(desc.kind);
		u32 num_fields = in.read<u32>();
		desc.fields.reserve(num_fields);
		for (u32 i = 0; i < num_fields; ++i) {
			EvoxFieldDesc& field = desc.fields.emplace(m_allocator);
			field.name = in.readString();
			deserializeTypeDesc(in, field.type);
		}
	}

	void serializeTypeDesc(OutputMemoryStream& out, const EvoxTypeDesc& desc) {
		out.writeString(desc.type_name);
		out.write(desc.kind);
		out.write(desc.fields.size());
		for (const EvoxFieldDesc& field : desc.fields) {
			out.writeString(field.name);
			serializeTypeDesc(out, field.type);
		}
	}

	void serializeTypeDesc(OutputMemoryStream& out, const ex_type* type) {
		out.writeString(fromEvox(ex_type_get_name(type)));
		out.write(ex_type_get_kind(type));
		const u32 num_fields = ex_type_struct_field_count(type);
		u32 serializable_fields = 0;
		for (u32 i = 0; i < num_fields; ++i) {
			if (isSerializableType(*ex_type_struct_field_type(type, i))) ++serializable_fields;
		}
		out.write(serializable_fields);
		for (u32 i = 0; i < num_fields; ++i) {
			const ex_type* field_type = ex_type_struct_field_type(type, i);
			if (!isSerializableType(*field_type)) continue;
			out.writeString(fromEvox(ex_type_struct_field_name(type, i)));
			serializeTypeDesc(out, field_type);
		}
	}

	void serialize(OutputMemoryStream& out) override {
		out.write((u32)m_components.size());
		for (auto iter = m_components.begin(), end = m_components.end(); iter != end; ++iter) out.write(iter.key());

		out.write((u32)(m_data_storage.size() + m_pending_types.size()));
		for (const PendingType& t : m_pending_types) {
			out.writeArray(t.entities);
			out.write((u64)t.values.size());
			out.write(t.values.data(), t.values.size());
			serializeTypeDesc(out, t.type_desc);
		}
		for (const EvoxDataType& t : m_data_storage) {
			const u32 packed_stride = getPackedSize(t.type);
			const u64 packed_size = (u64)t.entities.size() * packed_stride;
			out.writeArray(t.entities);
			out.write(packed_size);
			u8* packed = (u8*)out.skip(packed_size);
			packValues(t.type, packed, packed_stride, t.values.data, t.element_size, t.entities.size());
			serializeTypeDesc(out, t.type);
		}
	}

	void deserialize(InputMemoryStream& in, const EntityMap& entity_map, i32 version) override {
		if (version <= 0) in.readString();
		if (version <= 1) return;
		const u32 component_count = in.read<u32>();
		for (u32 i = 0; i < component_count; ++i) {
			const EntityRef source_entity = in.read<EntityRef>();
			const EntityRef mapped = entity_map.get(source_entity);
			createEvox(mapped);
		}

		const u32 num_types = in.read<u32>();
		m_pending_types.reserve(m_pending_types.size() + num_types);
		for (u32 i = 0; i < num_types; ++i) {
			PendingType& t = m_pending_types.emplace(m_allocator);
			in.readArray(&t.entities);
			for (EntityRef& entity : t.entities) entity = entity_map.get(entity);
			u64 data_size = in.read<u64>();
			t.values.resize(data_size);
			in.read(t.values.getMutableData(), data_size);
			deserializeTypeDesc(in, t.type_desc);
			remapEntityProperties(t.type_desc, t.values.getMutableData(), t.type_desc.getSize(), t.entities.size(), entity_map);
		}
		applyPendingData();
	}

	void update(float) override {}

	Span<const u8> getEvoxData(const char* type_name) override {
		const StringView requested_name(type_name);
		for (const EvoxDataType& data_type : m_data_storage) {
			const ex_string_view name = ex_type_get_name(data_type.type);
			if (requested_name != StringView(name.begin, (u64)name.length)) continue;
			return Span<const u8>(data_type.values.data, data_type.values.size);
		}
		return {};
	}

	void setEvoxDataTypes(Span<const ex_type*> types) override {
		clearEvoxData();
		m_data_storage.reserve(types.length());
		for (const ex_type* type : types) {
			m_data_storage.emplace(type, m_allocator);
		}
		applyPendingData();
	}

	void clearEvoxData() override {
		// stash data from m_data_storage to m_pending_types
		m_pending_types.reserve(m_pending_types.size() + m_data_storage.size());
		for (const EvoxDataType& src : m_data_storage) {
			PendingType& dst = m_pending_types.emplace(m_allocator);
			src.entities.copyTo(dst.entities);
			createTypeDesc(dst.type_desc, src.type);
			dst.values.resize(src.entities.size() * dst.type_desc.getSize());
			packValues(src.type
				, dst.values.getMutableData()
				, dst.type_desc.getSize()
				, src.values.data
				, src.element_size
				, src.entities.size());
		}

		m_data_storage.clear();
		for (EvoxComponent& component : m_components) component.data.clear();
		m_data_storage.clear();
	}

	bool isReady() const override { return m_system.isReady(); }

	Span<const ex_type*> getEvoxDataTypes() const override { return m_system.getEvoxDataTypes(); }

	u32 getEvoxDataCount(EntityRef entity) const override {
		auto iter = m_components.find(entity);
		return iter.isValid() ? iter.value().data.size() : 0;
	}

	const ex_type* getEvoxDataType(EntityRef entity, u32 index) const override {
		auto iter = m_components.find(entity);
		if (!iter.isValid() || index >= (u32)iter.value().data.size()) return nullptr;
		return iter.value().data[index].type;
	}
	const void* getEvoxData(EntityRef entity, const ex_type* type) const override {
		auto iter = m_components.find(entity);
		if (!iter.isValid()) return nullptr;
		const i32 ref_index = findDataRef(iter.value(), type);
		if (ref_index < 0) return nullptr;
		const EvoxDataType* data_type = findDataType(type);
		if (!data_type) return nullptr;
		return data_type->values.data + iter.value().data[ref_index].index * data_type->element_size;
	}
	bool addEvoxData(EntityRef entity, const ex_type* type) override {
		EvoxDataType* data_type = findDataType(type);
		if (!data_type) return false;
		if (!m_world.hasComponent(entity, reflection::getComponentType("evox"))) {
			m_world.createComponent(reflection::getComponentType("evox"), entity);
		}
		EvoxComponent& component = m_components[entity];
		if (findDataRef(component, type) >= 0) return false;

		const u32 index = data_type->entities.size();
		const u32 old_size = data_type->values.size;
		data_type->values.resize(old_size + data_type->element_size);
		u8* value = data_type->values.data + old_size;
		memset(value, 0, data_type->element_size);
		injectEntity(*data_type, value, entity);
		data_type->entities.push(entity);
		component.data.push({type, index});
		return true;
	}
	bool removeEvoxData(EntityRef entity, const ex_type* type) override {
		auto iter = m_components.find(entity);
		if (!iter.isValid()) return false;
		const i32 ref_index = findDataRef(iter.value(), type);
		if (ref_index < 0) return false;
		removeEvoxDataAt(iter.value(), ref_index);
		return true;
	}
	bool hasEvoxData(EntityRef entity, const ex_type* type) const override {
		auto iter = m_components.find(entity);
		return iter.isValid() && findDataRef(iter.value(), type) >= 0;
	}
	ex_runtime* getDebugRuntime() override { return m_system.m_runtime; }
	ex_task* getTask() override { return m_system.getTask(); }
	ex_module* getDebugModule() override { return m_system.getDebugModule(); }
	const Path& getDebugPath() const override { return m_system.getDebugPath(); }
	bool setDebugBreakpoint(const Path& source, u32 line) override { return m_system.setDebugBreakpoint(source, line); }
	bool removeDebugBreakpoint(const Path& source, u32 line) override { return m_system.removeDebugBreakpoint(source, line); }

	void createTypeDesc(EvoxTypeDesc& dst, const ex_type* src) {
		dst.type_name = fromEvox(ex_type_get_name(src));
		dst.kind = ex_type_get_kind(src);
		const u32 num_fields = ex_type_struct_field_count(src);
		dst.fields.reserve(num_fields);
		for (u32 i = 0; i < num_fields; ++i) {
			const ex_type* field_type = ex_type_struct_field_type(src, i);
			if (!isSerializableType(*field_type)) continue;
			EvoxFieldDesc& field = dst.fields.emplace(m_allocator);
			field.name = fromEvox(ex_type_struct_field_name(src, i));
			createTypeDesc(field.type, field_type);
		}
	}

	EvoxDataType* getDataStorage(StringView type_name) {
		for (EvoxDataType& t : m_data_storage) {
			StringView tmp = fromEvox(ex_type_get_name(t.type));
			if (equalStrings(tmp, type_name)) return &t;
		}

		for (const ex_type* type : m_system.getEvoxDataTypes()) {
			ASSERT(type);
			const ex_string_view name = ex_type_get_name(type);
			if (!equalStrings(fromEvox(name), type_name)) continue;

			return &m_data_storage.emplace(type, m_allocator);
		}
		return nullptr;
	}

	i32 findField(const ex_type* conatiner_type, StringView field_name) {
		u32 num_fields = ex_type_struct_field_count(conatiner_type);
		for (u32 i = 0; i < num_fields; ++i) {
			StringView fn = fromEvox(ex_type_struct_field_name(conatiner_type, i));
			if (equalStrings(fn, field_name)) return i;
		}
		return -1;
	}

	static u32 getPackedSize(const ex_type* type) {
		if (!isSerializableType(*type)) return 0;
		if (ex_type_get_kind(type) != EX_TYPE_STRUCT) return ex_type_get_size(type);
		u32 size = 0;
		for (u32 i = 0, count = ex_type_struct_field_count(type); i < count; ++i) {
			size += getPackedSize(ex_type_struct_field_type(type, i));
		}
		return size;
	}

	void packValues(const ex_type* src_type, u8* dst, u32 dst_stride, const u8* src, u32 src_stride, u32 num_values) {
		if (num_values == 0) return;
		const ex_type_kind kind = ex_type_get_kind(src_type);
		if (kind != EX_TYPE_STRUCT) {
			const u32 size = ex_type_get_size(src_type);
			for (u32 i = 0; i < num_values; ++i) {
				if (kind == EX_TYPE_CPTR) memset(dst, 0, size);
				else memcpy(dst, src, size);
				dst += dst_stride;
				src += src_stride;
			}
			return;
		}

		u32 dst_offset = 0;
		for (u32 i = 0, count = ex_type_struct_field_count(src_type); i < count; ++i) {
			const ex_type* field_type = ex_type_struct_field_type(src_type, i);
			const u32 packed_size = getPackedSize(field_type);
			if (packed_size == 0) continue;
			const u32 src_offset = ex_type_struct_field_offset(src_type, i);
			packValues(field_type, dst + dst_offset, dst_stride, src + src_offset, src_stride, num_values);
			dst_offset += packed_size;
		}
	}

	void remapEntityProperties(const EvoxTypeDesc& type_desc, u8* values, u32 stride, u32 num_values, const EntityMap& entity_map) {
		if (type_desc.type_name == EVOX_ENTITY_TYPE) {
			u32 index_offset = 0;
			bool has_index = false;
			for (const EvoxFieldDesc& field : type_desc.fields) {
				if (field.name == "index") {
					has_index = true;
					break;
				}
				index_offset += field.type.getSize();
			}
			if (!has_index) return;
			for (u32 i = 0; i < num_values; ++i) {
				i32 source_index;
				memcpy(&source_index, values + i * stride + index_offset, sizeof(source_index));
				const EntityPtr mapped = source_index >= 0 ? entity_map.get(EntityPtr(source_index)) : INVALID_ENTITY;
				memcpy(values + i * stride + index_offset, &mapped.index, sizeof(mapped.index));
			}
			return;
		}
		if (type_desc.kind != EX_TYPE_STRUCT) return;
		u32 offset = 0;
		for (const EvoxFieldDesc& field : type_desc.fields) {
			const u32 size = field.type.getSize();
			if (size > 0) remapEntityProperties(field.type, values + offset, stride, num_values, entity_map);
			offset += size;
		}
	}

	void copyValues(const ex_type* dst_type, const EvoxTypeDesc& src_type_desc, u8* dst, u32 dst_stride, const u8* src, u32 src_stride, u32 num_values) {
		switch (src_type_desc.kind) {
			case EX_TYPE_CPTR:
				// keep cptr null
				return;
			case EX_TYPE_BOOL:
			case EX_TYPE_U8:
			case EX_TYPE_I8:
			case EX_TYPE_U16:
			case EX_TYPE_I16:
			case EX_TYPE_I32:
			case EX_TYPE_U32:
			case EX_TYPE_ENUM:
			case EX_TYPE_I64:
			case EX_TYPE_U64:
			case EX_TYPE_F32:
			case EX_TYPE_F64: {
				u32 size = ex_type_get_size(dst_type);
				for (i32 i = 0, c = num_values; i < c; ++i) {
					memcpy(dst, src, size);
					dst += dst_stride;
					src += src_stride;
				}
				return;
			}
			case EX_TYPE_STRUCT: {
				u32 src_offset = 0;
				for (const EvoxFieldDesc& f : src_type_desc.fields) {
					const u32 field_size = f.type.getSize();
					if (field_size == 0) continue;
					i32 field_index = findField(dst_type, f.name);
					if (field_index < 0) {
						src_offset += field_size;
						continue; // field no longer exists
					}

					const ex_type* dst_field_type = ex_type_struct_field_type(dst_type, field_index);
					if (f.type.kind != ex_type_get_kind(dst_field_type)) {
						src_offset += field_size;
						continue; // field kind changed
					}

					u32 dst_offset = ex_type_struct_field_offset(dst_type, field_index);
					copyValues(dst_field_type, f.type, dst + dst_offset, dst_stride, src + src_offset, src_stride, num_values);
					src_offset += field_size;
				}
				// Entity values are serialized as a struct. Their world pointer is
				// runtime-only and must point at this world after loading.
				if (src_type_desc.type_name == EVOX_ENTITY_TYPE) {
					const i32 dst_index_field = findField(dst_type, "index");
					const i32 dst_world_field = findField(dst_type, "world");
					if (dst_index_field >= 0 && dst_world_field >= 0) {
						const u32 dst_world_offset = ex_type_struct_field_offset(dst_type, dst_world_field);
						for (u32 i = 0; i < num_values; ++i) {
							i32 index;
							memcpy(&index, dst + i * dst_stride + ex_type_struct_field_offset(dst_type, dst_index_field), sizeof(index));
							World* world = index >= 0 ? &m_world : nullptr;
							memcpy(dst + i * dst_stride + dst_world_offset, &world, sizeof(world));
						}
					}
				}
				return;
			}
			default:
				ASSERT(false); // TODO
				return;
		}
	}

	void applyPendingData() {
		for (i32 pending_idx = m_pending_types.size() - 1; pending_idx >= 0; --pending_idx) {
			const PendingType& src = m_pending_types[pending_idx];
			if (src.entities.empty()) {
				m_pending_types.swapAndPop(pending_idx);
				continue;
			}

			EvoxDataType* dst = getDataStorage(src.type_desc.type_name);
			if (!dst) continue; // type is not available yet
			if (src.type_desc.kind != ex_type_get_kind(dst->type)) continue; // kind changed

			i32 old_num_entities = dst->entities.size();
			dst->entities.resize(old_num_entities + src.entities.size());
			dst->values.resize((old_num_entities + src.entities.size()) * dst->element_size);
			memset(dst->values.data + old_num_entities * dst->element_size
				, 0
				, src.entities.size() * dst->element_size);
			memcpy(dst->entities.data() + old_num_entities, src.entities.data(), src.entities.byte_size());

			copyValues(dst->type, src.type_desc, dst->values.data + old_num_entities * dst->element_size, dst->element_size, src.values.data(), src.type_desc.getSize(), src.entities.size());
			for (i32 i = 0, count = src.entities.size(); i < count; ++i) {
				const EntityRef e = src.entities[i];
				injectEntity(*dst, dst->values.data + (old_num_entities + i) * dst->element_size, e);
				EvoxComponent& cmp = m_components[e];
				cmp.data.push({dst->type, (u32)old_num_entities + i});
			}
			m_pending_types.swapAndPop(pending_idx);
		}
	}

	void removePendingData(EntityRef entity) {
		for (PendingType& type : m_pending_types) {
			const u32 value_size = type.type_desc.getSize();
			for (i32 i = (i32)type.entities.size() - 1; i >= 0; --i) {
				if (type.entities[i] != entity) continue;

				const u32 last = type.entities.size() - 1;
				if ((u32)i != last && value_size > 0) {
					memcpy(type.values.getMutableData() + (u32)i * value_size
						, type.values.data() + last * value_size
						, value_size);
				}
				type.entities.swapAndPop(i);
				type.values.resize(last * value_size);
			}
		}
	}

	void injectEntity(const EvoxDataType& data_type, u8* value, EntityRef entity) {
		for (u32 i = 0, count = ex_type_struct_field_count(data_type.type); i < count; ++i) {
			bool inject = false;
			for (u32 j = 0, attribute_count = ex_type_struct_field_attribute_count(data_type.type, i); j < attribute_count; ++j) {
				const ex_attribute attribute = ex_type_struct_field_attribute_value(data_type.type, i, j);
				if (!attribute.type) continue;

				const ex_string_view name = ex_type_get_name(attribute.type);
				if (StringView(name.begin, (u64)name.length) == EVOX_OWNER_ATTRIBUTE_TYPE) {
					inject = true;
					break;
				}
			}
			if (!inject) continue;

			const ex_type* field_type = ex_type_struct_field_type(data_type.type, i);
			if (!field_type || ex_type_get_kind(field_type) != EX_TYPE_STRUCT) continue;

			const ex_string_view type_name = ex_type_get_name(field_type);
			if (StringView(type_name.begin, (u64)type_name.length) != EVOX_ENTITY_TYPE) continue;

			u8* field_value = value + ex_type_struct_field_offset(data_type.type, i);
			for (u32 j = 0, field_count = ex_type_struct_field_count(field_type); j < field_count; ++j) {
				const ex_string_view name = ex_type_struct_field_name(field_type, j);
				const StringView field_name(name.begin, (u64)name.length);
				u8* dst = field_value + ex_type_struct_field_offset(field_type, j);
				const ex_type* member_type = ex_type_struct_field_type(field_type, j);
				if (field_name == "index" && member_type && ex_type_get_kind(member_type) == EX_TYPE_I32) {
					memcpy(dst, &entity.index, sizeof(entity.index));
				}
				else if (field_name == "world" && member_type && ex_type_get_kind(member_type) == EX_TYPE_CPTR) {
					World* world = &m_world;
					memcpy(dst, &world, sizeof(world));
				}
			}
		}
	}

	static i32 findDataRef(const EvoxComponent& component, const ex_type* type) {
		return component.data.find([type](const EvoxDataRef& ref) { return ref.type == type; });
	}

	EvoxDataType* findDataType(const ex_type* type) {
		const i32 index = m_data_storage.find([type](const EvoxDataType& data_type) { return data_type.type == type; });
		return index >= 0 ? &m_data_storage[index] : nullptr;
	}

	const EvoxDataType* findDataType(const ex_type* type) const {
		const i32 index = m_data_storage.find([type](const EvoxDataType& data_type) { return data_type.type == type; });
		return index >= 0 ? &m_data_storage[index] : nullptr;
	}

	void removeEvoxDataAt(EvoxComponent& component, u32 ref_index) {
		const EvoxDataRef ref = component.data[ref_index];
		EvoxDataType* data_type = findDataType(ref.type);
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
	EvoxSystemImpl& m_system;
	IAllocator& m_allocator;
	Array<EvoxDataType> m_data_storage;
	HashMap<EntityRef, EvoxComponent> m_components;
	Array<PendingType> m_pending_types;
};

EvoxSystemImpl::EvoxSystemImpl(Engine& engine)
	: m_engine(engine)
	, m_allocator(engine.getAllocator(), "evox")
	, m_evox_resource_manager(m_allocator)
	, m_path("scripts/main.evox")
	, m_native_functions(m_allocator)
	, m_data_types(m_allocator)
	, m_modules(m_allocator)
{
	m_host = {};
	Evox::gatherCoreFunctions(m_native_functions);
	EvoxModuleImpl::reflect();
	m_evox_resource_manager.create(EvoxResource::TYPE, m_engine.getResourceManager());
}

u32 EvoxModuleImpl::EvoxTypeDesc::getSize() const {
	switch (kind) {
		case EX_TYPE_BOOL:
		case EX_TYPE_I8:
		case EX_TYPE_U8: return 1;
		case EX_TYPE_I16:
		case EX_TYPE_U16: return 2;
		case EX_TYPE_I32:
		case EX_TYPE_U32:
		case EX_TYPE_ENUM:
		case EX_TYPE_F32: return 4;
		case EX_TYPE_I64:
		case EX_TYPE_U64:
		case EX_TYPE_CPTR:
		case EX_TYPE_F64: return 8;
		case EX_TYPE_STRUCT: {
			u32 sum = 0;
			for (const EvoxFieldDesc& f : fields) {
				sum += f.type.getSize();
			}
			return sum;
		}
		default:
			ASSERT(false); // unsupported serialized type
			return 0;
	}
}

void EvoxSystemImpl::createModules(World& world) {
	loadRoot();
	auto module = UniquePtr<EvoxModuleImpl>::create(m_allocator, world, *this);
	world.addModule(module.move());
}

IModule* createEvoxModule(World& world);
void destroyEvoxModule(IModule* module);

LUMIX_PLUGIN_ENTRY(evox) {
	return LUMIX_NEW(engine.getAllocator(), EvoxSystemImpl)(engine);
}

} // namespace Lumix
