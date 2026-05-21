#include "lumscript/lumscript_module.h"
#include "engine/world.h"
#include "engine/engine.h"
#include "engine/input_system.h"
#include "engine/resource_manager.h"
#include "engine/file_system.h"
#include "core/allocator.h"
#include "core/array.h"
#include "core/crt.h"
#include "core/stream.h"
#include "core/log.h"
#include "core/tag_allocator.h"
#include "core/path.h"
#include "lumscript/capi.h"
#include "lumscript/lumscript_resource.h"
#include "lumscript/lumscript_engine_api.h"

namespace Lumix {

using namespace LumScript;

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

static ls_value makeNativeValue(void* ptr, const char* type_name) {
	ls_value value = ls_value_make_null();
	value.type = ls_type_make_native(toLs(type_name), -1, 0);
	value.ptr = ptr;
	return value;
}

static ls_host makeAllocatorHost(IAllocator& allocator) {
	ls_host host = {};
	host.allocator_userdata = &allocator;
	host.allocate = [](void* userdata, size_t size, size_t align) -> void* {
		return ((IAllocator*)userdata)->allocate(size, align);
	};
	host.deallocate = [](void* userdata, void* ptr) {
		if (!ptr) return;
		((IAllocator*)userdata)->deallocate(ptr);
	};
	host.reallocate = [](void* userdata, void* ptr, size_t new_size, size_t old_size, size_t align) -> void* {
		return ((IAllocator*)userdata)->reallocate(ptr, new_size, old_size, align);
	};
	return host;
}

struct LumScriptModuleImpl : LumScriptModule {
	struct Script {
		Path path;
		ls_module* module = nullptr;
		ls_runtime* runtime = nullptr;
		LumScriptResource* resource = nullptr;
		bool is_ready = false;
	};

	LumScriptModuleImpl(World& world, ISystem& system)
		: m_world(world)
		, m_system(system)
		, m_allocator(world.getAllocator())
	{
	}

	~LumScriptModuleImpl() {
		if (m_script.runtime) {
			ls_runtime_destroy(m_script.runtime);
			m_script.runtime = nullptr;
		}
		if (m_script.module) {
			ls_module_destroy(m_script.module);
			m_script.module = nullptr;
		}
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
		
		// Call update function for world-level script
		// TODO host as member?
		String diagnostics(m_allocator);
		ls_host host = makeAllocatorHost(m_allocator);
		LumScriptDiagnosticsContext diag_ctx = { &diagnostics, &host };
		host.diagnostics_userdata = &diag_ctx;
		host.print = &printLumScriptMessage;

		ls_value dt_value = ls_value_make_f32(time_delta);
		if (!ls_runtime_call(m_script.runtime, toLs("update"), &dt_value, 1, nullptr, &host)) {
			logError("LumScript update: ", diagnostics);
		}
	}

	void load(const Path& path) override {
		// Unload old resource
		if (m_script.resource) {
			m_script.resource->getObserverCb().unbind<&LumScriptModuleImpl::onResourceChanged>(this);
			m_script.resource->decRefCount();
			m_script.resource = nullptr;
		}
		if (m_script.runtime) {
			ls_runtime_destroy(m_script.runtime);
			m_script.runtime = nullptr;
		}
		if (m_script.module) {
			ls_module_destroy(m_script.module);
			m_script.module = nullptr;
		}

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

	bool isReady() const override {
		return m_script.is_ready;
	}

	void startGame() override {
		// Auto-load {world_name}.lum
		Path lum_path { m_world.getPartitions()[0].name };
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
			, sources(allocator)
		{}
	};

	static bool isValidCoreImportPath(StringView path) {
		if (!startsWith(path, "core:")) return false;
		StringView name = path.withoutLeft(5);
		if (name.empty() || name[0] == '/' || name[0] == '\\') return false;
		return !find(name, "..") && !find(name, ':') && !find(name, '\\');
	}

	static int resolveImport(void* userdata, ls_string_view path, ls_string_view alias, ls_string_view* source) {
		ImportContext* ctx = (ImportContext*)userdata;
		if (!ctx || !ctx->module) return 0;

		StringView path_view(path.begin, path.end);
		StringView alias_view(alias.begin, alias.end);
		if (resolveEngineImport(*ctx->module, ctx->world, path_view, alias_view)) {
			*source = {};
			return 1;
		}
		if (isValidCoreImportPath(path_view)) {
			StringView name = path_view.withoutLeft(5);
			const bool has_lum_extension = endsWith(name, ".lum");
			Path file_path = has_lum_extension ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".lum");
			OutputMemoryStream& blob = ctx->sources.emplace(*ctx->allocator);
			if (!ctx->filesystem->getContentSync(file_path, blob)) {
				ctx->sources.pop();
				return 0;
			}
			*source = { (const char*)blob.data(), (const char*)blob.data() + blob.size() };
			return 1;
		}
		return 0;
	}

	void onResourceChanged(Resource::State, Resource::State new_state, Resource& resource) {
		if (&resource != m_script.resource) return;
		if (new_state == Resource::State::READY) {
			m_script.is_ready = compileAndRun();
		}
		else {
			m_script.is_ready = false;
		}
	}

	bool compileAndRun() {
		if (!m_script.resource) return false;

		String diagnostics(m_allocator);
		ls_host host = makeAllocatorHost(m_allocator);
		LumScriptDiagnosticsContext diag_ctx = { &diagnostics, &host };
		host.diagnostics_userdata = &diag_ctx;
		host.print = &printLumScriptMessage;
		
		// Parse and compile the world script
		if (m_script.module) {
			ls_module_destroy(m_script.module);
			m_script.module = nullptr;
		}
		m_script.module = ls_module_create(&host);
		if (!m_script.module) return false;
		
		LumScriptSystem* lumscript_system = static_cast<LumScriptSystem*>(&m_system);
		ImportContext import_ctx(m_script.module, m_world, lumscript_system->getEngine().getFileSystem(), m_allocator);
		if (!ls_module_compile(m_script.module, toLs(m_script.resource->getSourceCode()), toLs(m_script.path.c_str()), &host, &resolveImport, &import_ctx)) {
			logError("LumScript compilation failed: ", diagnostics);
			return false;
		}

		// Create runtime
		if (m_script.runtime) {
			ls_runtime_destroy(m_script.runtime);
			m_script.runtime = nullptr;
		}
		m_script.runtime = ls_runtime_create(m_script.module);
		if (!m_script.runtime) return false;

		ls_value args[] = {
			makeNativeValue(&m_world, "engine:world/World"),
			makeNativeValue(&static_cast<LumScriptSystem&>(m_system).getEngine().getInputSystem(), "engine:input/InputSystem")
		};
		if (ls_runtime_call(
			m_script.runtime,
			toLs("init"),
			args,
			2,
			nullptr,
			&host
		)) {
			return true;
		}

		logError("LumScript init failed: ", diagnostics);
		return false;
	}

	World& m_world;
	ISystem& m_system;
	IAllocator& m_allocator;
	Script m_script;
};

struct LumScriptSystemImpl : LumScriptSystem {
	explicit LumScriptSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "lumscript")
		, m_lumscript_resource_manager(m_allocator)
	{
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
