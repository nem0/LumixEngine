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

struct EvoxDiagnosticsContext {
	String* message = nullptr;
	ex_host* host = nullptr;
};

static void printEvoxMessage(void* userdata, ex_string_view msg) {
	EvoxDiagnosticsContext* ctx = (EvoxDiagnosticsContext*)userdata;
	if (ctx->message) ctx->message->append(StringView(msg.begin, (u64)msg.length));
}

static ex_string_view toLs(StringView value) {
	return {value.data, (i64)value.length};
}

static ex_string_view toLs(const char* value) {
	return {value, (i64)stringLength(value)};
}

static void bindCoreFunctions(ex_module* module, ex_runtime* runtime, IAllocator& allocator) {
	HashMap<NativeFunctionKey, ex_native_fn, NativeFunctionKeyHash> functions(allocator);
	Lumix::Evox::gatherCoreFunctions(functions);
	for (i32 unit_index = 0, unit_count = ex_module_get_unit_count(module); unit_index < unit_count; ++unit_index) {
		ex_unit* unit = ex_module_get_unit(module, unit_index);
		const ex_string_view path = ex_unit_get_path(unit);
		const StringView unit_path(path.begin, path.length);
		if (unit_path == "std:math" || unit_path == "std:mem") continue;
		for (i32 function_index = 0, function_count = ex_unit_get_native_function_count(unit); function_index < function_count; ++function_index) {
			const ex_string_view name = ex_unit_get_native_function_name(unit, function_index);
			const NativeFunctionKey key{unit_path, {name.begin, (u64)name.length}};
			auto iter = functions.find(key);
			if (!iter.isValid()) {
				logError("Evox : failed to bind native function ", key.unit_path, ".", key.name);
				continue;
			}
			ex_runtime_set_native_function_callback(runtime, unit, function_index, iter.value());
		}
	}
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
	}

	void stopGame() override {
		m_is_game_running = false;
	}


	const char* getName() const override { return "evox_system"; }
	Engine& getEngine() override { return m_engine; }

	void serialize(OutputMemoryStream& out) const override {}

	bool deserialize(i32, InputMemoryStream&) override { return true; }

	void registerModule(EvoxModule& module) override {
		m_modules.push(&module);
		module.setEvoxDataTypes(m_data_types);
	}

	void unregisterModule(EvoxModule& module) override { m_modules.eraseItem(&module); }
	bool isReady() const override { return m_is_ready; }
	Span<const ex_type*> getEvoxDataTypes() const override { return m_data_types; }
	ex_runtime* getDebugRuntime() override { return m_runtime; }
	const Path& getDebugPath() const override { return m_path; }

	bool setDebugBreakpoint(const Path& source, u32 line) override {
		return m_bytecode && ex_debug_set_breakpoint(m_bytecode, toLs(source.c_str()), line, nullptr) != EX_RESULT_FAILURE;
	}

	bool removeDebugBreakpoint(const Path& source, u32 line) override {
		return m_bytecode && ex_debug_remove_breakpoint(m_bytecode, toLs(source.c_str()), line) != EX_RESULT_FAILURE;
	}

	void createModules(World& world) override;

	void update(float time_delta) override {
		if (!m_is_ready || !m_runtime || ex_debug_is_suspended(m_runtime)) return;
		if (!m_is_game_running) return;

		const ex_string_view function_name = toLs("update");
		if (ex_bytecode_runtime_result_kind(m_runtime, function_name) == EX_TYPE_INVALID) return;

		ex_push_f32(m_runtime, time_delta);
		if (ex_call(m_runtime, function_name) == EX_RESULT_FAILURE) logError("Evox update failed");
	}

	void loadRoot() {
		if (m_resource) return;
		m_resource = m_engine.getResourceManager().load<EvoxResource>(m_path);
		if (m_resource) m_resource->onLoaded<&EvoxSystemImpl::onResourceChanged>(this);
	}

	void addWorld(World& world) {
		const ex_string_view function_name = toLs("addWorld");
		if (ex_bytecode_runtime_result_kind(m_runtime, function_name) == EX_TYPE_INVALID) return;
		ex_push_ptr(m_runtime, &world);
		if (ex_call(m_runtime, function_name) == EX_RESULT_FAILURE) {
			logError("Evox addWorld failed");
		}
	}

	static bool isEvoxDataType(const ex_type& type) {
		if (ex_type_get_kind(&type) != EX_TYPE_STRUCT) return false;

		for (u32 i = 0, count = ex_type_attribute_count(&type); i < count; ++i) {
			const ex_attribute attribute = ex_type_attribute_value(&type, i);
			if (!attribute.type) continue;

			const ex_string_view name = ex_type_get_name(attribute.type);
			if (StringView(name.begin, name.length) == "Data") return true;
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
		if (m_runtime) { ex_runtime_destroy(m_runtime); m_runtime = nullptr; }
		if (m_bytecode) { ex_bytecode_destroy(m_bytecode); m_bytecode = nullptr; }
		if (m_module) { ex_module_destroy(m_module); m_module = nullptr; }
		if (m_host.arena.allocate) {
			ex_default_arena_destroy(&m_host.arena);
			m_host.arena = {};
		}
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
		if (!m_module || !ex_module_compile(m_module, toLs(m_resource->getSourceCode()), toLs(m_path.c_str()), &resolveImport, &imports)) {
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
		m_runtime = ex_runtime_create(m_bytecode, &m_host);
		if (!m_runtime) return false;
		bindCoreFunctions(m_module, m_runtime, m_allocator);
		for (EvoxModule* module : m_modules) module->setEvoxDataTypes(m_data_types);
		const ex_string_view init_name = toLs("init");
		if (ex_bytecode_runtime_result_kind(m_runtime, init_name) != EX_TYPE_INVALID) {
			ex_push_ptr(m_runtime, &m_engine.getInputSystem());
			if (ex_call(m_runtime, init_name) == EX_RESULT_FAILURE) {
				logError("Evox init failed");
				return false;
			}
		}
		for (EvoxModule* module : m_modules) addWorld(module->getWorld());
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

		const ex_type* type;
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

	struct PendingField {
		explicit PendingField(IAllocator& allocator)
			: name(allocator)
			, type_name(allocator)
			, values(allocator)
		{}
		PendingField(PendingField&& rhs)
			: name(static_cast<String&&>(rhs.name))
			, type_name(static_cast<String&&>(rhs.type_name))
			, size(rhs.size)
			, values(static_cast<OutputMemoryStream&&>(rhs.values))
		{}

		String name;
		String type_name;
		u32 size;
		OutputMemoryStream values;
	};

	struct PendingType {
		explicit PendingType(IAllocator& allocator)
			: name(allocator)
			, fields(allocator)
			, entities(allocator)
		{}
		PendingType(PendingType&& rhs)
			: name(static_cast<String&&>(rhs.name))
			, fields(rhs.fields.move())
			, entities(rhs.entities.move())
		{}

		String name;
		Array<PendingField> fields;
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

	static bool isSerializableField(const ex_type& type) {
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
			case EX_TYPE_F64: return true;
			default: return false;
		}
	}

	void serialize(OutputMemoryStream& out) override {
		out.write(m_components.size());
		for (auto iter = m_components.begin(), end = m_components.end(); iter != end; ++iter) {
			out.write(iter.key());
		}

		// Live and pending types share the same serialized representation.
		out.write(m_data_storage.size() + m_pending_types.size());
		for (const EvoxDataType& data : m_data_storage) {
			const ex_type* type = data.type;
			const ex_string_view type_name = ex_type_get_name(type);
			out.writeString({type_name.begin, (u64)type_name.length});
			u32 num_fields = 0;
			for (u32 i = 0, count = ex_type_struct_field_count(type); i < count; ++i) {
				if (isSerializableField(*ex_type_struct_field_type(type, i))) ++num_fields;
			}
			out.write(num_fields);
			const u32 num_values = data.entities.size();
			out.write(num_values);
			for (u32 i = 0, field_count = ex_type_struct_field_count(type); i < field_count; ++i) {
				const ex_type* field_type = ex_type_struct_field_type(type, i);
				if (!isSerializableField(*field_type)) continue;
				const ex_string_view field_name = ex_type_struct_field_name(type, i);
				out.writeString({field_name.begin, (u64)field_name.length});
				const u32 field_offset = ex_type_struct_field_offset(type, i);
				const ex_string_view field_type_name = ex_type_get_name(field_type);
				out.writeString({field_type_name.begin, (u64)field_type_name.length});
				const u32 field_size = ex_type_get_size(field_type);
				out.write(field_size);
				for (u32 j = 0; j < num_values; ++j) {
					out.write(data.values.data + field_offset + j * data.element_size, field_size);
				}
			}
			out.write(data.entities.begin(), data.entities.byte_size());
		}
		for (const PendingType& type : m_pending_types) {
			out.writeString(type.name);
			out.write(type.fields.size());
			out.write(type.entities.size());
			for (const PendingField& field : type.fields) {
				out.writeString(field.name);
				out.writeString(field.type_name);
				out.write(field.size);
				out.write(field.values.data(), field.values.size());
			}
			out.write(type.entities.begin(), type.entities.byte_size());
		}
	}

	void deserialize(InputMemoryStream& in, const EntityMap& entity_map, i32 version) override {
		if (version <= 0) in.readString();
		if (version <= 1) return;

		const u32 component_count = in.read<u32>();
		for (u32 i = 0; i < component_count; ++i) {
			EntityRef entity = entity_map.get(in.read<EntityRef>());
			createEvox(entity);
		}

		const u32 type_count = in.read<u32>();
		m_pending_types.reserve(m_pending_types.size() + type_count);
		for (u32 i = 0; i < type_count; ++i) {
			PendingType& type = m_pending_types.emplace(m_allocator);
			in.read(type.name);
			const u32 field_count = in.read<u32>();
			const u32 value_count = in.read<u32>();
			type.fields.reserve(field_count);
			for (u32 j = 0; j < field_count; ++j) {
				PendingField& field = type.fields.emplace(m_allocator);
				in.read(field.name);
				in.read(field.type_name);
				in.read(field.size);
				field.values.resize(field.size * value_count);
				in.read(field.values.getMutableData(), field.values.size());
			}
			type.entities.resize(value_count);
			in.read(type.entities.begin(), type.entities.byte_size());
			for (EntityRef& entity : type.entities) entity = entity_map.get(entity);
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
		stashCurrentData();
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
	ex_runtime* getDebugRuntime() override { return m_system.getDebugRuntime(); }
	const Path& getDebugPath() const override { return m_system.getDebugPath(); }
	bool setDebugBreakpoint(const Path& source, u32 line) override { return m_system.setDebugBreakpoint(source, line); }
	bool removeDebugBreakpoint(const Path& source, u32 line) override { return m_system.removeDebugBreakpoint(source, line); }

private:
	void stashCurrentData() {
		for (const EvoxDataType& data : m_data_storage) {
			if (data.entities.empty()) continue;

			PendingType& pending = m_pending_types.emplace(m_allocator);
			const ex_string_view name = ex_type_get_name(data.type);
			pending.name = StringView(name.begin, name.length);
			data.entities.copyTo(pending.entities);
			for (u32 i = 0, field_count = ex_type_struct_field_count(data.type); i < field_count; ++i) {
				const ex_type* field_type = ex_type_struct_field_type(data.type, i);
				if (!isSerializableField(*field_type)) continue;

				PendingField& field = pending.fields.emplace(m_allocator);
				const ex_string_view field_name = ex_type_struct_field_name(data.type, i);
				field.name = StringView(field_name.begin, field_name.length);
				const ex_string_view field_type_name = ex_type_get_name(field_type);
				field.type_name = StringView(field_type_name.begin, field_type_name.length);
				field.size = ex_type_get_size(field_type);
				const u32 offset = ex_type_struct_field_offset(data.type, i);
				field.values.resize(field.size * data.entities.size());
				for (u32 j = 0; j < (u32)data.entities.size(); ++j) {
					memcpy(field.values.getMutableData() + j * field.size,
						data.values.data + j * data.element_size + offset,
						field.size);
				}
			}
		}
	}

	void applyPendingData() {
		for (i32 pending_idx = m_pending_types.size() - 1; pending_idx >= 0; --pending_idx) {
			PendingType& pending = m_pending_types[pending_idx];
			EvoxDataType* data_type = nullptr;
			for (EvoxDataType& candidate : m_data_storage) {
				const ex_string_view name = ex_type_get_name(candidate.type);
				if (pending.name == StringView(name.begin, name.length)) {
					data_type = &candidate;
					break;
				}
			}
			if (!data_type) continue;

			const u32 base_index = data_type->entities.size();
			const u32 count = pending.entities.size();
			const u32 old_size = data_type->values.size;
			data_type->values.resize(old_size + count * data_type->element_size);
			memset(data_type->values.data + old_size, 0, count * data_type->element_size);

			for (const PendingField& field : pending.fields) {
				for (u32 i = 0, field_count = ex_type_struct_field_count(data_type->type); i < field_count; ++i) {
					const ex_string_view current_name = ex_type_struct_field_name(data_type->type, i);
					if (field.name != StringView(current_name.begin, current_name.length)) continue;

					const ex_type* current_type = ex_type_struct_field_type(data_type->type, i);
					const ex_string_view current_type_name = ex_type_get_name(current_type);
					if (field.size != ex_type_get_size(current_type)
						|| field.type_name != StringView(current_type_name.begin, current_type_name.length))
					{
						logWarning("Evox: ignoring incompatible field ", pending.name, ".", field.name);
						break;
					}
					const u32 offset = ex_type_struct_field_offset(data_type->type, i);
					for (u32 j = 0; j < count; ++j) {
						memcpy(data_type->values.data + old_size + j * data_type->element_size + offset,
							field.values.data() + j * field.size,
							field.size);
					}
					break;
				}
			}

			data_type->entities.reserve(base_index + count);
			for (u32 i = 0; i < count; ++i) {
				const EntityRef entity = pending.entities[i];
				data_type->entities.push(entity);
				injectEntity(*data_type, data_type->values.data + old_size + i * data_type->element_size, entity);
			}

			for (u32 i = 0; i < count; ++i) {
				auto cmp = m_components.find(pending.entities[i]);
				if (cmp.isValid() && findDataRef(cmp.value(), data_type->type) < 0) {
					cmp.value().data.push({data_type->type, base_index + i});
				}
			}
			m_pending_types.swapAndPop(pending_idx);
		}
	}

	void removePendingData(EntityRef entity) {
		for (PendingType& type : m_pending_types) {
			for (i32 i = type.entities.size() - 1; i >= 0; --i) {
				if (type.entities[i] != entity) continue;
				const u32 last = type.entities.size() - 1;
				for (PendingField& field : type.fields) {
					if (i != (i32)last) {
						memcpy(field.values.getMutableData() + i * field.size,
							field.values.data() + last * field.size,
							field.size);
					}
					field.values.resize(last * field.size);
				}
				type.entities.swapAndPop(i);
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
				// TODO not string based compare
				if (StringView(name.begin, (u64)name.length) == "Owner") {
					inject = true;
					break;
				}
			}
			if (!inject) continue;

			const ex_type* field_type = ex_type_struct_field_type(data_type.type, i);
			if (!field_type || ex_type_get_kind(field_type) != EX_TYPE_STRUCT) continue;

			const ex_string_view type_name = ex_type_get_name(field_type);
			if (StringView(type_name.begin, (u64)type_name.length) != "Entity") continue;

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
	, m_data_types(m_allocator)
	, m_modules(m_allocator)
{
	m_host.arena = {};
	EvoxModuleImpl::reflect();
	m_evox_resource_manager.create(EvoxResource::TYPE, m_engine.getResourceManager());
}

void EvoxSystemImpl::createModules(World& world) {
	loadRoot();
	auto module = UniquePtr<EvoxModuleImpl>::create(m_allocator, world, *this);
	world.addModule(module.move());
	if (m_is_ready) addWorld(world);
}

IModule* createEvoxModule(World& world);
void destroyEvoxModule(IModule* module);

LUMIX_PLUGIN_ENTRY(evox) {
	return LUMIX_NEW(engine.getAllocator(), EvoxSystemImpl)(engine);
}

} // namespace Lumix
