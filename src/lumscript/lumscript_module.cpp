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
	if (ctx->message) ctx->message->append(StringView(msg.begin, msg.end));
}

static ls_string_view toLs(StringView value) {
	return {value.begin, value.end};
}

static ls_string_view toLs(const char* value) {
	return {value, value + stringLength(value)};
}

static void bindCoreFunctions(ls_module* module, ls_runtime* runtime, IAllocator& allocator) {
	HashMap<NativeFunctionKey, ls_native_fn, NativeFunctionKeyHash> functions(allocator);
	Lumix::LumScript::gatherCoreFunctions(functions);
	for (i32 unit_index = 0, unit_count = ls_module_get_unit_count(module); unit_index < unit_count; ++unit_index) {
		ls_unit* unit = ls_module_get_unit(module, unit_index);
		const ls_string_view path = ls_unit_get_path(unit);
		const StringView unit_path(path.begin, path.end);
		if (unit_path == "std:math" || unit_path == "std:mem") continue;
		for (i32 function_index = 0, function_count = ls_unit_get_native_function_count(unit); function_index < function_count; ++function_index) {
			const ls_string_view name = ls_unit_get_native_function_name(unit, function_index);
			const NativeFunctionKey key{unit_path, {name.begin, name.end}};
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
	struct Script {
		Path path;
		ls_module* module = nullptr;
		ls_bytecode* bytecode = nullptr;
		ls_runtime* runtime = nullptr;
		LumScriptResource* resource = nullptr;
		bool is_ready = false;
	};

	LumScriptModuleImpl(World& world, ISystem& system)
		: m_world(world)
		, m_system(system)
		, m_allocator(world.getAllocator())
	{
		m_host.arena = {};
	}

	~LumScriptModuleImpl() {
		destroyScript();
		if (m_script.resource) {
			m_script.resource->getObserverCb().unbind<&LumScriptModuleImpl::onResourceChanged>(this);
			m_script.resource->decRefCount();
			m_script.resource = nullptr;
		}
	}

	const char* getName() const override { return "lumscript"; }
	World& getWorld() override { return m_world; }
	ISystem& getSystem() const override { return m_system; }

	void serialize(OutputMemoryStream& serializer) override {
		// Serialize world-level script data
		serializer.writeString(m_script.path.c_str());
	}

	void deserialize(InputMemoryStream& deserializer, const EntityMap& entity_map, i32 version) override {
		// Deserialize world-level script data
		const char* path = deserializer.readString();
		if (path && path[0]) {
			load(Path(path));
		}
	}

	void update(float time_delta) override {
		if (!m_script.is_ready || !m_script.runtime) return;
		if (ls_debug_is_suspended(m_script.runtime)) return;

		ls_push_f32(m_script.runtime, time_delta);
		const ls_result result = ls_call(m_script.runtime, toLs("update"));
		if (result == LS_RESULT_FAILURE) {
			logError("LumScript update failed");
		}
	}

	void load(const Path& path) override {
		// Unload old resource
		if (m_script.resource) {
			m_script.resource->getObserverCb().unbind<&LumScriptModuleImpl::onResourceChanged>(this);
			m_script.resource->decRefCount();
			m_script.resource = nullptr;
		}
		destroyScript();

		m_script.path = path;
		m_script.is_ready = false;

		// Load new resource
		if (!path.isEmpty()) {
			LumScriptSystem* lua_system = static_cast<LumScriptSystem*>(&m_system);
			auto* res = lua_system->getEngine().getResourceManager().load<LumScriptResource>(path);
			m_script.resource = res;
			if (res) res->onLoaded<&LumScriptModuleImpl::onResourceChanged>(this);
		}
	}

	bool isReady() const override { return m_script.is_ready; }
	ls_runtime* getDebugRuntime() override { return m_script.runtime; }
	const Path& getDebugPath() const override { return m_script.path; }

	bool setDebugBreakpoint(const Path& source, u32 line) override {
		if (!m_script.bytecode) return false;
		return ls_debug_set_breakpoint(m_script.bytecode, toLs(source.c_str()), line, nullptr) != LS_RESULT_FAILURE;
	}

	bool removeDebugBreakpoint(const Path& source, u32 line) override {
		if (!m_script.bytecode) return false;
		return ls_debug_remove_breakpoint(m_script.bytecode, toLs(source.c_str()), line) != LS_RESULT_FAILURE;
	}

	void startGame() override {
		// Auto-load {world_name}.lum
		Path lum_path{m_world.getPartitions()[0].name};
		if (lum_path.isEmpty()) return;

		char tmp[MAX_PATH];
		copyString(tmp, lum_path.c_str());
		Path::replaceExtension(tmp, "lum");
		lum_path = tmp;

		LumScriptSystem* lua_system = static_cast<LumScriptSystem*>(&m_system);
		if (!lua_system->getEngine().getFileSystem().fileExists(lum_path)) {
			load(Path());
			return;
		}
		load(lum_path);
	}

private:
	struct ImportContext {
		ls_module* module;
		World* world;
		FileSystem* filesystem;
		IAllocator* allocator;
		Array<OutputMemoryStream> sources;

		ImportContext(ls_module* module, World& world, FileSystem& filesystem, IAllocator& allocator)
			: module(module)
			, world(&world)
			, filesystem(&filesystem)
			, allocator(&allocator)
			, sources(allocator) {}
	};

	static int resolveImport(void* userdata, ls_string_view path, ls_string_view alias, ls_string_view* source) {
		ImportContext* ctx = (ImportContext*)userdata;
		if (!ctx || !ctx->module) return 0;

		StringView path_view(path.begin, path.end);
		if (startsWith(path_view, "core:")) {
			StringView name = path_view.withoutLeft(5);
			// TODO no .lum allowed
			const bool has_lum_extension = endsWith(name, ".lum");
			Path file_path = has_lum_extension ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".lum");
			OutputMemoryStream& blob = ctx->sources.emplace(*ctx->allocator);
			if (!ctx->filesystem->getContentSync(file_path, blob)) {
				ctx->sources.pop();
				return 0;
			}
			*source = {(const char*)blob.data(), (const char*)blob.data() + blob.size()};
			return 1;
		}
		return 0;
	}

	void onResourceChanged(Resource::State, Resource::State new_state, Resource& resource) {
		if (&resource != m_script.resource) return;
		if (new_state == Resource::State::READY) {
			m_script.is_ready = compileAndRun();
		} else {
			m_script.is_ready = false;
		}
	}

	void destroyRuntime() {
		if (m_script.runtime) {
			ls_runtime_destroy(m_script.runtime);
			m_script.runtime = nullptr;
		}
		if (m_script.bytecode) {
			ls_bytecode_destroy(m_script.bytecode);
			m_script.bytecode = nullptr;
		}
	}

	void destroyScript() {
		destroyRuntime();
		if (m_script.module) {
			ls_module_destroy(m_script.module);
			m_script.module = nullptr;
		}
		if (m_host.arena.allocate) {
			ls_default_arena_destroy(&m_host.arena);
			m_host.arena = {};
		}
	}

	bool compileAndRun() {
		if (!m_script.resource) return false;

		String diagnostics(m_allocator);
		LumScriptDiagnosticsContext diag_ctx = {&diagnostics, &m_host};
		m_host.diagnostics_userdata = &diag_ctx;
		m_host.print = &printLumScriptMessage;

		// Parse and compile the world script
		destroyScript();
		ls_default_arena_create(&m_host.arena);
		m_script.module = ls_module_create(&m_host);
		if (!m_script.module) return false;

		LumScriptSystem* lumscript_system = static_cast<LumScriptSystem*>(&m_system);
		ImportContext import_ctx(m_script.module, m_world, lumscript_system->getEngine().getFileSystem(), m_allocator);
		if (!ls_module_compile(m_script.module, toLs(m_script.resource->getSourceCode()), toLs(m_script.path.c_str()), &resolveImport, &import_ctx)) {
			m_host.diagnostics_userdata = nullptr;
			m_host.print = nullptr;
			logError("LumScript compilation failed: ", diagnostics);
			return false;
		}

		// Create runtime
		m_script.bytecode = ls_bytecode_compile(m_script.module, &m_host, nullptr);
		if (!m_script.bytecode) {
			m_host.diagnostics_userdata = nullptr;
			m_host.print = nullptr;
			logError("LumScript bytecode compilation failed: ", diagnostics);
			return false;
		}
		m_host.diagnostics_userdata = nullptr;
		m_host.print = nullptr;

		m_script.runtime = ls_runtime_create(m_script.bytecode, &m_host);
		if (!m_script.runtime) {
			destroyRuntime();
			return false;
		}
		bindCoreFunctions(m_script.module, m_script.runtime, m_allocator);

		ls_push_ptr(m_script.runtime, &m_world);
		ls_push_ptr(m_script.runtime, &static_cast<LumScriptSystem&>(m_system).getEngine().getInputSystem());
		if (!ls_call(m_script.runtime, toLs("init"))) {
			logError("LumScript init failed: ", diagnostics);
			return false;
		}

		return true;
	}

	World& m_world;
	ISystem& m_system;
	IAllocator& m_allocator;
	ls_host m_host;
	Script m_script;
};

struct LumScriptSystemImpl : LumScriptSystem {
	explicit LumScriptSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "lumscript")
		, m_lumscript_resource_manager(m_allocator) {
		// Register the LumScript resource type
		m_lumscript_resource_manager.create(LumScriptResource::TYPE, m_engine.getResourceManager());
	}

	const char* getName() const override { return "lumscript_system"; }
	Engine& getEngine() override { return m_engine; }

	void serialize(OutputMemoryStream& serializer) const override {}
	bool deserialize(i32 version, InputMemoryStream& serializer) override { return true; }

	void createModules(World& world) override {
		auto module = UniquePtr<LumScriptModuleImpl>::create(m_allocator, world, *this);
		world.addModule(module.move());
	}

	void updateScripts(float time_delta) override {
		// Called if needed for script-level updates across all modules
	}

private:
	Engine& m_engine;
	TagAllocator m_allocator;
	LumScriptResourceManager m_lumscript_resource_manager;
};

IModule* createLumScriptModule(World& world);
void destroyLumScriptModule(IModule* module);

LUMIX_PLUGIN_ENTRY(lumscript) {
	return LUMIX_NEW(engine.getAllocator(), LumScriptSystemImpl)(engine);
}

} // namespace Lumix
